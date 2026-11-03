// See DeferredDevice.h. Not in the original.
#include "cube/DeferredDevice.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace cube {

namespace {

enum {
    MaxRenderTargets = 4,
    MaxPixelConstants = 224
};

// The recorded calls, each a Command followed by its data (padded to 16 bytes).
enum Op {
    OpSetRenderState,
    OpSetSamplerState,
    OpSetTextureStageState,
    OpSetTexture,
    OpSetTransform,
    OpSetVertexShaderConstantF,
    OpSetVertexShaderConstantI,
    OpSetVertexShaderConstantB,
    OpSetPixelShaderConstantF,
    OpSetPixelShaderConstantI,
    OpSetPixelShaderConstantB,
    OpSetVertexShader,
    OpSetPixelShader,
    OpSetVertexDeclaration,
    OpSetFVF,
    OpSetStreamSource,
    OpSetStreamSourceFreq,
    OpSetIndices,
    OpSetViewport,
    OpSetScissorRect,
    OpSetMaterial,
    OpSetLight,
    OpLightEnable,
    OpSetClipPlane,
    OpSetRenderTarget,
    OpSetDepthStencilSurface,
    OpClear,
    OpBeginScene,
    OpEndScene,
    OpDrawPrimitive,
    OpDrawIndexedPrimitive,
    OpDrawPrimitiveUP,
    OpDrawIndexedPrimitiveUP,
    OpUploadVertexBuffer,
    OpUploadIndexBuffer,
    OpPresent
};

struct alignas(16) Command {
    unsigned op;
    unsigned size; // of the whole entry, data included
    DWORD a, b, c, d, f, g, h;
    float e;
    bool flag;
    IUnknown *object;
};

UINT vertexCount(D3DPRIMITIVETYPE type, UINT primitives)
{
    switch (type) {
    case D3DPT_POINTLIST:
        return primitives;
    case D3DPT_LINELIST:
        return primitives * 2;
    case D3DPT_LINESTRIP:
        return primitives + 1;
    case D3DPT_TRIANGLELIST:
        return primitives * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:
        return primitives + 2;
    default:
        return 0;
    }
}

class DeferredDevice;

// A dynamic vertex or index buffer: while the device records, a lock hands out memory of the frame, and the
// unlock records the upload; otherwise the calls go to the buffer.
template <class Interface, class Description>
class DeferredBuffer final : public Interface {
public:
    DeferredBuffer(DeferredDevice *device, Interface *buffer, UINT length);
    // The buffer behind a wrapped one, or the buffer itself.
    static Interface *unwrap(Interface *buffer)
    {
        if (buffer && *reinterpret_cast<void *const *>(buffer) == s_vtable)
            return static_cast<DeferredBuffer *>(buffer)->m_buffer;
        return buffer;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
    {
        return m_buffer->QueryInterface(riid, ppvObject);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_references; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG references = --m_references;
        if (references == 0)
            delete this;
        return references;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, const void *data, DWORD data_size, DWORD flags) override
    {
        return m_buffer->SetPrivateData(guid, data, data_size, flags);
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void *pData, DWORD *pSizeOfData) override
    {
        return m_buffer->GetPrivateData(refguid, pData, pSizeOfData);
    }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) override { return m_buffer->FreePrivateData(refguid); }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) override { return m_buffer->SetPriority(PriorityNew); }
    DWORD STDMETHODCALLTYPE GetPriority() override { return m_buffer->GetPriority(); }
    void STDMETHODCALLTYPE PreLoad() override { m_buffer->PreLoad(); }
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return m_buffer->GetType(); }
    HRESULT STDMETHODCALLTYPE Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE Unlock() override;
    HRESULT STDMETHODCALLTYPE GetDesc(Description *pDesc) override { return m_buffer->GetDesc(pDesc); }

private:
    ~DeferredBuffer() { m_buffer->Release(); }

    static void *s_vtable;
    DeferredDevice *m_device;
    Interface *m_buffer;
    UINT m_length;
    std::atomic<ULONG> m_references;
    // The lock being recorded: its range and flags, and the memory handed out.
    bool m_recordedLock;
    UINT m_lockOffset, m_lockSize;
    DWORD m_lockFlags;
    std::vector<unsigned char> m_lockData;
};

template <class Interface, class Description>
void *DeferredBuffer<Interface, Description>::s_vtable;

class DeferredDevice final : public IDirect3DDevice9 {
public:
    explicit DeferredDevice(IDirect3DDevice9 *device)
        : m_device(device),
          m_references(1),
          m_threaded(false),
          m_recording(false),
          m_owner(std::this_thread::get_id()),
          m_current(0),
          m_pending(0),
          m_quit(false),
          m_presentResult(D3D_OK)
    {
        memset(m_renderTargets, 0, sizeof(m_renderTargets));
        m_depthStencil = 0;
        memset(m_pixelConstants, 0, sizeof(m_pixelConstants));
        fetchShadows();
        m_thread = std::thread([this]() { run(); });
    }

    void setThreaded(bool threaded) { m_threaded = threaded; }
    bool recording() const
    {
        return m_recording.load(std::memory_order_relaxed) && std::this_thread::get_id() == m_owner;
    }

    // Records the upload of a dynamic buffer's locked range.
    template <class Interface>
    void upload(Interface *buffer, UINT offset, UINT size, DWORD flags, const unsigned char *data)
    {
        Command *command =
            put(std::is_same<Interface, IDirect3DVertexBuffer9>::value ? OpUploadVertexBuffer : OpUploadIndexBuffer, size);
        command->a = offset;
        command->b = size;
        command->c = flags;
        command->object = keep(buffer);
        memcpy(command + 1, data, size);
    }

    // --- IDirect3DDevice9 ---
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
    // The device itself is this wrapper.
    if (!ppvObject)
        return E_POINTER;
    void *object = 0;
    HRESULT result = m_device->QueryInterface(riid, &object);
    if (result == S_OK && object == (void *)m_device) {
        m_device->Release();
        AddRef();
        *ppvObject = this;
        return S_OK;
    }
    *ppvObject = object;
    return result;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
    return ++m_references;
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
    ULONG references = --m_references;
    if (references == 0)
        delete this;
    return references;
    }
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override
    {
        synchronize();
        return m_device->TestCooperativeLevel();
    }
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override
    {
        return m_device->GetAvailableTextureMem();
    }
    HRESULT STDMETHODCALLTYPE EvictManagedResources() override
    {
        synchronize();
        return m_device->EvictManagedResources();
    }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** ppD3D9) override
    {
        return m_device->GetDirect3D(ppD3D9);
    }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* pCaps) override
    {
        return m_device->GetDeviceCaps(pCaps);
    }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode) override
    {
        return m_device->GetDisplayMode(iSwapChain, pMode);
    }
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters) override
    {
        return m_device->GetCreationParameters(pParameters);
    }
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) override
    {
        synchronize();
        return m_device->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap);
    }
    void STDMETHODCALLTYPE SetCursorPosition(int X, int Y, DWORD Flags) override
    {
        synchronize();
        m_device->SetCursorPosition(X, Y, Flags);
    }
    BOOL STDMETHODCALLTYPE ShowCursor(BOOL bShow) override
    {
        synchronize();
        return m_device->ShowCursor(bShow);
    }
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) override
    {
        synchronize();
        return m_device->CreateAdditionalSwapChain(pPresentationParameters, pSwapChain);
    }
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9** pSwapChain) override
    {
        synchronize();
        return m_device->GetSwapChain(iSwapChain, pSwapChain);
    }
    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override
    {
        synchronize();
        return m_device->GetNumberOfSwapChains();
    }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) override
    {
    synchronize();
    // A reset needs every reference to the back buffer released.
    releaseShadows();
    HRESULT result = m_device->Reset(pPresentationParameters);
    fetchShadows();
    return result;
    }
    HRESULT STDMETHODCALLTYPE Present(const RECT *src_rect, const RECT *dst_rect, HWND dst_window_override, const RGNDATA *dirty_region) override
    {
    // A frame presented here, unthreaded, may be followed by recorded ones; a recorded frame goes to the drawing
    // thread, which presents it while this thread goes on. Turned off, the frame presented last is waited for.
    if (!recording() || src_rect || dst_rect || dst_window_override || dirty_region) {
        synchronize();
        HRESULT result = m_device->Present(src_rect, dst_rect, dst_window_override, dirty_region);
        m_recording = m_threaded && std::this_thread::get_id() == m_owner;
        return result;
    }
    put(OpPresent);
    HRESULT result = submit();
    if (!m_threaded) {
        waitIdle();
        result = m_presentResult;
        m_recording = false;
    }
    return result;
    }
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) override
    {
        synchronize();
        return m_device->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer);
    }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus) override
    {
        synchronize();
        return m_device->GetRasterStatus(iSwapChain, pRasterStatus);
    }
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL bEnableDialogs) override
    {
        synchronize();
        return m_device->SetDialogBoxMode(bEnableDialogs);
    }
    void STDMETHODCALLTYPE SetGammaRamp(UINT swapchain_idx, DWORD flags, const D3DGAMMARAMP *ramp) override
    {
        synchronize();
        m_device->SetGammaRamp(swapchain_idx, flags, ramp);
    }
    void STDMETHODCALLTYPE GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp) override
    {
        synchronize();
        m_device->GetGammaRamp(iSwapChain, pRamp);
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) override
    {
        return m_device->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);
    }
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) override
    {
        return m_device->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle);
    }
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) override
    {
        return m_device->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle);
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) override
    {
    HRESULT result = m_device->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
    // Dynamic buffers are rewritten while drawing: their locks are recorded with the frame.
    if (result == D3D_OK && (Usage & D3DUSAGE_DYNAMIC))
        *ppVertexBuffer = new DeferredBuffer<IDirect3DVertexBuffer9, D3DVERTEXBUFFER_DESC>(this, *ppVertexBuffer, Length);
    return result;
    }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle) override
    {
    HRESULT result = m_device->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle);
    if (result == D3D_OK && (Usage & D3DUSAGE_DYNAMIC))
        *ppIndexBuffer = new DeferredBuffer<IDirect3DIndexBuffer9, D3DINDEXBUFFER_DESC>(this, *ppIndexBuffer, Length);
    return result;
    }
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override
    {
        return m_device->CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle);
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override
    {
        return m_device->CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);
    }
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9 *src_surface, const RECT *src_rect, IDirect3DSurface9 *dst_surface, const POINT *dst_point) override
    {
        synchronize();
        return m_device->UpdateSurface(src_surface, src_rect, dst_surface, dst_point);
    }
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) override
    {
        synchronize();
        return m_device->UpdateTexture(pSourceTexture, pDestinationTexture);
    }
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) override
    {
        synchronize();
        return m_device->GetRenderTargetData(pRenderTarget, pDestSurface);
    }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) override
    {
        synchronize();
        return m_device->GetFrontBufferData(iSwapChain, pDestSurface);
    }
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9 *src_surface, const RECT *src_rect, IDirect3DSurface9 *dst_surface, const RECT *dst_rect, D3DTEXTUREFILTERTYPE filter) override
    {
        synchronize();
        return m_device->StretchRect(src_surface, src_rect, dst_surface, dst_rect, filter);
    }
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9 *surface, const RECT *rect, D3DCOLOR color) override
    {
        synchronize();
        return m_device->ColorFill(surface, rect, color);
    }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override
    {
        return m_device->CreateOffscreenPlainSurface(Width, Height, Format, Pool, ppSurface, pSharedHandle);
    }
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) override
    {
    if (RenderTargetIndex < MaxRenderTargets)
        setShadow(m_renderTargets[RenderTargetIndex], pRenderTarget);
    if (!recording())
        return m_device->SetRenderTarget(RenderTargetIndex, pRenderTarget);
    Command *command = put(OpSetRenderTarget);
    command->a = RenderTargetIndex;
    command->object = keep(pRenderTarget);
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) override
    {
    if (!ppRenderTarget || RenderTargetIndex >= MaxRenderTargets)
        return D3DERR_INVALIDCALL;
    *ppRenderTarget = m_renderTargets[RenderTargetIndex];
    if (!*ppRenderTarget)
        return D3DERR_NOTFOUND;
    (*ppRenderTarget)->AddRef();
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil) override
    {
    setShadow(m_depthStencil, pNewZStencil);
    if (!recording())
        return m_device->SetDepthStencilSurface(pNewZStencil);
    put(OpSetDepthStencilSurface)->object = keep(pNewZStencil);
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) override
    {
    if (!ppZStencilSurface)
        return D3DERR_INVALIDCALL;
    *ppZStencilSurface = m_depthStencil;
    if (!m_depthStencil)
        return D3DERR_NOTFOUND;
    m_depthStencil->AddRef();
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE BeginScene() override
    {
    if (!recording())
        return m_device->BeginScene();
    Command *command = put(OpBeginScene);
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE EndScene() override
    {
    if (!recording())
        return m_device->EndScene();
    Command *command = put(OpEndScene);
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE Clear(DWORD rect_count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) override
    {
    if (!recording())
        return m_device->Clear(rect_count, rects, flags, color, z, stencil);
    Command *command = put(OpClear, rects ? rect_count * sizeof(D3DRECT) : 0);
    command->a = rects ? rect_count : 0;
    command->b = flags;
    command->c = color;
    command->d = stencil;
    command->e = z;
    command->flag = rects != 0;
    if (rects)
        memcpy(command + 1, rects, rect_count * sizeof(D3DRECT));
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) override
    {
    if (!recording())
        return m_device->SetTransform(state, matrix);
    Command *command = put(OpSetTransform, sizeof(D3DMATRIX));
    command->a = state;
    memcpy(command + 1, matrix, sizeof(D3DMATRIX));
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) override
    {
        synchronize();
        return m_device->GetTransform(State, pMatrix);
    }
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) override
    {
        synchronize();
        return m_device->MultiplyTransform(state, matrix);
    }
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9 *viewport) override
    {
    if (!recording())
        return m_device->SetViewport(viewport);
    Command *command = put(OpSetViewport, sizeof(D3DVIEWPORT9));
    memcpy(command + 1, viewport, sizeof(D3DVIEWPORT9));
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* pViewport) override
    {
        synchronize();
        return m_device->GetViewport(pViewport);
    }
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9 *material) override
    {
    if (!recording())
        return m_device->SetMaterial(material);
    Command *command = put(OpSetMaterial, sizeof(D3DMATERIAL9));
    memcpy(command + 1, material, sizeof(D3DMATERIAL9));
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* pMaterial) override
    {
        synchronize();
        return m_device->GetMaterial(pMaterial);
    }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD index, const D3DLIGHT9 *light) override
    {
    if (!recording())
        return m_device->SetLight(index, light);
    Command *command = put(OpSetLight, sizeof(D3DLIGHT9));
    command->a = index;
    memcpy(command + 1, light, sizeof(D3DLIGHT9));
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetLight(DWORD Index, D3DLIGHT9* a1) override
    {
        synchronize();
        return m_device->GetLight(Index, a1);
    }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD Index, BOOL Enable) override
    {
    if (!recording())
        return m_device->LightEnable(Index, Enable);
    Command *command = put(OpLightEnable);
    command->a = Index;
    command->b = Enable;
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD Index, BOOL* pEnable) override
    {
        synchronize();
        return m_device->GetLightEnable(Index, pEnable);
    }
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD index, const float *plane) override
    {
    if (!recording())
        return m_device->SetClipPlane(index, plane);
    Command *command = put(OpSetClipPlane, 4 * sizeof(float));
    command->a = index;
    memcpy(command + 1, plane, 4 * sizeof(float));
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD Index, float* pPlane) override
    {
        synchronize();
        return m_device->GetClipPlane(Index, pPlane);
    }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override
    {
    if (!recording())
        return m_device->SetRenderState(State, Value);
    Command *command = put(OpSetRenderState);
    command->a = State;
    command->b = Value;
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) override
    {
        synchronize();
        return m_device->GetRenderState(State, pValue);
    }
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB) override
    {
        return m_device->CreateStateBlock(Type, ppSB);
    }
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override
    {
        synchronize();
        return m_device->BeginStateBlock();
    }
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** ppSB) override
    {
        synchronize();
        return m_device->EndStateBlock(ppSB);
    }
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9 *clip_status) override
    {
        synchronize();
        return m_device->SetClipStatus(clip_status);
    }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9* pClipStatus) override
    {
        synchronize();
        return m_device->GetClipStatus(pClipStatus);
    }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD Stage, IDirect3DBaseTexture9** ppTexture) override
    {
        synchronize();
        return m_device->GetTexture(Stage, ppTexture);
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture) override
    {
    if (!recording())
        return m_device->SetTexture(Stage, pTexture);
    Command *command = put(OpSetTexture);
    command->a = Stage;
    command->object = keep(pTexture);
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) override
    {
        synchronize();
        return m_device->GetTextureStageState(Stage, Type, pValue);
    }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override
    {
    if (!recording())
        return m_device->SetTextureStageState(Stage, Type, Value);
    Command *command = put(OpSetTextureStageState);
    command->a = Stage;
    command->b = Type;
    command->c = Value;
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue) override
    {
        synchronize();
        return m_device->GetSamplerState(Sampler, Type, pValue);
    }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override
    {
    if (!recording())
        return m_device->SetSamplerState(Sampler, Type, Value);
    Command *command = put(OpSetSamplerState);
    command->a = Sampler;
    command->b = Type;
    command->c = Value;
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* pNumPasses) override
    {
        synchronize();
        return m_device->ValidateDevice(pNumPasses);
    }
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT palette_idx, const PALETTEENTRY *entries) override
    {
        synchronize();
        return m_device->SetPaletteEntries(palette_idx, entries);
    }
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY* pEntries) override
    {
        synchronize();
        return m_device->GetPaletteEntries(PaletteNumber, pEntries);
    }
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT PaletteNumber) override
    {
        synchronize();
        return m_device->SetCurrentTexturePalette(PaletteNumber);
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT *PaletteNumber) override
    {
        synchronize();
        return m_device->GetCurrentTexturePalette(PaletteNumber);
    }
    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT *rect) override
    {
    if (!recording())
        return m_device->SetScissorRect(rect);
    Command *command = put(OpSetScissorRect, sizeof(RECT));
    memcpy(command + 1, rect, sizeof(RECT));
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* pRect) override
    {
        synchronize();
        return m_device->GetScissorRect(pRect);
    }
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL bSoftware) override
    {
        synchronize();
        return m_device->SetSoftwareVertexProcessing(bSoftware);
    }
    BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override
    {
        synchronize();
        return m_device->GetSoftwareVertexProcessing();
    }
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float nSegments) override
    {
        synchronize();
        return m_device->SetNPatchMode(nSegments);
    }
    float STDMETHODCALLTYPE GetNPatchMode() override
    {
        synchronize();
        return m_device->GetNPatchMode();
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override
    {
    if (!recording())
        return m_device->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
    Command *command = put(OpDrawPrimitive);
    command->a = PrimitiveType;
    command->b = StartVertex;
    command->c = PrimitiveCount;
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE a0, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) override
    {
    if (!recording())
        return m_device->DrawIndexedPrimitive(a0, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
    Command *command = put(OpDrawIndexedPrimitive);
    command->a = a0;
    command->b = (DWORD)BaseVertexIndex;
    command->c = MinVertexIndex;
    command->d = NumVertices;
    command->f = startIndex;
    command->g = primCount;
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE primitive_type, UINT primitive_count, const void *data, UINT stride) override
    {
    if (!recording())
        return m_device->DrawPrimitiveUP(primitive_type, primitive_count, data, stride);
    size_t size = (size_t)vertexCount(primitive_type, primitive_count) * stride;
    Command *command = put(OpDrawPrimitiveUP, size);
    command->a = primitive_type;
    command->b = primitive_count;
    command->c = stride;
    memcpy(command + 1, data, size);
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitive_type, UINT min_vertex_idx, UINT vertex_count, UINT primitive_count, const void *index_data, D3DFORMAT index_format, const void *data, UINT stride) override
    {
    if (!recording())
        return m_device->DrawIndexedPrimitiveUP(primitive_type, min_vertex_idx, vertex_count, primitive_count, index_data,
                                                index_format, data, stride);
    // The vertices from the first one through the last one used; the indices, padded to 16 bytes.
    size_t indexSize = (size_t)vertexCount(primitive_type, primitive_count) * (index_format == D3DFMT_INDEX32 ? 4 : 2);
    size_t paddedIndexSize = (indexSize + 15) & ~(size_t)15;
    size_t vertexSize = (size_t)(min_vertex_idx + vertex_count) * stride;
    Command *command = put(OpDrawIndexedPrimitiveUP, paddedIndexSize + vertexSize);
    command->a = primitive_type;
    command->b = min_vertex_idx;
    command->c = vertex_count;
    command->d = primitive_count;
    command->f = index_format;
    command->g = stride;
    command->h = (DWORD)paddedIndexSize;
    memcpy(command + 1, index_data, indexSize);
    memcpy((char *)(command + 1) + paddedIndexSize, data, vertexSize);
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags) override
    {
        synchronize();
        return m_device->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDecl, Flags);
    }
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **declaration) override
    {
        return m_device->CreateVertexDeclaration(elements, declaration);
    }
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) override
    {
    if (!recording())
        return m_device->SetVertexDeclaration(pDecl);
    Command *command = put(OpSetVertexDeclaration);
    command->object = keep(pDecl);
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl) override
    {
        synchronize();
        return m_device->GetVertexDeclaration(ppDecl);
    }
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD FVF) override
    {
    if (!recording())
        return m_device->SetFVF(FVF);
    Command *command = put(OpSetFVF);
    command->a = FVF;
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD* pFVF) override
    {
        synchronize();
        return m_device->GetFVF(pFVF);
    }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD *byte_code, IDirect3DVertexShader9 **shader) override
    {
        return m_device->CreateVertexShader(byte_code, shader);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* pShader) override
    {
    if (!recording())
        return m_device->SetVertexShader(pShader);
    Command *command = put(OpSetVertexShader);
    command->object = keep(pShader);
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** ppShader) override
    {
        synchronize();
        return m_device->GetVertexShader(ppShader);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT reg_idx, const float *data, UINT count) override
    {
    if (!recording())
        return m_device->SetVertexShaderConstantF(reg_idx, data, count);
    Command *command = put(OpSetVertexShaderConstantF, count * 4 * sizeof(float));
    command->a = reg_idx;
    command->b = count;
    memcpy(command + 1, data, count * 4 * sizeof(float));
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) override
    {
        synchronize();
        return m_device->GetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT reg_idx, const int *data, UINT count) override
    {
    if (!recording())
        return m_device->SetVertexShaderConstantI(reg_idx, data, count);
    Command *command = put(OpSetVertexShaderConstantI, count * 4 * sizeof(int));
    command->a = reg_idx;
    command->b = count;
    memcpy(command + 1, data, count * 4 * sizeof(int));
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) override
    {
        synchronize();
        return m_device->GetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT reg_idx, const BOOL *data, UINT count) override
    {
    if (!recording())
        return m_device->SetVertexShaderConstantB(reg_idx, data, count);
    Command *command = put(OpSetVertexShaderConstantB, count * sizeof(BOOL));
    command->a = reg_idx;
    command->b = count;
    memcpy(command + 1, data, count * sizeof(BOOL));
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) override
    {
        synchronize();
        return m_device->GetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
    }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) override
    {
    IDirect3DVertexBuffer9 *buffer = DeferredBuffer<IDirect3DVertexBuffer9, D3DVERTEXBUFFER_DESC>::unwrap(pStreamData);
    if (!recording())
        return m_device->SetStreamSource(StreamNumber, buffer, OffsetInBytes, Stride);
    Command *command = put(OpSetStreamSource);
    command->a = StreamNumber;
    command->b = OffsetInBytes;
    command->c = Stride;
    command->object = keep(buffer);
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* OffsetInBytes, UINT* pStride) override
    {
        synchronize();
        return m_device->GetStreamSource(StreamNumber, ppStreamData, OffsetInBytes, pStride);
    }
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT StreamNumber, UINT Divider) override
    {
    if (!recording())
        return m_device->SetStreamSourceFreq(StreamNumber, Divider);
    Command *command = put(OpSetStreamSourceFreq);
    command->a = StreamNumber;
    command->b = Divider;
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT StreamNumber, UINT* Divider) override
    {
        synchronize();
        return m_device->GetStreamSourceFreq(StreamNumber, Divider);
    }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* pIndexData) override
    {
    IDirect3DIndexBuffer9 *buffer = DeferredBuffer<IDirect3DIndexBuffer9, D3DINDEXBUFFER_DESC>::unwrap(pIndexData);
    if (!recording())
        return m_device->SetIndices(buffer);
    put(OpSetIndices)->object = keep(buffer);
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** ppIndexData) override
    {
        synchronize();
        return m_device->GetIndices(ppIndexData);
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD *byte_code, IDirect3DPixelShader9 **shader) override
    {
        return m_device->CreatePixelShader(byte_code, shader);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* pShader) override
    {
    if (!recording())
        return m_device->SetPixelShader(pShader);
    Command *command = put(OpSetPixelShader);
    command->object = keep(pShader);
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** ppShader) override
    {
        synchronize();
        return m_device->GetPixelShader(ppShader);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT reg_idx, const float *data, UINT count) override
    {
    if (data && reg_idx + count <= MaxPixelConstants)
        memcpy(&m_pixelConstants[reg_idx * 4], data, count * 4 * sizeof(float));
    if (!recording())
        return m_device->SetPixelShaderConstantF(reg_idx, data, count);
    Command *command = put(OpSetPixelShaderConstantF, count * 4 * sizeof(float));
    command->a = reg_idx;
    command->b = count;
    memcpy(command + 1, data, count * 4 * sizeof(float));
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) override
    {
    if (!pConstantData || StartRegister + Vector4fCount > MaxPixelConstants)
        return D3DERR_INVALIDCALL;
    memcpy(pConstantData, &m_pixelConstants[StartRegister * 4], Vector4fCount * 4 * sizeof(float));
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT reg_idx, const int *data, UINT count) override
    {
    if (!recording())
        return m_device->SetPixelShaderConstantI(reg_idx, data, count);
    Command *command = put(OpSetPixelShaderConstantI, count * 4 * sizeof(int));
    command->a = reg_idx;
    command->b = count;
    memcpy(command + 1, data, count * 4 * sizeof(int));
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) override
    {
        synchronize();
        return m_device->GetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT reg_idx, const BOOL *data, UINT count) override
    {
    if (!recording())
        return m_device->SetPixelShaderConstantB(reg_idx, data, count);
    Command *command = put(OpSetPixelShaderConstantB, count * sizeof(BOOL));
    command->a = reg_idx;
    command->b = count;
    memcpy(command + 1, data, count * sizeof(BOOL));
    return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) override
    {
        synchronize();
        return m_device->GetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
    }
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT handle, const float *segment_count, const D3DRECTPATCH_INFO *patch_info) override
    {
        synchronize();
        return m_device->DrawRectPatch(handle, segment_count, patch_info);
    }
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT handle, const float *segment_count, const D3DTRIPATCH_INFO *patch_info) override
    {
        synchronize();
        return m_device->DrawTriPatch(handle, segment_count, patch_info);
    }
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT Handle) override
    {
        synchronize();
        return m_device->DeletePatch(Handle);
    }
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) override
    {
        return m_device->CreateQuery(Type, ppQuery);
    }

private:
    struct alignas(16) Block {
        unsigned char bytes[16];
    };
    struct Frame {
        std::vector<Block> commands;
        size_t used = 0; // blocks
        std::vector<IUnknown *> objects;
    };

    ~DeferredDevice()
    {
        synchronize();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_quit = true;
        }
        m_wake.notify_all();
        m_thread.join();
        releaseShadows();
        m_device->Release();
    }

    // Appends a call with room for size bytes of data after it.
    Command *put(unsigned op, size_t size = 0)
    {
        Frame &frame = m_frames[m_current];
        size_t blocks = (sizeof(Command) + size + sizeof(Block) - 1) / sizeof(Block);
        if (frame.used + blocks > frame.commands.size())
            frame.commands.resize(std::max(frame.commands.size() * 2, frame.used + blocks + 4096));
        Command *command = reinterpret_cast<Command *>(&frame.commands[frame.used]);
        frame.used += blocks;
        command->op = op;
        command->size = (unsigned)blocks;
        return command;
    }
    Command *put(Op op, size_t size = 0) { return put((unsigned)op, size); }

    // Keeps an object a call uses until the call ran.
    template <class Interface>
    Interface *keep(Interface *object)
    {
        if (object) {
            object->AddRef();
            m_frames[m_current].objects.push_back(object);
        }
        return object;
    }

    void setShadow(IDirect3DSurface9 *&slot, IDirect3DSurface9 *surface)
    {
        if (surface)
            surface->AddRef();
        if (slot)
            slot->Release();
        slot = surface;
    }
    // The render targets and depth surface the device has (the ones set last).
    void fetchShadows()
    {
        for (int i = 0; i < MaxRenderTargets; i++)
            if (m_device->GetRenderTarget(i, &m_renderTargets[i]) != D3D_OK)
                m_renderTargets[i] = 0;
        if (m_device->GetDepthStencilSurface(&m_depthStencil) != D3D_OK)
            m_depthStencil = 0;
    }
    void releaseShadows()
    {
        for (int i = 0; i < MaxRenderTargets; i++)
            setShadow(m_renderTargets[i], 0);
        setShadow(m_depthStencil, 0);
    }

    // Waits until the drawing thread ran the frame it was given.
    void waitIdle()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_idle.wait(lock, [this]() { return !m_pending; });
    }
    // Hands the recorded frame to the drawing thread and records the next one into the other buffer. Returns
    // what presenting the frame before gave.
    HRESULT submit()
    {
        HRESULT result;
        waitIdle();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            result = m_presentResult;
            m_pending = &m_frames[m_current];
        }
        m_wake.notify_all();
        m_current ^= 1;
        return result;
    }
    // Before a call that is not recorded: the calls recorded so far run first, here.
    void synchronize()
    {
        if (!recording())
            return;
        waitIdle();
        replay(m_frames[m_current]);
    }

    void run()
    {
        for (;;) {
            Frame *frame;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_wake.wait(lock, [this]() { return m_pending || m_quit; });
                if (!m_pending)
                    return;
                frame = m_pending;
            }
            replay(*frame);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_pending = 0;
            }
            m_idle.notify_all();
        }
    }

    // Makes the recorded calls, in order, and lets go of their objects.
    void replay(Frame &frame)
    {
        IDirect3DDevice9 *device = m_device;
        for (size_t at = 0; at < frame.used;) {
            Command *command = reinterpret_cast<Command *>(&frame.commands[at]);
            at += command->size;
            const void *data = command + 1;
            switch (command->op) {
            case OpSetRenderState:
                device->SetRenderState((D3DRENDERSTATETYPE)command->a, command->b);
                break;
            case OpSetSamplerState:
                device->SetSamplerState(command->a, (D3DSAMPLERSTATETYPE)command->b, command->c);
                break;
            case OpSetTextureStageState:
                device->SetTextureStageState(command->a, (D3DTEXTURESTAGESTATETYPE)command->b, command->c);
                break;
            case OpSetTexture:
                device->SetTexture(command->a, static_cast<IDirect3DBaseTexture9 *>(command->object));
                break;
            case OpSetTransform:
                device->SetTransform((D3DTRANSFORMSTATETYPE)command->a, static_cast<const D3DMATRIX *>(data));
                break;
            case OpSetVertexShaderConstantF:
                device->SetVertexShaderConstantF(command->a, static_cast<const float *>(data), command->b);
                break;
            case OpSetVertexShaderConstantI:
                device->SetVertexShaderConstantI(command->a, static_cast<const int *>(data), command->b);
                break;
            case OpSetVertexShaderConstantB:
                device->SetVertexShaderConstantB(command->a, static_cast<const BOOL *>(data), command->b);
                break;
            case OpSetPixelShaderConstantF:
                device->SetPixelShaderConstantF(command->a, static_cast<const float *>(data), command->b);
                break;
            case OpSetPixelShaderConstantI:
                device->SetPixelShaderConstantI(command->a, static_cast<const int *>(data), command->b);
                break;
            case OpSetPixelShaderConstantB:
                device->SetPixelShaderConstantB(command->a, static_cast<const BOOL *>(data), command->b);
                break;
            case OpSetVertexShader:
                device->SetVertexShader(static_cast<IDirect3DVertexShader9 *>(command->object));
                break;
            case OpSetPixelShader:
                device->SetPixelShader(static_cast<IDirect3DPixelShader9 *>(command->object));
                break;
            case OpSetVertexDeclaration:
                device->SetVertexDeclaration(static_cast<IDirect3DVertexDeclaration9 *>(command->object));
                break;
            case OpSetFVF:
                device->SetFVF(command->a);
                break;
            case OpSetStreamSource:
                device->SetStreamSource(command->a, static_cast<IDirect3DVertexBuffer9 *>(command->object), command->b,
                                        command->c);
                break;
            case OpSetStreamSourceFreq:
                device->SetStreamSourceFreq(command->a, command->b);
                break;
            case OpSetIndices:
                device->SetIndices(static_cast<IDirect3DIndexBuffer9 *>(command->object));
                break;
            case OpSetViewport:
                device->SetViewport(static_cast<const D3DVIEWPORT9 *>(data));
                break;
            case OpSetScissorRect:
                device->SetScissorRect(static_cast<const RECT *>(data));
                break;
            case OpSetMaterial:
                device->SetMaterial(static_cast<const D3DMATERIAL9 *>(data));
                break;
            case OpSetLight:
                device->SetLight(command->a, static_cast<const D3DLIGHT9 *>(data));
                break;
            case OpLightEnable:
                device->LightEnable(command->a, (BOOL)command->b);
                break;
            case OpSetClipPlane:
                device->SetClipPlane(command->a, static_cast<const float *>(data));
                break;
            case OpSetRenderTarget:
                device->SetRenderTarget(command->a, static_cast<IDirect3DSurface9 *>(command->object));
                break;
            case OpSetDepthStencilSurface:
                device->SetDepthStencilSurface(static_cast<IDirect3DSurface9 *>(command->object));
                break;
            case OpClear:
                device->Clear(command->a, command->flag ? static_cast<const D3DRECT *>(data) : 0, command->b,
                              command->c, command->e, command->d);
                break;
            case OpBeginScene:
                device->BeginScene();
                break;
            case OpEndScene:
                device->EndScene();
                break;
            case OpDrawPrimitive:
                device->DrawPrimitive((D3DPRIMITIVETYPE)command->a, command->b, command->c);
                break;
            case OpDrawIndexedPrimitive:
                device->DrawIndexedPrimitive((D3DPRIMITIVETYPE)command->a, (INT)command->b, command->c, command->d,
                                             command->f, command->g);
                break;
            case OpDrawPrimitiveUP:
                device->DrawPrimitiveUP((D3DPRIMITIVETYPE)command->a, command->b, data, command->c);
                break;
            case OpDrawIndexedPrimitiveUP:
                device->DrawIndexedPrimitiveUP((D3DPRIMITIVETYPE)command->a, command->b, command->c, command->d, data,
                                               (D3DFORMAT)command->f, static_cast<const char *>(data) + command->h,
                                               command->g);
                break;
            case OpUploadVertexBuffer: {
                void *bytes;
                IDirect3DVertexBuffer9 *buffer = static_cast<IDirect3DVertexBuffer9 *>(command->object);
                if (buffer->Lock(command->a, command->b, &bytes, command->c) == D3D_OK) {
                    memcpy(bytes, data, command->b);
                    buffer->Unlock();
                }
                break;
            }
            case OpUploadIndexBuffer: {
                void *bytes;
                IDirect3DIndexBuffer9 *buffer = static_cast<IDirect3DIndexBuffer9 *>(command->object);
                if (buffer->Lock(command->a, command->b, &bytes, command->c) == D3D_OK) {
                    memcpy(bytes, data, command->b);
                    buffer->Unlock();
                }
                break;
            }
            case OpPresent:
                m_presentResult = device->Present(0, 0, 0, 0);
                break;
            }
        }
        frame.used = 0;
        for (std::vector<IUnknown *>::iterator it = frame.objects.begin(); it != frame.objects.end(); ++it)
            (*it)->Release();
        frame.objects.clear();
    }

    IDirect3DDevice9 *m_device;
    std::atomic<ULONG> m_references;
    // Threaded rendering as asked for, and as it is for the frame being recorded (it changes at Present).
    std::atomic<bool> m_threaded;
    std::atomic<bool> m_recording;
    std::thread::id m_owner; // the thread that draws: only its calls are recorded
    IDirect3DSurface9 *m_renderTargets[MaxRenderTargets];
    IDirect3DSurface9 *m_depthStencil;
    float m_pixelConstants[MaxPixelConstants * 4];
    Frame m_frames[2];
    int m_current; // the frame being recorded
    std::mutex m_mutex;
    std::condition_variable m_wake, m_idle;
    Frame *m_pending; // the frame the drawing thread was given and has not finished
    bool m_quit;
    HRESULT m_presentResult; // of the last frame the drawing thread presented
    std::thread m_thread;
};

template <class Interface, class Description>
DeferredBuffer<Interface, Description>::DeferredBuffer(DeferredDevice *device, Interface *buffer, UINT length)
    : m_device(device),
      m_buffer(buffer),
      m_length(length),
      m_references(1),
      m_recordedLock(false),
      m_lockOffset(0),
      m_lockSize(0),
      m_lockFlags(0)
{
    s_vtable = *reinterpret_cast<void **>(this);
}

template <class Interface, class Description>
HRESULT DeferredBuffer<Interface, Description>::GetDevice(IDirect3DDevice9 **ppDevice)
{
    if (!ppDevice)
        return D3DERR_INVALIDCALL;
    m_device->AddRef();
    *ppDevice = m_device;
    return D3D_OK;
}

template <class Interface, class Description>
HRESULT DeferredBuffer<Interface, Description>::Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags)
{
    if (!m_device->recording()) {
        m_recordedLock = false;
        return m_buffer->Lock(OffsetToLock, SizeToLock, ppbData, Flags);
    }
    if (!ppbData || OffsetToLock > m_length)
        return D3DERR_INVALIDCALL;
    m_recordedLock = true;
    m_lockOffset = OffsetToLock;
    m_lockSize = SizeToLock ? SizeToLock : m_length - OffsetToLock;
    m_lockFlags = Flags;
    if (m_lockData.size() < m_lockSize)
        m_lockData.resize(m_lockSize);
    *ppbData = m_lockData.data();
    return D3D_OK;
}

template <class Interface, class Description>
HRESULT DeferredBuffer<Interface, Description>::Unlock()
{
    if (!m_recordedLock)
        return m_buffer->Unlock();
    m_recordedLock = false;
    m_device->upload(m_buffer, m_lockOffset, m_lockSize, m_lockFlags, m_lockData.data());
    return D3D_OK;
}

DeferredDevice *s_deferredDevice; // the device made last

} // namespace

IDirect3DDevice9 *createDeferredDevice(IDirect3DDevice9 *device)
{
    s_deferredDevice = new DeferredDevice(device);
    return s_deferredDevice;
}

void setThreadedRendering(IDirect3DDevice9 *device, bool threaded)
{
    if (device && device == static_cast<IDirect3DDevice9 *>(s_deferredDevice))
        s_deferredDevice->setThreaded(threaded);
}

} // namespace cube

// WinMain opens the window, Direct3D 9, XAudio2 and DirectInput 8, creates the game and runs the message loop. each frame copies input, updates and renders the game, and waits for the configured minimum frame time.
#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <direct.h>
#include <d3d9.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <string>
#include <vector>
#include <algorithm>

#include "plasma/D3D9Engine.h"
#include "plasma/FontEngine.h"
#include "plasma/Vector.h"
#include "cube/CubeShader.h"
#include "cube/GameController.h"
#include "cube/DefaultBlocks.h"
#include "cube/Options.h"
#include "cube/XAudio2Engine.h"

using namespace cube;

// The device's settings (anonymous: this file's own): an anti-aliasing option step doubles the
// samples (2x, 4x, 8x); a failed reset is retried every 100 ms.
namespace {
enum {
    DeviceResetRetryDelay = 100
};
}

IDirect3DDevice9 *g_device;

IDirect3D9 *g_direct3d;

CubeShader *g_shader;

HWND g_window;

HINSTANCE g_instance;

GameController *g_game;

plasma::D3D9Engine *g_engine;

bool g_cursorShown; // the cursor was free in the last frame
bool g_deviceLost;  // Present reported D3DERR_DEVICELOST.
// The global options hold the display settings in use.
Options options;

bool g_readInput = true; // never cleared

void resetDevice(bool unused);

LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_SIZE:
    case WM_SETFOCUS:
        resetDevice(false);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        break;
    case WM_CHAR:
        g_engine->characterEntered((int)wParam);
        g_game->onKeyTyped((char)wParam);
        return 0;
    case WM_KEYUP:
        g_engine->keyReleased((unsigned short)wParam);
        g_game->handleKeyUp((unsigned char)wParam);
        return 0;
    case WM_KEYDOWN:
        g_engine->keyPressed((unsigned short)wParam);
        g_game->handleKeyDown((unsigned char)wParam);
        return 0;
    case WM_LBUTTONDOWN:
        g_game->handleButtonDown(plasma::MOUSE_BUTTON_LEFT);
        break;
    case WM_LBUTTONUP:
        g_game->handleButtonUp(plasma::MOUSE_BUTTON_LEFT);
        break;
    case WM_RBUTTONDOWN:
        g_game->handleButtonDown(plasma::MOUSE_BUTTON_RIGHT);
        break;
    case WM_RBUTTONUP:
        g_game->handleButtonUp(plasma::MOUSE_BUTTON_RIGHT);
        break;
    case WM_MBUTTONDOWN:
        g_game->handleButtonDown(plasma::MOUSE_BUTTON_MIDDLE);
        break;
    case WM_MBUTTONUP:
        g_game->handleButtonUp(plasma::MOUSE_BUTTON_MIDDLE);
        break;
    case WM_MOUSEWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta < 0)
            delta = -1;
        else if (delta > 0)
            delta = 1;
        g_game->handleMouseWheel(delta);
        break;
    }
    case WM_MOUSEMOVE:
    case WM_KILLFOCUS:
        break;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return 1;
}

// One frame: updates the game with the elapsed time, applies changed display options, renders and
// presents.
void frame()
{
    if (!g_device || !g_game)
        return;
    static DWORD s_lastTime; // Previous frame time.
    DWORD now = timeGetTime();
    if (!s_lastTime)
        s_lastTime = now;
    g_game->update(now - s_lastTime);
    if (g_game->m_options.resolutionX != options.resolutionX || g_game->m_options.resolutionY != options.resolutionY ||
        g_game->m_options.fullscreen != options.fullscreen || g_game->m_options.antiAliasing != options.antiAliasing) {
        options = g_game->m_options;
        resetDevice(false);
    }
    s_lastTime = now;
    g_game->render();
    if (g_device->Present(0, 0, 0, 0) == D3DERR_DEVICELOST)
        g_deviceLost = true;
}

// Creates the device for the window, the engine (with the system font directory) and the shader.
bool initializeDirect3D()
{
    g_direct3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_direct3d)
        return false;
    D3DPRESENT_PARAMETERS parameters;
    ZeroMemory(&parameters, sizeof(parameters));
    parameters.MultiSampleType = D3DMULTISAMPLE_NONE;
    parameters.MultiSampleQuality = 0;
    parameters.BackBufferWidth = GetSystemMetrics(SM_CXSCREEN);
    parameters.BackBufferHeight = GetSystemMetrics(SM_CYSCREEN);
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    parameters.EnableAutoDepthStencil = TRUE;
    parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
    if (g_direct3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_window,
                                 D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &parameters,
                                 &g_device) != D3D_OK) {
        g_direct3d->Release();
        g_direct3d = 0;
        return false;
    }
    g_engine = new plasma::D3D9Engine(g_device);
    g_engine->getFontEngine()->addFontPath(L"c:\\windows\\fonts");
    if (!g_engine->setupShadersAndFrameBuffer()) {
        g_device->Release();
        g_device = 0;
        g_direct3d->Release();
        g_direct3d = 0;
        return false;
    }
    g_shader = new CubeShader(g_device);
    if (!g_shader->initialize()) {
        delete g_shader;
        g_device->Release();
        g_device = 0;
        g_direct3d->Release();
        g_direct3d = 0;
        return false;
    }
    RECT client;
    GetClientRect(g_window, &client);
    g_engine->setViewport(0, 0, client.right, client.bottom, true);
    return true;
}

// Resets the device to the window's client size (or the options' resolution in fullscreen) and the
// options' antialiasing, under the game's update locks.
void resetDevice(bool unused)
{
    bool fullscreen = options.fullscreen != 0;
    RECT client;
    if (fullscreen) {
        client.right = options.resolutionX;
        client.bottom = options.resolutionY;
    } else {
        GetClientRect(g_window, &client);
    }
    if (client.right <= 0 || client.bottom <= 0)
        return;
    if (g_device) {
        if (g_engine) {
            g_engine->releaseDeviceResources();
            g_engine->setViewport(0, 0, client.right, client.bottom, true);
        }
        D3DPRESENT_PARAMETERS parameters;
        ZeroMemory(&parameters, sizeof(parameters));
        // 2^step samples, halved until the adapter supports them for the back and depth buffers
        // (an unsupported count would make the reset below fail forever).
        int samples = options.antiAliasing > 0 ? 1 << options.antiAliasing : 0;
        while (samples >= 2 &&
               (g_direct3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, !fullscreen,
                                                       (D3DMULTISAMPLE_TYPE)samples, 0) != D3D_OK ||
                g_direct3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_D24S8, !fullscreen,
                                                       (D3DMULTISAMPLE_TYPE)samples, 0) != D3D_OK))
            samples /= 2;
        parameters.MultiSampleType = samples >= 2 ? (D3DMULTISAMPLE_TYPE)samples : D3DMULTISAMPLE_NONE;
        parameters.MultiSampleQuality = 0;
        parameters.BackBufferFormat = D3DFMT_A8R8G8B8;
        parameters.BackBufferWidth = client.right;
        parameters.BackBufferHeight = client.bottom;
        parameters.Windowed = !fullscreen;
        parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        parameters.EnableAutoDepthStencil = TRUE;
        parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
        if (g_game)
            g_game->beginUpdate();
        while (g_device->Reset(&parameters) != D3D_OK)
            Sleep(DeviceResetRetryDelay);
        if (g_game) {
            g_game->endUpdate();
            g_game->setWindowSize(client.right, client.bottom);
        }
        if (g_engine)
            g_engine->restoreDeviceResources();
    }
    g_deviceLost = false;
}

// Opens the window and the devices, creates the game and runs the message loop until WM_QUIT.
// Command line: "server" hosts, any other first argument is a server address to join.
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    g_instance = instance;
    WSADATA winsock;
    WSAStartup(MAKEWORD(2, 2), &winsock);

    WNDCLASSW windowClass;
    windowClass.style = 0;
    windowClass.lpfnWndProc = WndProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = g_instance;
    windowClass.hIcon = LoadIconW(g_instance, L"Cube");
    windowClass.hCursor = 0;
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    windowClass.lpszMenuName = L"Cube";
    windowClass.lpszClassName = L"Cube";
    if (!RegisterClassW(&windowClass))
        return 0;
    // The window starts at 200, 200, 800 x 600 pixels.
    g_window = CreateWindowExW(0, L"Cube", L"Cube", WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 200, 200,
                               800, 600, 0, 0, g_instance, 0);
    if (!g_window)
        return 0;
    if (!initializeDirect3D()) {
        MessageBoxA(g_window,
                    "Could not initialize Direct3D. Please make sure the latest DirectX End-User Runtime is installed: "
                    "http://www.microsoft.com/en-us/download/details.aspx?id=35",
                    "Cube World", MB_OK);
        return 0;
    }

    // The distinct display resolutions, for the options screen.
    UINT modeCount = g_direct3d->GetAdapterModeCount(D3DADAPTER_DEFAULT, D3DFMT_X8R8G8B8);
    std::vector<plasma::Vector<2, int>> resolutions;
    for (UINT i = 0; i < modeCount; i++) {
        D3DDISPLAYMODE mode;
        g_direct3d->EnumAdapterModes(D3DADAPTER_DEFAULT, D3DFMT_X8R8G8B8, i, &mode);
        bool known = false;
        for (std::vector<plasma::Vector<2, int>>::iterator it = resolutions.begin(); it != resolutions.end(); ++it) {
            if (*it == plasma::Vector<2, int>(mode.Width, mode.Height)) {
                known = true;
                break;
            }
        }
        if (!known)
            resolutions.push_back(plasma::Vector<2, int>(mode.Width, mode.Height));
    }
    options.resolutionX = GetSystemMetrics(SM_CXSCREEN);
    options.resolutionY = GetSystemMetrics(SM_CYSCREEN);
    options.load();

    XAudio2Engine audio;
    if (!audio.initialize()) {
        MessageBoxA(g_window,
                    "Could not initialize XAudio2. Please make sure the latest DirectX End-User Runtime is installed: "
                    "http://www.microsoft.com/en-us/download/details.aspx?id=35",
                    "Cube World", MB_OK);
        return 0;
    }
    audio.setMusicVolume(options.musicVolume * 0.01f, 0);

    IDirectInput8W *input;
    if (DirectInput8Create(instance, DIRECTINPUT_VERSION, IID_IDirectInput8W, (void **)&input, 0) != DI_OK) {
        MessageBoxA(g_window,
                    "Could not initialize DirectInput8. Please make sure the latest DirectX End-User Runtime is "
                    "installed: http://www.microsoft.com/en-us/download/details.aspx?id=35",
                    "Cube World", MB_OK);
        return 0;
    }
    IDirectInputDevice8W *keyboard;
    input->CreateDevice(GUID_SysKeyboard, &keyboard, 0);
    keyboard->SetDataFormat(&c_dfDIKeyboard);
    keyboard->Acquire();
    IDirectInputDevice8W *mouse;
    input->CreateDevice(GUID_SysMouse, &mouse, 0);
    mouse->SetDataFormat(&c_dfDIMouse);
    mouse->SetCooperativeLevel(g_window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    mouse->Acquire();

    int argumentCount = 0;
    bool server = false;
    std::string serverAddress;
    LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments) {
        if (argumentCount >= 2) {
            if (std::wstring(arguments[1]) == L"server")
                server = true;
            else {
                std::wstring address(arguments[1]);
                serverAddress = narrowString(address);
            }
        }
        LocalFree(arguments);
    }
    _mkdir("Save");

    g_game = new GameController(g_device, g_shader, g_engine, &audio, &options, &resolutions, server, serverAddress);
    ShowWindow(g_window, SW_MAXIMIZE);
    SetCursor(0);
    ShowCursor(FALSE);
    UpdateWindow(g_window);
    SetFocus(g_window);
    timeBeginPeriod(1);

    MSG message;
    for (;;) {
        DWORD frameStart = timeGetTime();
        if (PeekMessageW(&message, 0, 0, 0, PM_NOREMOVE) == TRUE) {
            if (!GetMessageW(&message, 0, 0, 0))
                break;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (g_game->m_quit)
            PostQuitMessage(0);
        if (g_readInput) {
            // A free cursor appears where the engine's pointer is; otherwise it is kept in the middle of the
            // focused window.
            RECT client;
            GetClientRect(g_window, &client);
            POINT cursor;
            GetCursorPos(&cursor);
            ScreenToClient(g_window, &cursor);
            if (g_game->isCursorVisible()) {
                if (!g_cursorShown) {
                    POINT pointer;
                    pointer.x = (int)g_engine->getPointer()[0];
                    pointer.y = (int)g_engine->getPointer()[1];
                    ClientToScreen(g_window, &pointer);
                    SetCursorPos(pointer.x, pointer.y);
                }
                g_cursorShown = true;
            } else if (GetFocus() == g_window) {
                cursor.x = client.right / 2;
                cursor.y = client.bottom / 2;
                g_cursorShown = false;
                ClientToScreen(g_window, &cursor);
                SetCursorPos(cursor.x, cursor.y);
            }
        }
        if (g_readInput && GetFocus() == g_window) {
            unsigned char keys[256];
            keyboard->GetDeviceState(sizeof(keys), keys);
            g_game->m_inputs[InputKeyW] = keys[DIK_W] != 0;
            g_game->m_inputs[InputKeyS] = keys[DIK_S] != 0;
            g_game->m_inputs[InputKeyA] = keys[DIK_A] != 0;
            g_game->m_inputs[InputKeyD] = keys[DIK_D] != 0;
            g_game->m_inputs[InputKeyE] = keys[DIK_E] != 0;
            g_game->m_inputs[InputKeyR] = keys[DIK_R] != 0;
            g_game->m_inputs[InputKeyT] = keys[DIK_T] != 0;
            g_game->m_inputs[InputKeySpace] = keys[DIK_SPACE] != 0;
            g_game->m_inputs[InputKeyShift] = keys[DIK_LSHIFT] != 0;
            g_game->m_inputs[InputKeyControl] = keys[DIK_LCONTROL] != 0;
            g_game->m_inputs[InputKeyQ] = keys[DIK_Q] != 0;
            DIMOUSESTATE mouseState;
            mouse->GetDeviceState(sizeof(mouseState), &mouseState);
            if (g_game->isCursorVisible()) {
                POINT cursor;
                GetCursorPos(&cursor);
                if (!options.fullscreen)
                    ScreenToClient(g_window, &cursor);
                g_game->handleMouseMove((float)cursor.x, (float)cursor.y);
            } else {
                g_game->handleMouseMove((float)mouseState.lX, (float)mouseState.lY);
            }
            g_game->m_inputs[InputMouseLeft] = mouseState.rgbButtons[plasma::MOUSE_BUTTON_LEFT] != 0;
            g_game->m_inputs[InputKeyShift2] = keys[DIK_LSHIFT] != 0;
            g_game->m_inputs[InputMouseMiddle] = mouseState.rgbButtons[plasma::MOUSE_BUTTON_MIDDLE] != 0;
            g_game->m_inputs[InputMouseRight] = mouseState.rgbButtons[plasma::MOUSE_BUTTON_RIGHT] != 0;
            g_game->m_inputs[InputKey1] = keys[DIK_1] != 0;
            g_game->m_inputs[InputKey2] = keys[DIK_2] != 0;
            g_game->m_inputs[InputKey3] = keys[DIK_3] != 0;
            g_game->m_inputs[InputKey4] = keys[DIK_4] != 0;
            g_game->m_moveStick = plasma::Vector<2, int>(0, 0);
            g_game->m_lookStick = plasma::Vector<2, int>(0, 0);
        }
        frame();
        DWORD elapsed = timeGetTime() - frameStart;
        if ((int)elapsed < g_game->m_options.minTimeStep)
            Sleep(g_game->m_options.minTimeStep - elapsed);
    }
    timeEndPeriod(1);

    delete g_game;
    delete g_engine;
    keyboard->Release();
    mouse->Release();
    input->Release();
    delete g_shader;
    if (g_device)
        g_device->Release();
    if (g_direct3d)
        g_direct3d->Release();
    WSACleanup();
    return 0;
}

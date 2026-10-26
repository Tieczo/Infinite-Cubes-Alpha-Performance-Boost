// The native Linux entry point (src/main.cpp's WinMain on Windows): SDL3 opens the window and reads
// keyboard and mouse, DXVK-native provides Direct3D 9 on Vulkan for that window and FAudio plays the
// XAudio2 sound. The frame loop, the device setup and the input the game receives are WinMain's: key
// events carry Windows virtual-key codes, the held keys and buttons are polled each frame as
// DirectInput did, and while the game hides the cursor the mouse reports relative motion.
#include <SDL3/SDL.h>
#include <windows.h>
#include <winsock2.h>
#include <d3d9.h>
#include <direct.h>
#include <unistd.h>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "plasma/D3D9Engine.h"
#include "plasma/FontEngine.h"
#include "plasma/StringConversion.h"
#include "plasma/Vector.h"
#include "cube/CubeShader.h"
#include "cube/GameController.h"
#include "cube/DefaultBlocks.h"
#include "cube/Options.h"
#include "cube/XAudio2Engine.h"

using namespace cube;

// A failed reset is retried every 100 ms.
namespace {
enum {
    DeviceResetRetryDelay = 100
};
}

IDirect3DDevice9 *g_device;

IDirect3D9 *g_direct3d;

CubeShader *g_shader;

SDL_Window *g_window;

GameController *g_game;

plasma::D3D9Engine *g_engine;

bool g_cursorShown; // the cursor was free in the last frame
bool g_deviceLost;  // Present reported D3DERR_DEVICELOST.
// The global options hold the display settings in use.
Options options;

bool g_readInput = true; // never cleared

// The back buffer's size; the mouse position is scaled from window points to it.
int g_backBufferWidth;
int g_backBufferHeight;

// Relative mouse motion since the last frame, while the game hides the cursor.
float g_mouseDeltaX;
float g_mouseDeltaY;
float g_wheel; // scrolled distance not yet passed on as whole steps

void resetDevice(bool unused);

static void showError(const char *message)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cube World", message, g_window);
}

// The window's drawable size in pixels (GetClientRect).
static void clientSize(int &width, int &height)
{
    width = 0;
    height = 0;
    SDL_GetWindowSizeInPixels(g_window, &width, &height);
}

static bool hasFocus()
{
    return (SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

// The Windows virtual-key code of an SDL key (WM_KEYDOWN's wParam), or 0. Letters and digits follow
// the keyboard layout, as virtual keys do.
static unsigned char virtualKey(const SDL_KeyboardEvent &event)
{
    SDL_Keycode key = event.key;
    if (key >= SDLK_A && key <= SDLK_Z)
        return (unsigned char)('A' + (key - SDLK_A));
    if (key >= SDLK_0 && key <= SDLK_9)
        return (unsigned char)('0' + (key - SDLK_0));
    if (key >= SDLK_F1 && key <= SDLK_F12)
        return (unsigned char)(VK_F1 + (key - SDLK_F1));
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9)
        return (unsigned char)(VK_NUMPAD0 + 1 + (key - SDLK_KP_1));
    switch (key) {
    case SDLK_BACKSPACE: return VK_BACK;
    case SDLK_TAB: return VK_TAB;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return VK_RETURN;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: return VK_SHIFT;
    case SDLK_LCTRL:
    case SDLK_RCTRL: return VK_CONTROL;
    case SDLK_PAUSE: return VK_PAUSE;
    case SDLK_CAPSLOCK: return VK_CAPITAL;
    case SDLK_ESCAPE: return VK_ESCAPE;
    case SDLK_SPACE: return VK_SPACE;
    case SDLK_PAGEUP: return VK_PRIOR;
    case SDLK_PAGEDOWN: return VK_NEXT;
    case SDLK_END: return VK_END;
    case SDLK_HOME: return VK_HOME;
    case SDLK_LEFT: return VK_LEFT;
    case SDLK_UP: return VK_UP;
    case SDLK_RIGHT: return VK_RIGHT;
    case SDLK_DOWN: return VK_DOWN;
    case SDLK_INSERT: return VK_INSERT;
    case SDLK_DELETE: return VK_DELETE;
    case SDLK_KP_0: return VK_NUMPAD0;
    case SDLK_KP_MULTIPLY: return VK_MULTIPLY;
    case SDLK_KP_PLUS: return VK_ADD;
    case SDLK_KP_MINUS: return VK_SUBTRACT;
    case SDLK_KP_PERIOD: return VK_DECIMAL;
    case SDLK_KP_DIVIDE: return VK_DIVIDE;
    case SDLK_SEMICOLON: return VK_OEM_1;
    case SDLK_PLUS:
    case SDLK_EQUALS: return VK_OEM_PLUS;
    case SDLK_COMMA: return VK_OEM_COMMA;
    case SDLK_MINUS: return VK_OEM_MINUS;
    case SDLK_PERIOD: return VK_OEM_PERIOD;
    case SDLK_SLASH: return VK_OEM_2;
    case SDLK_GRAVE: return VK_OEM_3;
    case SDLK_LEFTBRACKET: return VK_OEM_4;
    case SDLK_BACKSLASH: return VK_OEM_5;
    case SDLK_RIGHTBRACKET: return VK_OEM_6;
    case SDLK_APOSTROPHE: return VK_OEM_7;
    }
    // The key left of 1 and the extra key next to the left shift, by position (VK_OEM_3, VK_OEM_102).
    if (event.scancode == SDL_SCANCODE_GRAVE)
        return VK_OEM_3;
    if (event.scancode == SDL_SCANCODE_NONUSBACKSLASH)
        return VK_OEM_102;
    return 0;
}

// The mouse position in back-buffer pixels (window points scaled to the back buffer).
static void cursorPosition(float &x, float &y)
{
    SDL_GetMouseState(&x, &y);
    int width = 0, height = 0;
    SDL_GetWindowSize(g_window, &width, &height);
    if (width > 0 && height > 0 && g_backBufferWidth > 0 && g_backBufferHeight > 0) {
        x *= (float)g_backBufferWidth / width;
        y *= (float)g_backBufferHeight / height;
    }
}

// The window procedure's part: device resets on size and focus changes, typed characters, keys,
// buttons and the wheel. Returns false when the window is closed.
static bool handleEvent(const SDL_Event &event)
{
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return false;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        resetDevice(false);
        break;
    case SDL_EVENT_TEXT_INPUT: {
        // One WM_CHAR per character: the engine gets the code point, the chat its low byte.
        const char *text = event.text.text;
        while (text && *text) {
            Uint32 character = SDL_StepUTF8(&text, nullptr);
            if (!character)
                break;
            g_engine->characterEntered((int)character);
            g_game->onKeyTyped((char)character);
        }
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        unsigned char key = virtualKey(event.key);
        if (!key)
            break;
        if (event.type == SDL_EVENT_KEY_DOWN) {
            g_engine->keyPressed(key);
            g_game->handleKeyDown(key);
        } else {
            g_engine->keyReleased(key);
            g_game->handleKeyUp(key);
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        int button;
        if (event.button.button == SDL_BUTTON_LEFT)
            button = plasma::MOUSE_BUTTON_LEFT;
        else if (event.button.button == SDL_BUTTON_RIGHT)
            button = plasma::MOUSE_BUTTON_RIGHT;
        else if (event.button.button == SDL_BUTTON_MIDDLE)
            button = plasma::MOUSE_BUTTON_MIDDLE;
        else
            break;
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            g_game->handleButtonDown(button);
        else
            g_game->handleButtonUp(button);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        // One step per notch (WM_MOUSEWHEEL's sign); smooth scrolling adds up to whole steps.
        float distance = event.wheel.y;
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            distance = -distance;
        g_wheel += distance;
        while (g_wheel >= 1.0f) {
            g_game->handleMouseWheel(1);
            g_wheel -= 1.0f;
        }
        while (g_wheel <= -1.0f) {
            g_game->handleMouseWheel(-1);
            g_wheel += 1.0f;
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        g_mouseDeltaX += event.motion.xrel;
        g_mouseDeltaY += event.motion.yrel;
        break;
    }
    return true;
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
bool initializeDirect3D(int screenWidth, int screenHeight)
{
    g_direct3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_direct3d)
        return false;
    D3DPRESENT_PARAMETERS parameters;
    ZeroMemory(&parameters, sizeof(parameters));
    parameters.MultiSampleType = D3DMULTISAMPLE_NONE;
    parameters.MultiSampleQuality = 0;
    parameters.BackBufferWidth = screenWidth;
    parameters.BackBufferHeight = screenHeight;
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    parameters.EnableAutoDepthStencil = TRUE;
    parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
    parameters.hDeviceWindow = (HWND)g_window;
    if (g_direct3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, (HWND)g_window,
                                 D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &parameters,
                                 &g_device) != D3D_OK) {
        g_direct3d->Release();
        g_direct3d = 0;
        return false;
    }
    g_backBufferWidth = screenWidth;
    g_backBufferHeight = screenHeight;
    g_engine = new plasma::D3D9Engine(g_device);
    g_engine->getFontEngine()->addFontPath(L"/usr/share/fonts/TTF");
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
    int width, height;
    clientSize(width, height);
    g_engine->setViewport(0, 0, width, height, true);
    return true;
}

// Resets the device to the window's client size (or the options' resolution in fullscreen) and the
// options' antialiasing, under the game's update locks.
void resetDevice(bool unused)
{
    bool fullscreen = options.fullscreen != 0;
    int width, height;
    if (fullscreen) {
        width = options.resolutionX;
        height = options.resolutionY;
    } else {
        clientSize(width, height);
    }
    if (width <= 0 || height <= 0)
        return;
    if (g_device) {
        if (g_engine) {
            g_engine->releaseDeviceResources();
            g_engine->setViewport(0, 0, width, height, true);
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
        parameters.BackBufferWidth = width;
        parameters.BackBufferHeight = height;
        parameters.Windowed = !fullscreen;
        parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        parameters.EnableAutoDepthStencil = TRUE;
        parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
        parameters.hDeviceWindow = (HWND)g_window;
        if (g_game)
            g_game->beginUpdate();
        while (g_device->Reset(&parameters) != D3D_OK)
            Sleep(DeviceResetRetryDelay);
        g_backBufferWidth = width;
        g_backBufferHeight = height;
        if (g_game) {
            g_game->endUpdate();
            g_game->setWindowSize(width, height);
        }
        if (g_engine)
            g_engine->restoreDeviceResources();
    }
    g_deviceLost = false;
}

// Opens the window and the devices, creates the game and runs the event loop until the window closes.
// Command line: "server" hosts, any other first argument is a server address to join.
int main(int argc, char **argv)
{
    // The game's files (data*.db, *.plx, Save/) are next to the executable.
    const char *base = SDL_GetBasePath();
    if (base && chdir(base) != 0)
        SDL_Log("chdir %s failed", base);

    // DXVK-native draws into the SDL3 window (its window-system driver must be named).
    setenv("DXVK_WSI_DRIVER", "SDL3", 0);

    WSADATA winsock;
    WSAStartup(MAKEWORD(2, 2), &winsock);

    SDL_SetHint(SDL_HINT_APP_ID, "infinite-cubes-alpha-linux");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        showError(SDL_GetError());
        return 0;
    }
    // The screen's size in pixels (GetSystemMetrics), for the first back buffer and the default
    // fullscreen resolution.
    int screenWidth = 800, screenHeight = 600;
    const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    if (desktop) {
        screenWidth = (int)lroundf(desktop->w * desktop->pixel_density);
        screenHeight = (int)lroundf(desktop->h * desktop->pixel_density);
    }
    // The window starts at 800 x 600 and is then maximized.
    g_window = SDL_CreateWindow("Cube World", 800, 600,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                    SDL_WINDOW_HIDDEN);
    if (!g_window) {
        showError(SDL_GetError());
        return 0;
    }
    if (!initializeDirect3D(screenWidth, screenHeight)) {
        showError("Could not initialize Direct3D 9 (DXVK-native). Please make sure Vulkan works "
                  "(vulkaninfo) and lib/libdxvk_d3d9.so is next to the game.");
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
    options.resolutionX = screenWidth;
    options.resolutionY = screenHeight;
    options.load();

    // Deleted before SDL_Quit: FAudio plays through SDL.
    XAudio2Engine *audio = new XAudio2Engine;
    if (!audio->initialize()) {
        showError("Could not initialize the sound (FAudio).");
        return 0;
    }
    audio->setMusicVolume(options.musicVolume * 0.01f, 0);

    bool server = false;
    std::string serverAddress;
    if (argc >= 2) {
        if (std::string(argv[1]) == "server")
            server = true;
        else
            serverAddress = argv[1];
    }
    _mkdir("Save");

    g_game = new GameController(g_device, g_shader, g_engine, audio, &options, &resolutions, server, serverAddress);
    SDL_ShowWindow(g_window);
    SDL_MaximizeWindow(g_window);
    SDL_HideCursor();
    SDL_RaiseWindow(g_window);
    SDL_StartTextInput(g_window);
    resetDevice(false);

    bool relativeMouse = false;
    bool running = true;
    while (running) {
        DWORD frameStart = timeGetTime();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (!handleEvent(event)) {
                running = false;
                break;
            }
        }
        if (!running || g_game->m_quit)
            break;
        bool focused = hasFocus();
        if (g_readInput) {
            // A free cursor appears where the engine's pointer is; otherwise the mouse is captured
            // and reports relative motion (WinMain kept the cursor in the window's middle).
            bool visible = g_game->isCursorVisible();
            bool wantRelative = !visible && focused;
            if (wantRelative != relativeMouse) {
                SDL_SetWindowRelativeMouseMode(g_window, wantRelative);
                relativeMouse = wantRelative;
                g_mouseDeltaX = 0.0f;
                g_mouseDeltaY = 0.0f;
            }
            if (visible) {
                if (!g_cursorShown && focused) {
                    int width = 0, height = 0;
                    SDL_GetWindowSize(g_window, &width, &height);
                    float x = g_engine->getPointer()[0], y = g_engine->getPointer()[1];
                    if (g_backBufferWidth > 0 && g_backBufferHeight > 0) {
                        x *= (float)width / g_backBufferWidth;
                        y *= (float)height / g_backBufferHeight;
                    }
                    SDL_WarpMouseInWindow(g_window, x, y);
                }
                g_cursorShown = true;
            } else if (focused) {
                g_cursorShown = false;
            }
        }
        if (g_readInput && focused) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            g_game->m_inputs[InputKeyW] = keys[SDL_SCANCODE_W];
            g_game->m_inputs[InputKeyS] = keys[SDL_SCANCODE_S];
            g_game->m_inputs[InputKeyA] = keys[SDL_SCANCODE_A];
            g_game->m_inputs[InputKeyD] = keys[SDL_SCANCODE_D];
            g_game->m_inputs[InputKeyE] = keys[SDL_SCANCODE_E];
            g_game->m_inputs[InputKeyR] = keys[SDL_SCANCODE_R];
            g_game->m_inputs[InputKeyT] = keys[SDL_SCANCODE_T];
            g_game->m_inputs[InputKeySpace] = keys[SDL_SCANCODE_SPACE];
            g_game->m_inputs[InputKeyShift] = keys[SDL_SCANCODE_LSHIFT];
            g_game->m_inputs[InputKeyControl] = keys[SDL_SCANCODE_LCTRL];
            g_game->m_inputs[InputKeyQ] = keys[SDL_SCANCODE_Q];
            float x, y;
            SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y);
            if (g_game->isCursorVisible()) {
                cursorPosition(x, y);
                g_game->handleMouseMove(x, y);
            } else {
                g_game->handleMouseMove(g_mouseDeltaX, g_mouseDeltaY);
            }
            g_game->m_inputs[InputMouseLeft] = (buttons & SDL_BUTTON_LMASK) != 0;
            g_game->m_inputs[InputKeyShift2] = keys[SDL_SCANCODE_LSHIFT];
            g_game->m_inputs[InputMouseMiddle] = (buttons & SDL_BUTTON_MMASK) != 0;
            g_game->m_inputs[InputMouseRight] = (buttons & SDL_BUTTON_RMASK) != 0;
            g_game->m_inputs[InputKey1] = keys[SDL_SCANCODE_1];
            g_game->m_inputs[InputKey2] = keys[SDL_SCANCODE_2];
            g_game->m_inputs[InputKey3] = keys[SDL_SCANCODE_3];
            g_game->m_inputs[InputKey4] = keys[SDL_SCANCODE_4];
            g_game->m_moveStick = plasma::Vector<2, int>(0, 0);
            g_game->m_lookStick = plasma::Vector<2, int>(0, 0);
        }
        g_mouseDeltaX = 0.0f;
        g_mouseDeltaY = 0.0f;
        frame();
        DWORD elapsed = timeGetTime() - frameStart;
        if ((int)elapsed < g_game->m_options.minTimeStep)
            Sleep(g_game->m_options.minTimeStep - elapsed);
    }

    SDL_StopTextInput(g_window);
    SDL_SetWindowRelativeMouseMode(g_window, false);
    delete g_game;
    delete audio;
    delete g_engine;
    delete g_shader;
    if (g_device)
        g_device->Release();
    if (g_direct3d)
        g_direct3d->Release();
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    WSACleanup();
    return 0;
}

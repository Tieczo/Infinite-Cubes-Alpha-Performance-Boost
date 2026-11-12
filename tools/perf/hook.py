# Test-only (build-measure.sh): hooks platform/linux/main.cpp (path as argument) for the test input channel (testinput.h).
import sys, re, os
p = sys.argv[1]; src = open(p).read()
header = os.environ.get('TESTINPUT', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'testinput-perf.h'))
def rep(a, b):
    global src
    assert a in src, a
    src = src.replace(a, b, 1)
rep('void resetDevice(bool unused);\n', 'void resetDevice(bool unused);\n#include "%s"\n' % header)
rep('    SDL_GetMouseState(&x, &y);\n', '    SDL_GetMouseState(&x, &y);\n    if (t_mouseSet) { x = t_mx; y = t_my; }\n')
rep('        if (!running || g_game->m_quit)\n            break;\n', '        if (!running || g_game->m_quit)\n            break;\n        testInputPoll();\n        testFrameStats();\n')
src = re.sub(r'keys\[(SDL_SCANCODE_\w+)\]', r'testKey(keys, \1)', src)
rep('SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y);', 'SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y) | t_buttons;')
rep('    if (g_device->Present(0, 0, 0, 0)', '    testShot();\n    if (g_device->Present(0, 0, 0, 0)')
rep('    return (SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS) != 0;', '    return true;')
open(p, 'w').write(src)

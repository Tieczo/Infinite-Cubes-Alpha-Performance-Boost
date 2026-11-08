#pragma once
#include <windows.h>
#include <mmsystem.h>

// The game's millisecond clock for timers (not in the original). The original reads timeGetTime into an int:
// after 24.8 days of system uptime it turns negative, and timers that start at zero stop working (a server's
// send thread sleeps for weeks, the font engine never drops glyphs). This clock counts from its first read,
// starting at one hour so that such timers behave as with a short uptime. Only differences of it are used,
// so the timing stays the same. Seeds keep timeGetTime. Based on coremaze/Cube-World-System-Time-Overflow-Fix.
namespace cube {

const int GameTimeStart = 3600000;

inline int gameTime()
{
    static const DWORD start = timeGetTime();
    return (int)(timeGetTime() - start) + GameTimeStart;
}

}

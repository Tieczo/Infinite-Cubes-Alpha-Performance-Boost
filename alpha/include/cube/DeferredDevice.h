#pragma once
#include <d3d9.h>

namespace cube {

// Threaded rendering (not in the original): wraps the Direct3D 9 device so that, when turned on, the calls the
// creating thread makes for a frame are recorded and made by a drawing thread, which also presents the frame,
// while the creating thread goes on with the next one. The same calls are made in the same order, so the
// picture is the same; the game's own work stays where it was. Off, every call goes straight to the device.
IDirect3DDevice9 *createDeferredDevice(IDirect3DDevice9 *device);
// Turns threaded rendering of the device made by createDeferredDevice on or off, from the next Present on.
void setThreadedRendering(IDirect3DDevice9 *device, bool threaded);

} // namespace cube

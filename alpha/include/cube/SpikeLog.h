#pragma once
#include <windows.h>
#include <atomic>

// The spike log (the Spike Log option on the performance page; not in the original): a line in spikes.log
// for each frame that took more than twice the median of the last frames, with what happened during it,
// and a line for each window event and device reset. Off, a mark only reads one flag.
namespace cube {

enum SpikeCount {
    SpikeDeaths,
    SpikeLootDrops,
    SpikeZoneSaves,
    SpikeRegionSaves,
    SpikeDatabaseWrites,
    SpikeDatabaseReads,
    SpikeZoneGenerations,
    SpikeZoneRemovals,
    SpikeSpawns,
    SpikeDeviceResets,
    SpikeCountCount
};

// Time the main thread waited for a lock, in microseconds.
enum SpikeWait {
    SpikeWaitChunkLock,  // GameController::m_chunkUpdateLock (device reset)
    SpikeWaitMapLock,    // GameController::m_mapUpdateLock (device reset)
    SpikeWaitUpdateLock, // GameController::m_updateLock2 (device reset)
    SpikeWaitZoneLock,   // World::m_zoneLock
    SpikeWaitRegionLock, // World::m_regionLock
    SpikeWaitCount
};

extern std::atomic<bool> g_spikeLogOn;
extern thread_local bool t_spikeMainThread;

void spikeAdd(SpikeCount count);
void spikeWaited(SpikeWait wait, long long microseconds);

inline void spikeMark(SpikeCount count)
{
    if (g_spikeLogOn.load(std::memory_order_relaxed))
        spikeAdd(count);
}

// EnterCriticalSection; while the log is on, the main thread's wait is added to the frame.
void spikeEnterTimed(CRITICAL_SECTION *lock, SpikeWait wait);
inline void spikeEnter(CRITICAL_SECTION *lock, SpikeWait wait)
{
    if (g_spikeLogOn.load(std::memory_order_relaxed) && t_spikeMainThread)
        spikeEnterTimed(lock, wait);
    else
        EnterCriticalSection(lock);
}

// The main thread, once per frame before the window events: opens or closes the log as the option says
// and writes the last frame's line if it was a spike. client: connected to a server.
void spikeFrame(bool on, bool client);
// A window event or device reset, as its own line (only while the log is on).
void spikeEvent(const char *text);
// A device reset: why (for the next one's line), its start, and its end with the new size (not above 0:
// skipped, the window has no area).
void spikeResetReason(const char *reason);
void spikeResetBegin();
void spikeResetDone(int width, int height);

}

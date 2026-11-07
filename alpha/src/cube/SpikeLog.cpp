#include "cube/SpikeLog.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#ifdef __linux__
#include <sys/resource.h>
#endif

// The log is written only by the main thread; the other threads only add to the counters. A spike is a
// frame longer than twice the median of the last 1024 frames (worked out every 128 frames). spikes.log
// in the game folder; past 1 MiB it becomes spikes.old.log (the one before is deleted), so at most 2 MiB.
namespace cube {

namespace {
enum {
    FrameHistory = 1024,
    MedianInterval = 128,
    MaximumLogSize = 1024 * 1024
};
const char *const CountNames[SpikeCountCount] = {"deaths",          "lootDrops",       "zoneSaves",
                                                 "regionSaves",     "databaseWrites",  "databaseReads",
                                                 "zoneGenerations", "zoneRemovals",    "spawns",
                                                 "deviceResets"};
const char *const WaitNames[SpikeWaitCount] = {"waitChunkLock", "waitMapLock", "waitUpdateLock", "waitZoneLock",
                                               "waitRegionLock"};

std::atomic<long long> s_counts[SpikeCountCount];
std::atomic<long long> s_waits[SpikeWaitCount];

FILE *s_file;
std::chrono::steady_clock::time_point s_opened, s_lastFrame;
float s_frames[FrameHistory];
int s_frameCount;  // frames since the log was opened
float s_median;    // 0 until the first MedianInterval frames
bool s_failed; // the log could not be opened: not tried again until the option is switched off and on
const char *s_resetReason = "other";
std::chrono::steady_clock::time_point s_resetStart;
long s_minorFaults, s_majorFaults;

long long microsecondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
}

void pageFaults(long &minor, long &major)
{
#ifdef __linux__
    rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    minor = usage.ru_minflt;
    major = usage.ru_majflt;
#else
    minor = 0;
    major = 0;
#endif
}

// The local time and the seconds since the log was opened, at the start of a line.
void writeTime()
{
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", std::localtime(&seconds));
    int milliseconds =
        (int)(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    fprintf(s_file, "%s.%03d t=%.3f", text, milliseconds, microsecondsSince(s_opened) / 1e6);
}

void open()
{
    s_file = fopen("spikes.log", "a");
    if (!s_file)
        return;
    s_opened = std::chrono::steady_clock::now();
    s_lastFrame = s_opened;
    s_frameCount = 0;
    s_median = 0.0f;
    pageFaults(s_minorFaults, s_majorFaults);
    for (int i = 0; i < SpikeCountCount; i++)
        s_counts[i].store(0);
    for (int i = 0; i < SpikeWaitCount; i++)
        s_waits[i].store(0);
    writeTime();
    fprintf(s_file, " start\n");
    fflush(s_file);
}

void close()
{
    writeTime();
    fprintf(s_file, " stop\n");
    fclose(s_file);
    s_file = 0;
}

// Past MaximumLogSize the log is kept as spikes.old.log and a new one begins.
void limitSize()
{
    if (ftell(s_file) < MaximumLogSize)
        return;
    fclose(s_file);
    std::remove("spikes.old.log");
    std::rename("spikes.log", "spikes.old.log");
    s_file = fopen("spikes.log", "a");
    if (s_file) {
        writeTime();
        fprintf(s_file, " continued (earlier lines in spikes.old.log)\n");
    } else {
        s_failed = true;
        g_spikeLogOn.store(false);
    }
}
}

std::atomic<bool> g_spikeLogOn;
thread_local bool t_spikeMainThread;

void spikeAdd(SpikeCount count)
{
    s_counts[count].fetch_add(1, std::memory_order_relaxed);
}

void spikeWaited(SpikeWait wait, long long microseconds)
{
    s_waits[wait].fetch_add(microseconds, std::memory_order_relaxed);
}

void spikeEnterTimed(CRITICAL_SECTION *lock, SpikeWait wait)
{
    if (TryEnterCriticalSection(lock))
        return;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    EnterCriticalSection(lock);
    spikeWaited(wait, microsecondsSince(start));
}

void spikeFrame(bool on, bool client)
{
    t_spikeMainThread = true;
    if (!on) {
        if (s_file)
            close();
        s_failed = false;
        g_spikeLogOn.store(false);
        return;
    }
    if (!s_file) {
        if (!s_failed)
            open();
        s_failed = !s_file;
        g_spikeLogOn.store(s_file != 0);
        return;
    }
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    float milliseconds = std::chrono::duration<float, std::milli>(now - s_lastFrame).count();
    s_lastFrame = now;
    long long counts[SpikeCountCount], waits[SpikeWaitCount];
    for (int i = 0; i < SpikeCountCount; i++)
        counts[i] = s_counts[i].exchange(0, std::memory_order_relaxed);
    for (int i = 0; i < SpikeWaitCount; i++)
        waits[i] = s_waits[i].exchange(0, std::memory_order_relaxed);
    long minorFaults, majorFaults;
    pageFaults(minorFaults, majorFaults);
    long newMinorFaults = minorFaults - s_minorFaults, newMajorFaults = majorFaults - s_majorFaults;
    s_minorFaults = minorFaults;
    s_majorFaults = majorFaults;

    s_frames[s_frameCount % FrameHistory] = milliseconds;
    s_frameCount++;
    if (s_frameCount % MedianInterval == 0) {
        int n = std::min(s_frameCount, (int)FrameHistory);
        float sorted[FrameHistory];
        std::copy(s_frames, s_frames + n, sorted);
        std::nth_element(sorted, sorted + n / 2, sorted + n);
        s_median = sorted[n / 2];
    }
    if (s_median <= 0.0f || milliseconds <= 2.0f * s_median)
        return;
    writeTime();
    fprintf(s_file, " spike frame=%.2fms median=%.2fms client=%d", milliseconds, s_median, client ? 1 : 0);
    for (int i = 0; i < SpikeCountCount; i++)
        if (counts[i])
            fprintf(s_file, " %s=%lld", CountNames[i], counts[i]);
    for (int i = 0; i < SpikeWaitCount; i++)
        if (waits[i] >= 100)
            fprintf(s_file, " %s=%.1fms", WaitNames[i], waits[i] / 1000.0);
    if (newMinorFaults || newMajorFaults)
        fprintf(s_file, " pageFaults=%ld majorFaults=%ld", newMinorFaults, newMajorFaults);
    fprintf(s_file, "\n");
    fflush(s_file);
    limitSize();
}

void spikeEvent(const char *text)
{
    if (!s_file)
        return;
    writeTime();
    fprintf(s_file, " %s\n", text);
    fflush(s_file);
    limitSize();
}

void spikeResetReason(const char *reason)
{
    s_resetReason = reason;
}

void spikeResetBegin()
{
    s_resetStart = std::chrono::steady_clock::now();
}

void spikeResetDone(int width, int height)
{
    long long microseconds = microsecondsSince(s_resetStart);
    const char *reason = s_resetReason;
    s_resetReason = "other";
    if (!s_file)
        return;
    char text[128];
    if (width > 0 && height > 0) {
        spikeAdd(SpikeDeviceResets);
        snprintf(text, sizeof(text), "device reset reason=%s size=%dx%d took=%.1fms", reason, width, height,
                 microseconds / 1000.0);
    } else {
        snprintf(text, sizeof(text), "device reset skipped reason=%s size=%dx%d", reason, width, height);
    }
    spikeEvent(text);
}

}

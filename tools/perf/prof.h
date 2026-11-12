// Test-only frame profiler (only ever patched into a test copy, never into a game commit): main-thread time per zone (rdtsc), Direct3D 9 calls per
// zone (vtable hooks: count and time inside DXVK), counters, and a 1 kHz main-thread CPU-time sampler
// (leaf PC + zone) written to $CUBE_PROF_SAMPLES at exit.
#pragma once
#include <x86intrin.h>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <csignal>
#include <ctime>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <dirent.h>
#include <map>
#include <string>
#include <ucontext.h>
#include <d3d9.h>
#include <vector>
#include <algorithm>

enum ProfZone {
    PZ_OTHER, PZ_UPDATE, PZ_WORLD, PZ_CHUNKSCAN, PZ_SETUP, PZ_LIGHTS, PZ_CLOUDS, PZ_TERRAIN, PZ_PLANTS, PZ_ITEMS,
    PZ_CREATURES, PZ_EFFECTS, PZ_WATER, PZ_DEFERRED, PZ_UI, PZ_PRESENT, PZ_COUNT
};
static const char *const g_profZoneNames[PZ_COUNT] = {
    "Rest (Eingabe, Schleife)", "Spiellogik (GameCtrl::update)", "Spiellogik (World::update)", "Chunk-Durchlauf/Culling", "Himmel/Sonne/Setup",
    "Lichter sammeln/verteilen", "Wolken", "Landschaft zeichnen", "Pflanzen (nahe)", "Items/Objekte/Luftschiffe",
    "Lebewesen", "Partikel/Geschosse/Schatten", "Wasser", "Pflanzen/Objekte (fern, Blend)", "UI",
    "Present"};

enum ProfCounter {
    PC_ACTIVE_CHUNKS, PC_VISIBLE_CHUNKS, PC_CHUNK_BUFFERS, PC_PLANTS_SCANNED, PC_PLANTS_DRAWN, PC_DEFERRED,
    PC_CREATURES, PC_CREATURES_DRAWN, PC_LIGHTS, PC_COUNT
};
static const char *const g_profCounterNames[PC_COUNT] = {
    "aktive Chunks", "sichtbare Chunks", "Chunk-Puffer gezeichnet", "Pflanzen durchlaufen (Lichter/Sound)",
    "Pflanzen nah gezeichnet", "fern (Blend) gezeichnet", "Lebewesen geladen", "Lebewesen gezeichnet",
    "Lichter"};

enum ProfCall { PK_DRAW, PK_VSCONST, PK_PSCONST, PK_STATE, PK_OTHER, PK_COUNT };
static const char *const g_profCallNames[PK_COUNT] = {"Draw", "VSConst", "PSConst", "State", "Sonst"};

inline int g_pz = PZ_OTHER;
inline uint64_t g_pzStart;
inline uint64_t g_pzTicks[PZ_COUNT];
inline uint64_t g_pzD3dTicks[PZ_COUNT];
inline uint64_t g_pzCalls[PZ_COUNT][PK_COUNT];
inline uint64_t g_pc[PC_COUNT];
inline uint64_t g_pzWorstFrame[PZ_COUNT];
inline std::vector<float> g_profMeasuredFrames; // frame times (ms) while the sampler flag is set
inline double g_profNsPerTick;

static inline void profZone(int z)
{
    uint64_t now = __rdtsc();
    g_pzTicks[g_pz] += now - g_pzStart;
    g_pzStart = now;
    g_pz = z;
}
#define PZ(z) profZone(PZ_##z)
#define PCOUNT(c, n) (g_pc[PC_##c] += (n))

// ---- Direct3D 9 vtable hooks ----
template<typename F> static size_t profVtableIndex(F f)
{
    uintptr_t raw[2];
    static_assert(sizeof(f) == sizeof(raw), "PMF size");
    memcpy(raw, &f, sizeof raw);
    return (raw[0] - 1) / sizeof(void *);
}
inline void **g_profVtbl;
#define PROF_HOOK(name, kind, ret, params, args)                                                         \
    typedef ret (*Prof_##name##_t) params;                                                              \
    static Prof_##name##_t g_prof_orig_##name;                                                          \
    static ret prof_hk_##name params                                                                    \
    {                                                                                                   \
        uint64_t t0 = __rdtsc();                                                                        \
        ret r = g_prof_orig_##name args;                                                                \
        g_pzD3dTicks[g_pz] += __rdtsc() - t0;                                                           \
        g_pzCalls[g_pz][kind]++;                                                                        \
        return r;                                                                                       \
    }
typedef IDirect3DDevice9 Dev;
PROF_HOOK(DrawIndexedPrimitive, PK_DRAW, HRESULT, (Dev *d, D3DPRIMITIVETYPE a, INT b, UINT c, UINT e, UINT f, UINT g), (d, a, b, c, e, f, g))
PROF_HOOK(DrawPrimitive, PK_DRAW, HRESULT, (Dev *d, D3DPRIMITIVETYPE a, UINT b, UINT c), (d, a, b, c))
PROF_HOOK(DrawPrimitiveUP, PK_DRAW, HRESULT, (Dev *d, D3DPRIMITIVETYPE a, UINT b, const void *c, UINT e), (d, a, b, c, e))
PROF_HOOK(DrawIndexedPrimitiveUP, PK_DRAW, HRESULT, (Dev *d, D3DPRIMITIVETYPE a, UINT b, UINT c, UINT e, const void *f, D3DFORMAT g, const void *h, UINT i), (d, a, b, c, e, f, g, h, i))
PROF_HOOK(SetVertexShaderConstantF, PK_VSCONST, HRESULT, (Dev *d, UINT a, const float *b, UINT c), (d, a, b, c))
PROF_HOOK(SetPixelShaderConstantF, PK_PSCONST, HRESULT, (Dev *d, UINT a, const float *b, UINT c), (d, a, b, c))
PROF_HOOK(SetRenderState, PK_STATE, HRESULT, (Dev *d, D3DRENDERSTATETYPE a, DWORD b), (d, a, b))
PROF_HOOK(SetSamplerState, PK_STATE, HRESULT, (Dev *d, DWORD a, D3DSAMPLERSTATETYPE b, DWORD c), (d, a, b, c))
PROF_HOOK(SetTexture, PK_STATE, HRESULT, (Dev *d, DWORD a, IDirect3DBaseTexture9 *b), (d, a, b))
PROF_HOOK(SetStreamSource, PK_STATE, HRESULT, (Dev *d, UINT a, IDirect3DVertexBuffer9 *b, UINT c, UINT e), (d, a, b, c, e))
PROF_HOOK(SetIndices, PK_STATE, HRESULT, (Dev *d, IDirect3DIndexBuffer9 *a), (d, a))
PROF_HOOK(SetVertexShader, PK_STATE, HRESULT, (Dev *d, IDirect3DVertexShader9 *a), (d, a))
PROF_HOOK(SetPixelShader, PK_STATE, HRESULT, (Dev *d, IDirect3DPixelShader9 *a), (d, a))
PROF_HOOK(SetFVF, PK_STATE, HRESULT, (Dev *d, DWORD a), (d, a))
PROF_HOOK(SetVertexDeclaration, PK_STATE, HRESULT, (Dev *d, IDirect3DVertexDeclaration9 *a), (d, a))
PROF_HOOK(SetTransform, PK_STATE, HRESULT, (Dev *d, D3DTRANSFORMSTATETYPE a, const D3DMATRIX *b), (d, a, b))
PROF_HOOK(Clear, PK_OTHER, HRESULT, (Dev *d, DWORD a, const D3DRECT *b, DWORD c, D3DCOLOR e, float f, DWORD g), (d, a, b, c, e, f, g))
PROF_HOOK(BeginScene, PK_OTHER, HRESULT, (Dev *d), (d))
PROF_HOOK(EndScene, PK_OTHER, HRESULT, (Dev *d), (d))
PROF_HOOK(Present, PK_OTHER, HRESULT, (Dev *d, const RECT *a, const RECT *b, HWND c, const RGNDATA *e), (d, a, b, c, e))

static void profInstallOne(size_t index, void **orig, void *hook)
{
    if (g_profVtbl[index] == hook)
        return;
    *orig = g_profVtbl[index];
    g_profVtbl[index] = hook;
}
#define PROF_INSTALL(name) profInstallOne(profVtableIndex(&IDirect3DDevice9::name), (void **)&g_prof_orig_##name, (void *)&prof_hk_##name)
static void profInstallHooks(IDirect3DDevice9 *device)
{
    if (!device || getenv("CUBE_PROF_NOHOOK"))
        return;
    void **vtbl = *(void ***)device;
    if (vtbl == g_profVtbl)
        return;
    g_profVtbl = vtbl;
    long page = sysconf(_SC_PAGESIZE);
    uintptr_t start = (uintptr_t)vtbl & ~(uintptr_t)(page - 1);
    mprotect((void *)start, page * 3, PROT_READ | PROT_WRITE);
    PROF_INSTALL(DrawIndexedPrimitive); PROF_INSTALL(DrawPrimitive); PROF_INSTALL(DrawPrimitiveUP);
    PROF_INSTALL(DrawIndexedPrimitiveUP); PROF_INSTALL(SetVertexShaderConstantF); PROF_INSTALL(SetPixelShaderConstantF);
    PROF_INSTALL(SetRenderState); PROF_INSTALL(SetSamplerState); PROF_INSTALL(SetTexture); PROF_INSTALL(SetStreamSource);
    PROF_INSTALL(SetIndices); PROF_INSTALL(SetVertexShader); PROF_INSTALL(SetPixelShader); PROF_INSTALL(SetFVF);
    PROF_INSTALL(SetVertexDeclaration); PROF_INSTALL(SetTransform); PROF_INSTALL(Clear); PROF_INSTALL(BeginScene);
    PROF_INSTALL(EndScene); PROF_INSTALL(Present);
    fprintf(stderr, "PROF hooks installed (vtbl %p)\n", (void *)vtbl);
}

// ---- Sampler: main thread CPU time, 1 kHz, leaf PC + zone ----
struct ProfSample { uintptr_t pc; int zone; int frame; };
inline volatile int g_rcFrameNo;
inline ProfSample *g_profSamples;
inline volatile size_t g_profSampleCount;
static const size_t ProfMaxSamples = 4u << 20;
inline volatile int g_profSampling;
static void profSignal(int, siginfo_t *, void *uc)
{
    if (!g_profSampling)
        return;
    size_t n = g_profSampleCount;
    if (n >= ProfMaxSamples)
        return;
    g_profSamples[n].pc = (uintptr_t)((ucontext_t *)uc)->uc_mcontext.gregs[REG_RIP];
    g_profSamples[n].zone = g_pz;
    g_profSamples[n].frame = g_rcFrameNo;
    g_profSampleCount = n + 1;
}
static void profStartSampler()
{
    static bool started;
    if (started || !getenv("CUBE_PROF_SAMPLES"))
        return;
    started = true;
    g_profSamples = new ProfSample[ProfMaxSamples];
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = profSignal;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGRTMIN + 3, &sa, 0);
    clockid_t clock;
    pthread_getcpuclockid(pthread_self(), &clock);
    struct sigevent ev;
    memset(&ev, 0, sizeof ev);
    ev.sigev_notify = SIGEV_THREAD_ID;
    ev.sigev_signo = SIGRTMIN + 3;
    ev._sigev_un._tid = (pid_t)syscall(SYS_gettid);
    timer_t timer;
    if (timer_create(clock, &ev, &timer) != 0) {
        perror("timer_create");
        return;
    }
    struct itimerspec its;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 1000000;
    its.it_value = its.it_interval;
    timer_settime(timer, 0, &its, 0);
    fprintf(stderr, "PROF sampler started\n");
}
// ---- Second sampler: the busiest other thread named like the game (the drawing thread when it runs) ----
// Chosen 256 frames after sampling starts from the CPU time gained since; samples to $CUBE_PROF_SAMPLES.draw.
inline ProfSample *g_profDrawSamples;
inline volatile size_t g_profDrawCount;
static void profDrawSignal(int, siginfo_t *, void *uc)
{
    size_t n = g_profDrawCount;
    if (n >= ProfMaxSamples)
        return;
    g_profDrawSamples[n].pc = (uintptr_t)((ucontext_t *)uc)->uc_mcontext.gregs[REG_RIP];
    g_profDrawSamples[n].zone = 0;
    g_profDrawCount = n + 1;
}
static void profThreadTimes(std::map<int, long> &times)
{
    char path[64], text[1024];
    DIR *dir = opendir("/proc/self/task");
    while (dir) {
        struct dirent *e = readdir(dir);
        if (!e)
            break;
        int tid = atoi(e->d_name);
        if (tid <= 0 || tid == (int)syscall(SYS_gettid))
            continue;
        snprintf(path, sizeof path, "/proc/self/task/%d/stat", tid);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        size_t n = fread(text, 1, sizeof text - 1, f);
        fclose(f);
        text[n] = 0;
        char *end = strrchr(text, ')');
        if (!strstr(text, "(Cube)") || !end)
            continue;
        long utime = 0, stime = 0;
        if (sscanf(end + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld", &utime, &stime) == 2)
            times[tid] = utime + stime;
    }
    if (dir)
        closedir(dir);
}
static void profDrawSamplerStep()
{
    static std::map<int, long> before;
    static int frames;
    static bool done;
    if (done || !g_profSampling)
        return;
    if (frames == 0)
        profThreadTimes(before);
    if (++frames < 256)
        return;
    done = true;
    std::map<int, long> after;
    profThreadTimes(after);
    int best = 0;
    long gain = 0;
    for (auto &t : after)
        if (t.second - before[t.first] > gain) {
            gain = t.second - before[t.first];
            best = t.first;
        }
    if (!best)
        return;
    g_profDrawSamples = new ProfSample[ProfMaxSamples];
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = profDrawSignal;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGRTMIN + 4, &sa, 0);
    clockid_t clock = (clockid_t)((~(unsigned)best << 3) | 6); // the kernel's CPU clock of a thread (CPUCLOCK_SCHED)
    struct sigevent ev;
    memset(&ev, 0, sizeof ev);
    ev.sigev_notify = SIGEV_THREAD_ID;
    ev.sigev_signo = SIGRTMIN + 4;
    ev._sigev_un._tid = best;
    timer_t timer;
    if (timer_create(clock, &ev, &timer) != 0) {
        perror("timer_create draw");
        return;
    }
    struct itimerspec its;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 1000000;
    its.it_value = its.it_interval;
    timer_settime(timer, 0, &its, 0);
    fprintf(stderr, "PROF draw sampler on tid %d (gain %ld ticks)\n", best, gain);
}
// Starts/stops sampling (command "sample 1"/"sample 0") and writes the samples and the maps.
// ---- Spike log (ruckler.h marks, ruckler_patch.py): every frame over twice the median, with what happened in it ----
#if __has_include("cube/ruckler.h")
#include "cube/ruckler.h"
#include <sys/resource.h>
#define PROF_RUCKLER 1
inline uint64_t g_rcZone[PZ_COUNT];
inline std::vector<int> g_rcSpikes;
struct RcStat { uint64_t frames, spikeFrames; };
inline RcStat g_rcStat[RC_COUNT];
inline uint64_t g_rcFrames, g_rcOver[4];
inline double g_rcExcessMs;
static void rcFrame(double ms, double nsPerTick)
{
    static float ring[1024];
    static unsigned count;
    static float median;
    static double zoneMean[PZ_COUNT];
    static uint64_t lastN[2][RC_COUNT], lastT[2][RC_COUNT];
    static long lastFlt, lastMaj, lastVol, lastInvol;
    struct rusage ru;
    getrusage(RUSAGE_THREAD, &ru);
    long flt = ru.ru_minflt - lastFlt, maj = ru.ru_majflt - lastMaj, vol = ru.ru_nvcsw - lastVol, invol = ru.ru_nivcsw - lastInvol;
    lastFlt = ru.ru_minflt; lastMaj = ru.ru_majflt; lastVol = ru.ru_nvcsw; lastInvol = ru.ru_nivcsw;
    uint64_t dn[2][RC_COUNT], dt[2][RC_COUNT];
    for (int i = 0; i < 2; i++)
        for (int c = 0; c < RC_COUNT; c++) {
            uint64_t n = g_rcN[i][c], t = g_rcT[i][c];
            dn[i][c] = n - lastN[i][c]; dt[i][c] = t - lastT[i][c];
            lastN[i][c] = n; lastT[i][c] = t;
        }
    int frame = g_rcFrameNo;
    g_rcFrameNo = frame + 1;
    ring[count++ % 1024] = (float)ms;
    if (count % 128 == 0 && count >= 256) {
        unsigned n = count < 1024 ? count : 1024;
        std::vector<float> v(ring, ring + n);
        std::nth_element(v.begin(), v.begin() + n / 2, v.end());
        median = v[n / 2];
    }
    if (median <= 0.0f)
        return;
    g_rcFrames++;
    bool spike = ms > 2.0 * median;
    for (int c = 0; c < RC_COUNT; c++)
        if (dn[0][c] + dn[1][c]) {
            g_rcStat[c].frames++;
            if (spike) g_rcStat[c].spikeFrames++;
        }
    if (spike) {
        g_rcSpikes.push_back(frame);
        g_rcExcessMs += ms - median;
        g_rcOver[0]++;
        if (ms > 16.7) g_rcOver[1]++;
        if (ms > 33.3) g_rcOver[2]++;
        if (ms > 50.0) g_rcOver[3]++;
        if (g_rcSpikes.size() <= 6000) {
            int order[PZ_COUNT];
            double extra[PZ_COUNT];
            for (int i = 0; i < PZ_COUNT; i++) {
                order[i] = i;
                extra[i] = g_rcZone[i] * nsPerTick / 1e6 - zoneMean[i];
            }
            std::sort(order, order + PZ_COUNT, [&](int a, int b) { return extra[a] > extra[b]; });
            char causes[512];
            int at = 0;
            causes[0] = 0;
            for (int c = 0; c < RC_COUNT && at < 460; c++)
                if (dn[0][c] + dn[1][c])
                    at += snprintf(causes + at, sizeof causes - at, " %s=%llu/%.2f/%llu/%.2f", g_rcNames[c], (unsigned long long)dn[0][c],
                                   dt[0][c] * nsPerTick / 1e6, (unsigned long long)dn[1][c], dt[1][c] * nsPerTick / 1e6);
            fprintf(stderr, "SPIKE f=%d ms=%.2f med=%.2f pf=%ld maj=%ld vcs=%ld ics=%ld Z %s=+%.2f %s=+%.2f %s=+%.2f C%s\n", frame, ms, median, flt, maj,
                    vol, invol, g_profZoneNames[order[0]], extra[order[0]], g_profZoneNames[order[1]], extra[order[1]],
                    g_profZoneNames[order[2]], extra[order[2]], causes);
        }
    } else {
        for (int i = 0; i < PZ_COUNT; i++)
            zoneMean[i] += (g_rcZone[i] * nsPerTick / 1e6 - zoneMean[i]) / 64.0;
    }
}
static void rcSummary()
{
    fprintf(stderr, "SPIKESUM frames=%llu spikes=%llu over16=%llu over33=%llu over50=%llu excess_ms=%.0f\n", (unsigned long long)g_rcFrames,
            (unsigned long long)g_rcOver[0], (unsigned long long)g_rcOver[1], (unsigned long long)g_rcOver[2],
            (unsigned long long)g_rcOver[3], g_rcExcessMs);
    double nsPerTick = g_profNsPerTick;
    for (int c = 0; c < RC_COUNT; c++)
        fprintf(stderr, "SPIKECAUSE %s main=%llu/%.1fms andere=%llu/%.1fms bilder=%llu davon_spitzen=%llu\n", g_rcNames[c],
                (unsigned long long)g_rcN[0][c].load(), g_rcT[0][c].load() * nsPerTick / 1e6, (unsigned long long)g_rcN[1][c].load(),
                g_rcT[1][c].load() * nsPerTick / 1e6, (unsigned long long)g_rcStat[c].frames, (unsigned long long)g_rcStat[c].spikeFrames);
}
#endif

static void profWriteSamples()
{
    if (!g_profMeasuredFrames.empty()) {
        double sum = 0;
        for (float x : g_profMeasuredFrames)
            sum += x;
        std::vector<float> sorted = g_profMeasuredFrames;
        std::sort(sorted.begin(), sorted.end());
        fprintf(stderr, "FRAMETIMES n=%zu mean=%.3fms fps=%.1f p50=%.3fms p99=%.3fms p999=%.3fms max=%.3fms\n", sorted.size(),
                sum / sorted.size(), 1000.0 * sorted.size() / sum, sorted[sorted.size() / 2],
                sorted[(size_t)(sorted.size() * 0.99)], sorted[(size_t)(sorted.size() * 0.999)], sorted.back());
    }
#ifdef PROF_RUCKLER
    rcSummary();
#endif
    const char *path = getenv("CUBE_PROF_SAMPLES");
    if (!path || !g_profSamples)
        return;
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    FILE *maps = fopen("/proc/self/maps", "r");
    char line[1024];
    while (maps && fgets(line, sizeof line, maps))
        if (strstr(line, " r-xp ") || strstr(line, " r--p 00000000 "))
            fprintf(f, "M %s", line);
    if (maps)
        fclose(maps);
    for (size_t i = 0; i < g_profSampleCount; i++)
        fprintf(f, "S %lx %d\n", (unsigned long)g_profSamples[i].pc, g_profSamples[i].zone);
    fclose(f);
    fprintf(stderr, "PROF wrote %zu samples to %s\n", (size_t)g_profSampleCount, path);
#ifdef PROF_RUCKLER
    {   // the samples of the spike frames, for symbolize.py
        std::string spikePath = std::string(path) + ".spike";
        FILE *d = fopen(spikePath.c_str(), "w");
        FILE *m = fopen("/proc/self/maps", "r");
        while (d && m && fgets(line, sizeof line, m))
            if (strstr(line, " r-xp ") || strstr(line, " r--p 00000000 "))
                fprintf(d, "M %s", line);
        if (m)
            fclose(m);
        std::sort(g_rcSpikes.begin(), g_rcSpikes.end());
        for (size_t i = 0; d && i < g_profSampleCount; i++)
            if (std::binary_search(g_rcSpikes.begin(), g_rcSpikes.end(), g_profSamples[i].frame))
                fprintf(d, "S %lx %d\n", (unsigned long)g_profSamples[i].pc, g_profSamples[i].zone);
        if (d)
            fclose(d);
    }
#endif
    if (g_profDrawSamples) {
        std::string drawPath = std::string(path) + ".draw";
        FILE *d = fopen(drawPath.c_str(), "w");
        FILE *m = fopen("/proc/self/maps", "r");
        while (d && m && fgets(line, sizeof line, m))
            if (strstr(line, " r-xp ") || strstr(line, " r--p 00000000 "))
                fprintf(d, "M %s", line);
        if (m)
            fclose(m);
        for (size_t i = 0; d && i < g_profDrawCount; i++)
            fprintf(d, "S %lx 0\n", (unsigned long)g_profDrawSamples[i].pc);
        if (d)
            fclose(d);
    }
}

// ---- Report every 10 s (called once per frame at the start of the loop) ----
inline int g_profFrames;
inline uint64_t g_profFrameTicks[PZ_COUNT];
static void profFrameBoundary(IDirect3DDevice9 *device, int creatures)
{
    static std::chrono::steady_clock::time_point wallStart;
    static uint64_t tscStart;
    static bool init;
    profInstallHooks(device);
    profStartSampler();
    profDrawSamplerStep();
    profZone(PZ_OTHER);
    // Sampling runs from the "sample" file's appearance next to the fifo (touch it after loading).
    static int checkCounter;
    if (!g_profSampling && g_profSamples && ++checkCounter % 256 == 0) {
        const char *flag = getenv("CUBE_PROF_SAMPLE_FLAG");
        if (flag && access(flag, F_OK) == 0) {
            g_profSampling = 1;
            fprintf(stderr, "PROF sampling on\n");
        }
    }
    // worst single frame per zone: keep the per-frame totals
    static uint64_t lastTicks[PZ_COUNT];
    uint64_t frameTicks = 0;
    for (int i = 0; i < PZ_COUNT; i++)
        frameTicks += g_pzTicks[i] - lastTicks[i];
    if (g_profSampling && g_profNsPerTick > 0)
        g_profMeasuredFrames.push_back((float)(frameTicks * g_profNsPerTick / 1e6));
    for (int i = 0; i < PZ_COUNT; i++) {
        uint64_t d = g_pzTicks[i] - lastTicks[i];
#ifdef PROF_RUCKLER
        g_rcZone[i] = d;
#endif
        lastTicks[i] = g_pzTicks[i];
        if (d > g_pzWorstFrame[i])
            g_pzWorstFrame[i] = d;
    }
#ifdef PROF_RUCKLER
    t_rcMain = true;
    if (g_profSampling && g_profNsPerTick > 0)
        rcFrame(frameTicks * g_profNsPerTick / 1e6, g_profNsPerTick);
#endif
    if (!init) {
        init = true;
        wallStart = std::chrono::steady_clock::now();
        tscStart = __rdtsc();
        memset(g_pzTicks, 0, sizeof g_pzTicks);
        memset(lastTicks, 0, sizeof lastTicks);
        memset(g_pzWorstFrame, 0, sizeof g_pzWorstFrame);
        return;
    }
    g_profFrames++;
    auto now = std::chrono::steady_clock::now();
    double ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - wallStart).count();
    if (ns < 10e9)
        return;
    uint64_t tsc = __rdtsc();
    double nsPerTick = ns / (double)(tsc - tscStart);
    g_profNsPerTick = nsPerTick;
    int n = g_profFrames;
    double total = 0;
    for (int i = 0; i < PZ_COUNT; i++)
        total += g_pzTicks[i];
    fprintf(stderr, "PROF frames=%d fps=%.1f frame=%.3fms creatures=%d\n", n, n / (ns / 1e9),
            total * nsPerTick / 1e6 / n, creatures);
    fprintf(stderr, "PROF   %-32s %8s %8s %8s %6s %7s %7s %7s %6s\n", "Bereich", "ms/Bild", "D3D-ms", "max-ms",
            "Anteil", "Draws", "VSConst", "State", "PSC");
    for (int i = 0; i < PZ_COUNT; i++) {
        double ms = g_pzTicks[i] * nsPerTick / 1e6 / n;
        double d3d = g_pzD3dTicks[i] * nsPerTick / 1e6 / n;
        fprintf(stderr, "PROF   %-32s %8.3f %8.3f %8.3f %5.1f%% %7.0f %7.0f %7.0f %6.0f\n", g_profZoneNames[i], ms, d3d,
                g_pzWorstFrame[i] * nsPerTick / 1e6, 100.0 * g_pzTicks[i] / total, (double)g_pzCalls[i][PK_DRAW] / n,
                (double)g_pzCalls[i][PK_VSCONST] / n, (double)g_pzCalls[i][PK_STATE] / n,
                (double)g_pzCalls[i][PK_PSCONST] / n);
    }
    double d3dAll = 0, drawsAll = 0;
    for (int i = 0; i < PZ_COUNT; i++) {
        d3dAll += g_pzD3dTicks[i];
        drawsAll += g_pzCalls[i][PK_DRAW];
    }
    fprintf(stderr, "PROF   Summe D3D-Aufrufe %.3f ms/Bild, Draws %.0f/Bild\n", d3dAll * nsPerTick / 1e6 / n, drawsAll / n);
    for (int i = 0; i < PC_COUNT; i++)
        fprintf(stderr, "PROF   # %-40s %10.1f\n", g_profCounterNames[i], (double)g_pc[i] / n);
    memset(g_pzTicks, 0, sizeof g_pzTicks);
    memset(lastTicks, 0, sizeof lastTicks);
    memset(g_pzD3dTicks, 0, sizeof g_pzD3dTicks);
    memset(g_pzCalls, 0, sizeof g_pzCalls);
    memset(g_pc, 0, sizeof g_pc);
    memset(g_pzWorstFrame, 0, sizeof g_pzWorstFrame);
    g_profFrames = 0;
    wallStart = now;
    tscStart = tsc;
}

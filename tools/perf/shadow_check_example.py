# Template, kept for its method (anchors fit commit f22626c only): turns a copy of the P1 code into a P1 equivalence check. Every
# frame, for every chunk, the original plant loop runs for real (recording the lights it adds, the sounds it
# plays and every rand() result), then the Fast Plant Scan path runs on the same state, replaying the
# recorded rand() results and only recording; lights, sounds (id, position, volume, pitch), the number of
# rand() calls and the lamp phase must be equal. Every 10 s: "P1CHECK ..." on stderr.
# Usage: p1_check.py <copy>/alpha
import sys
p = sys.argv[1] + '/src/cube/GameControllerRender.cpp'
s = open(p).read()
def rep(a, b, count=1):
    global s
    assert s.count(a) == count, (s.count(a), a[:90])
    s = s.replace(a, b)
helpers = r'''
// ---- P1 check (test only) ----
#include <cstring>
#include <SDL3/SDL_timer.h>
namespace {
struct ChkSound { int id; long long x, y, z; float volume, pitch; };
int g_chkMode; // 0 off, 1 record (original path, real effects), 2 replay (fast path, no effects)
std::vector<int> g_chkRand;
size_t g_chkPos;
bool g_chkOverrun;
std::vector<ChkSound> g_chkSounds;
long long g_chkChunks, g_chkLights, g_chkSoundsTotal, g_chkRands, g_chkMismatch, g_chkFrames;
int chkRand()
{
    if (g_chkMode == 2) {
        if (g_chkPos < g_chkRand.size())
            return g_chkRand[g_chkPos++];
        g_chkOverrun = true;
        return 0;
    }
    int r = rand();
    if (g_chkMode == 1)
        g_chkRand.push_back(r);
    return r;
}
}
'''
rep('namespace cube {\n', helpers + 'namespace cube {\n')
# inside the call lambda: rand() and playSound( go through the check
start = s.index('            auto call = [&](const Plant &plant) {')
end = s.index('            };\n', start) + len('            };\n')
body = s[start:end]
body = body.replace('rand()', 'chkRand()').replace('playSound(', 'chkSound(')
chk_sound = '''            auto chkSound = [&](int id, const plasma::Vector<3, Fixed> &position, float volume, float pitch) {
                ChkSound c;
                c.id = id;
                memcpy(&c.x, &position[0], sizeof(long long));
                memcpy(&c.y, &position[1], sizeof(long long));
                memcpy(&c.z, &position[2], sizeof(long long));
                c.volume = volume;
                c.pitch = pitch;
                g_chkSounds.push_back(c);
                if (g_chkMode != 2)
                    playSound(id, position, volume, pitch);
            };
'''
s = s[:start] + chk_sound + body + s[end:]
# the dispatch: record with the original loop, replay with the fast one, compare, keep the original results
a = s[s.index('            if (m_options.fastPlantScan) {'):]
a = a[:a.index('            LeaveCriticalSection(&chunk->lock);')]
check = '''            {
                size_t lightsBefore = lights.size();
                int phaseBefore = phase;
                g_chkMode = 1;
                g_chkRand.clear();
                g_chkSounds.clear();
                for (std::list<Plant>::iterator plant = chunk->plants.begin(); plant != chunk->plants.end(); ++plant) {
                    shine(*plant);
                    if (4096.0f > chunk->squaredCameraDistance)
                        call(*plant);
                }
                std::vector<FrameLight> originalLights(lights.begin() + lightsBefore, lights.end());
                std::vector<ChkSound> originalSounds = g_chkSounds;
                int phaseAfter = phase;
                lights.resize(lightsBefore);
                phase = phaseBefore;
                g_chkMode = 2;
                g_chkPos = 0;
                g_chkOverrun = false;
                g_chkSounds.clear();
                for (std::vector<Plant *>::iterator plant = chunk->emissivePlants.begin();
                     plant != chunk->emissivePlants.end(); ++plant)
                    shine(**plant);
                if (4096.0f > chunk->squaredCameraDistance) {
                    for (std::vector<Plant *>::iterator plant = chunk->callingPlants.begin();
                         plant != chunk->callingPlants.end(); ++plant)
                        call(**plant);
                }
                bool same = !g_chkOverrun && g_chkPos == g_chkRand.size() && phase == phaseAfter &&
                            lights.size() - lightsBefore == originalLights.size() &&
                            g_chkSounds.size() == originalSounds.size();
                for (size_t i = 0; same && i < originalLights.size(); i++)
                    same = memcmp(&lights[lightsBefore + i].position, &originalLights[i].position,
                                  sizeof(originalLights[i].position)) == 0 &&
                           memcmp(&lights[lightsBefore + i].radius, &originalLights[i].radius, sizeof(float)) == 0 &&
                           memcmp(&lights[lightsBefore + i].color, &originalLights[i].color,
                                  sizeof(originalLights[i].color)) == 0 &&
                           memcmp(&lights[lightsBefore + i].distanceSquared, &originalLights[i].distanceSquared,
                                  sizeof(float)) == 0;
                for (size_t i = 0; same && i < originalSounds.size(); i++)
                    same = g_chkSounds[i].id == originalSounds[i].id && g_chkSounds[i].x == originalSounds[i].x &&
                           g_chkSounds[i].y == originalSounds[i].y && g_chkSounds[i].z == originalSounds[i].z &&
                           memcmp(&g_chkSounds[i].volume, &originalSounds[i].volume, sizeof(float)) == 0 &&
                           memcmp(&g_chkSounds[i].pitch, &originalSounds[i].pitch, sizeof(float)) == 0;
                g_chkChunks++;
                g_chkLights += originalLights.size();
                g_chkSoundsTotal += originalSounds.size();
                g_chkRands += g_chkRand.size();
                if (!same)
                    g_chkMismatch++;
                lights.resize(lightsBefore);
                lights.insert(lights.end(), originalLights.begin(), originalLights.end());
                phase = phaseAfter;
                g_chkMode = 0;
            }
'''
s = s.replace(a, check, 1)
# report every 10 s, once per frame (after the chunk pass)
rep('    // While "Please wait..." shows', '''    {
        static Uint64 chkLast;
        g_chkFrames++;
        Uint64 now = SDL_GetTicksNS();
        if (!chkLast)
            chkLast = now;
        if (now - chkLast >= 10000000000ull) {
            fprintf(stderr, "P1CHECK frames=%lld chunks=%lld lights=%lld sounds=%lld rands=%lld mismatches=%lld\\n",
                    g_chkFrames, g_chkChunks, g_chkLights, g_chkSoundsTotal, g_chkRands, g_chkMismatch);
            chkLast = now;
        }
    }
    // While "Please wait..." shows''')
open(p, 'w').write(s)
print('p1 check ok')

// Test-only input channel (patched into test copies only): commands from the FIFO in $CUBE_TEST_FIFO become SDL
// events passed to handleEvent, plus overrides for the polled keyboard/mouse state, frame statistics,
// back-buffer screenshots and the visible screen.
#include "cube/widgets/StartMenuWidget.h"
#include "cube/WorldDimensions.h"
#include "cube/Field.h"
#include "cube/CreatureIds.h"
#include "cube/Behavior.h"
#include "cube/ServerUpdates.h"
#include "cube/HitPacket.h"
#include <vector>
// A/B builds (ab_patch.py) define these; elsewhere they stay null.
extern int g_abOld __attribute__((weak)), g_abFreeze __attribute__((weak));
#include <fcntl.h>
#include <map>
#include <sstream>
static int t_fd = -1;
static std::string t_buf;
static bool t_keys[SDL_SCANCODE_COUNT];
static bool t_mouseSet;
static float t_mx, t_my;
static SDL_MouseButtonFlags t_buttons;
static std::string t_shot;
static float t_spin; // camera turn in degrees per second ("spin"), by wall time
static Uint64 t_spinLast;
static bool t_chase; // "chase 1": every frame turn to the nearest living enemy and walk (W) until in melee range
static int t_mob; // "mob n x y [hp]": n enemies pending around block column x, y (load test), hit points times hp (10000 = they survive); again = a new wave
static float t_mobHp = 10000.0f;
static std::vector<long long> t_living; // wave clones that can die (hp below 100): one lethal test hit about every 190 ms
// The incoming hits of the world update (ruckler_patch.py): the lethal hits, as the local Server makes them.
cube::ServerUpdates *testIncoming()
{
    static cube::ServerUpdates incoming;
    static Uint64 last;
    incoming.hits.clear();
    Uint64 now = SDL_GetTicksNS();
    if (t_living.empty() || !g_game->m_player || now - last < 190000000ull) return &incoming;
    last = now;
    cube::HitPacket hit;
    hit.attackerId = g_game->m_player->m_id;
    hit.targetId = t_living.back();
    hit.damage = 1e9f;
    t_living.pop_back();
    cube::Creature *target = g_game->m_world.creatureByID(hit.targetId);
    if (target) {
        hit.position = target->m_entityData.position;
        incoming.hits.push_back(hit);
    }
    return &incoming;
}
static int t_pinX, t_pinY; // "pin x y": the player goes to this column (after a connect the world is new) and stands
static bool t_pinWait;
static plasma::Vector<3, float> t_drift; // "drift vx vy z": the player flies at height z, blocks per second (new terrain)
static plasma::Vector<3, cube::Fixed> t_driftAt;
static bool t_driftOn;
static Uint64 t_driftLast;
static int t_mobX, t_mobY, t_mobDone;
static plasma::Vector<3, cube::Fixed> t_mobPin; // the player stays here, knockback or not
static bool t_god; // "god 1": the player's hit points are set to the maximum every frame (only the player)
static bool handleEvent(const SDL_Event &event);
static void testSpin();
static bool t_wander; // "wander SEED": walks, jumps, climbs, looks around and now and then fights, like a player
static unsigned t_wanderSeed;
// The top solid block of a column (-1: not loaded), and whether water lies on it.
static int testGround(int x, int y, bool *wet)
{
    cube::World &w = g_game->m_world;
    cube::Field *f = w.getField(x, y);
    if (!f || !f->size()) return -1;
    int top = f->getBaseZ() + f->size() - 1;
    cube::Zone *zone = w.zone(x / cube::BlocksPerZone, y / cube::BlocksPerZone);
    while (top > f->getBaseZ() && (w.getBlock(x, y, top, zone).type & cube::BlockTypeMask) == cube::BlockAir) top--;
    int here = w.getBlock(x, y, top, zone).type & cube::BlockTypeMask, above = w.getBlock(x, y, top + 1, zone).type & cube::BlockTypeMask;
    if (wet) *wet = here == cube::BlockWater || here == cube::BlockWet || above == cube::BlockWater;
    return top;
}
static void testInputPoll()
{
    testSpin();
    if (t_fd < 0) {
        const char *p = getenv("CUBE_TEST_FIFO");
        if (!p) return;
        t_fd = open(p, O_RDONLY | O_NONBLOCK);
        if (t_fd < 0) return;
    }
    char b[4096];
    ssize_t n;
    while ((n = read(t_fd, b, sizeof b)) > 0) t_buf.append(b, n);
    size_t nl;
    while ((nl = t_buf.find('\n')) != std::string::npos) {
        std::string line = t_buf.substr(0, nl);
        t_buf.erase(0, nl + 1);
        std::istringstream in(line);
        std::string c; in >> c;
        SDL_Event e; SDL_zero(e);
        if (c == "move") {
            in >> t_mx >> t_my; t_mouseSet = true;
        } else if (c == "down" || c == "up") {
            int b2; in >> b2;
            e.type = c == "down" ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.button = b2;
            SDL_MouseButtonFlags m = SDL_BUTTON_MASK(b2);
            if (c == "down") t_buttons |= m; else t_buttons &= ~m;
            handleEvent(e);
        } else if (c == "rel") {
            e.type = SDL_EVENT_MOUSE_MOTION; in >> e.motion.xrel >> e.motion.yrel; handleEvent(e);
        } else if (c == "wheel") {
            e.type = SDL_EVENT_MOUSE_WHEEL; in >> e.wheel.y; handleEvent(e);
        } else if (c == "kd" || c == "ku") {
            std::string name; in >> name;
            SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
            e.type = c == "kd" ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.scancode = sc; e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
            t_keys[sc] = c == "kd";
            handleEvent(e);
        } else if (c == "text") {
            std::string t; std::getline(in, t); if (!t.empty() && t[0] == ' ') t.erase(0, 1);
            static char tb[256]; SDL_strlcpy(tb, t.c_str(), sizeof tb);
            e.type = SDL_EVENT_TEXT_INPUT; e.text.text = tb; handleEvent(e);
        } else if (c == "pitch") {
            float pitch = 90.0f; in >> pitch; g_game->m_targetCameraRotation[0] = pitch; // 90 = level, less = down
        } else if (c == "facechar") {
            // The camera behind the player, looking where the character looks (W walks along the camera's
            // -y, the character faces its +y): yaw = character yaw - 180, at once and as the target.
            if (g_game->m_player) {
                float yaw = g_game->m_player->m_entityData.rotation[2] - 180.0f;
                g_game->m_targetCameraRotation[2] = yaw;
                g_game->m_cameraRotation[2] = yaw;
            }
        } else if (c == "faceenemy") {
            // The camera turned so that W walks straight at the nearest living hostile creature within 80
            // blocks (W walks along the camera's -y: rotZ(yaw) * (0, -1, 0) = (sin yaw, -cos yaw)).
            cube::Creature *player = g_game->m_player, *best = 0;
            float bestDistance = 80.0f * 80.0f;
            for (auto &entry : g_game->m_world.m_creatures) {
                cube::Creature *other = entry.second;
                if (!player || !other || other == player || !(other->m_entityData.hp > 0.0f) ||
                    !g_game->m_world.isHostile(player, other))
                    continue;
                plasma::Vector<3, float> d(other->m_entityData.position - player->m_entityData.position);
                float distance = d[0] * d[0] + d[1] * d[1];
                if (distance < bestDistance) { bestDistance = distance; best = other; }
            }
            if (best) {
                plasma::Vector<3, float> d(best->m_entityData.position - player->m_entityData.position);
                float yaw = atan2f(d[0], -d[1]) * 180.0f / 3.14159265f;
                g_game->m_targetCameraRotation[2] = yaw;
                g_game->m_cameraRotation[2] = yaw;
                fprintf(stderr, "FACEENEMY race=%d distance=%.1f yaw=%.1f\n", best->m_entityData.race, sqrtf(bestDistance), yaw);
            } else {
                fprintf(stderr, "FACEENEMY none within 80 blocks\n");
            }
        } else if (c == "yaw") {
            float yaw = 0.0f; in >> yaw; // a fixed camera turn (degrees), at once and as the target
            g_game->m_targetCameraRotation[2] = yaw;
            g_game->m_cameraRotation[2] = yaw;
        } else if (c == "camstate") {
            // camera turn and tilt, the player's position (blocks) and facing, for the log
            std::string tag; in >> tag;
            if (g_game->m_player) {
                plasma::Vector<3, float> pos(g_game->m_player->m_entityData.position);
                fprintf(stderr, "CAMSTATE %s yaw=%.2f pitch=%.2f x=%.2f y=%.2f z=%.2f facing=%.2f\n", tag.c_str(),
                        g_game->m_cameraRotation[2], g_game->m_cameraRotation[0], pos[0], pos[1], pos[2],
                        g_game->m_player->m_entityData.rotation[2]);
            }
        } else if (c == "worldhash") {
            // A hash of every loaded zone within RADIUS zones of the player (blocks of all columns with their
            // heights, all plants), one "WORLDHASH x y hash" line per zone, under the world's zone lock. With
            // the rand fix the generation is deterministic, so two builds must give the same lines for the
            // zones both have loaded (checks that compiler options do not change floating-point results).
            int radius = 6; in >> radius;
            cube::World &w = g_game->m_world;
            int px = (int)g_game->m_player->m_entityData.position[0] / cube::BlocksPerZone;
            int py = (int)g_game->m_player->m_entityData.position[1] / cube::BlocksPerZone;
            w.enterZones();
            int zones = 0;
            for (int zx = px - radius; zx <= px + radius; zx++)
                for (int zy = py - radius; zy <= py + radius; zy++) {
                    cube::Zone *zone = w.zone(zx, zy);
                    if (!zone || !zone->m_fields)
                        continue;
                    unsigned long long h = 1469598103934665603ull;
                    auto mix = [&](const void *d, size_t n) {
                        const unsigned char *b = (const unsigned char *)d;
                        for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
                    };
                    for (int i = 0; i < cube::BlocksPerZone * cube::BlocksPerZone; i++) {
                        cube::Field &f = zone->m_fields[i];
                        mix(&f.m_baseZ, sizeof f.m_baseZ); mix(&f.m_groundZ, sizeof f.m_groundZ); mix(&f.m_size, sizeof f.m_size);
                        for (int k = 0; k < f.m_size; k++) {
                            mix(&f.m_blocks[k].color, sizeof f.m_blocks[k].color); mix(&f.m_blocks[k].type, sizeof f.m_blocks[k].type);
                        }
                    }
                    for (auto &pl : zone->m_plants) {
                        mix(&pl.type, sizeof pl.type); mix(&pl.position, sizeof pl.position); mix(&pl.scale, sizeof pl.scale);
                        mix(&pl.rotation, sizeof pl.rotation); mix(&pl.color, sizeof pl.color); mix(&pl.flags, sizeof pl.flags);
                    }
                    size_t counts[3] = {zone->m_plants.size(), zone->m_objects.size(), zone->m_items.size()};
                    mix(counts, sizeof counts);
                    fprintf(stderr, "WORLDHASH %d %d %016llx\n", zx, zy, h);
                    zones++;
                }
            w.leaveZones();
            fprintf(stderr, "WORLDHASH zones=%d\n", zones);
        } else if (c == "chase") {
            int on = 0; in >> on; t_chase = on != 0;
            if (!t_chase) t_keys[SDL_SCANCODE_W] = t_keys[SDL_SCANCODE_S] = t_keys[SDL_SCANCODE_SPACE] = false;
        } else if (c == "mob") {
            in >> t_mob >> t_mobX >> t_mobY;
            if (!(in >> t_mobHp)) t_mobHp = 10000.0f;
        } else if (c == "terrain") {
            // A height map around the player, every STEP blocks: the top solid block, "w" when water lies on it.
            int radius = 160, step = 8; in >> radius >> step;
            cube::World &w = g_game->m_world;
            plasma::Vector<3, int> me = cube::toBlock(g_game->m_player->m_entityData.position);
            fprintf(stderr, "TERRAIN player %d %d %d radius=%d step=%d\n", me[0], me[1], me[2], radius, step);
            w.enterZones();
            for (int y = me[1] - radius; y <= me[1] + radius; y += step) {
                std::string row;
                for (int x = me[0] - radius; x <= me[0] + radius; x += step) {
                    bool wet = false;
                    int top = testGround(x, y, &wet);
                    row += " " + std::to_string(top) + (wet ? "w" : "");
                }
                fprintf(stderr, "TERRAIN y=%d:%s\n", y, row.c_str());
            }
            w.leaveZones();
        } else if (c == "god") {
            int on = 0; in >> on; t_god = on != 0;
        } else if (c == "spin") {
            in >> t_spin; t_spinLast = SDL_GetTicksNS();
        } else if (c == "wander") {
            in >> t_wanderSeed; t_wander = true;
            fprintf(stderr, "WANDER start seed=%u\n", t_wanderSeed);
        } else if (c == "winev") { // a window event as SDL would deliver it: focus, size
            std::string what; in >> what;
            e.type = what == "focus" ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
            e.window.windowID = SDL_GetWindowID(g_window);
            handleEvent(e);
            fprintf(stderr, "WINEV %s\n", what.c_str());
        } else if (c == "minimize") { // the real window manager call; the events come back through SDL
            SDL_MinimizeWindow(g_window);
            fprintf(stderr, "MINIMIZE\n");
        } else if (c == "opt") { // "opt KEY VALUE": an options.cfg line, read into the game's options at once
            std::string key, value; in >> key >> value;
            FILE *f = fopen("options.cfg", "a"); fprintf(f, "%s %s\n", key.c_str(), value.c_str()); fclose(f);
            g_game->m_options.load();
            fprintf(stderr, "OPT %s %s\n", key.c_str(), value.c_str());
        } else if (c == "connect") {
            std::string address; in >> address;
            g_game->connect(address);
            fprintf(stderr, "CONNECT %s client=%d\n", address.c_str(), (int)g_game->m_world.m_client);
        } else if (c == "pin") {
            in >> t_pinX >> t_pinY; t_pinWait = true;
        } else if (c == "drift") {
            float z = 0.0f; in >> t_drift[0] >> t_drift[1] >> z;
            t_driftAt = g_game->m_player->m_entityData.position; t_driftAt[2] = cube::Fixed(z);
            t_driftOn = true; t_driftLast = SDL_GetTicksNS();
            t_mobDone = 0; t_pinWait = false;
        } else if (c == "abold") {
            if (&g_abOld) in >> g_abOld;
        } else if (c == "freeze") {
            if (&g_abFreeze) in >> g_abFreeze;
        } else if (c == "fx") { // what the effects pass has to draw right now
            int clouds = 0, squares = 0, shots = 0;
            for (auto &p : g_game->m_world.m_projectiles) {
                shots++;
                if (p.projectileType != ProjectileCloud) continue;
                int side = 2 * (int)(p.sizeInBlocks + 1.0f) + 1;
                clouds++; squares += side * side;
            }
            fprintf(stderr, "FX particles=%d projectiles=%d clouds=%d squares=%d\n", (int)g_game->m_particles.size(), shots, clouds, squares);
        } else if (c == "shot") {
            in >> t_shot;
        } else if (c == "screen") {
            std::string path; in >> path;
            const char *name = g_game->m_startScreen->isVisible() ? "start"
                : g_game->m_characterListScreen->isVisible() ? "characters"
                : g_game->m_worldListScreen->isVisible() ? "worlds"
                : g_game->m_gameScreen->isVisible() ? "game" : "other";
            FILE *f = fopen((path + ".tmp").c_str(), "w"); fprintf(f, "%s\n", name); fclose(f);
            rename((path + ".tmp").c_str(), path.c_str());
        }
    }
}
// Combat (read only, every frame): hits on enemies (hit points of a hostile creature within 80 blocks
// dropping) and enemies defeated (hit points reaching 0); every 10 s "COMBAT enemies80=.. melee=.. (within 3
// blocks) hits=.. kills=..". With "chase 1" the camera turns each frame to the nearest living hostile creature
// within 80 blocks that is not deep in water, and W is held while it is farther than 2.5 blocks; W is let go
// while the player is deep in water (in water and not partly above it).
// Load test ("mob n x y"): the player goes to block column x, y and n copies of the scene's two enemy kinds
// (races 86 and 106, first of each by id, same behaviour) stand on three rings of 6, 9 and 12 blocks around it,
// the same list in every run. Their hit points are multiplied by 10000, so that they outlive the measuring
// window. Waits until both kinds exist.
static void testMob()
{
    cube::World &w = g_game->m_world;
    cube::Creature *player = g_game->m_player;
    static cube::Creature *kinds[2]; // copies out of the world: the living models may die between waves
    static int wave;
    if (!kinds[0] || !kinds[1]) {
        cube::Creature *found[2] = {0, 0};
        for (auto &entry : w.m_creatures) {
            cube::Creature *other = entry.second;
            if (!other || !other->m_behavior) continue;
            if (other->m_entityData.race == 86 && !found[0]) found[0] = other;
            if (other->m_entityData.race == 106 && !found[1]) found[1] = other;
        }
        if (!found[0] || !found[1]) return;
        for (int k = 0; k < 2; k++) {
            kinds[k] = new cube::Creature(0x7e00000000ll + k);
            kinds[k]->m_entityData = found[k]->m_entityData;
            kinds[k]->m_behavior = found[k]->m_behavior->clone();
            kinds[k]->m_despawnDistance = found[k]->m_despawnDistance;
            kinds[k]->m_specialSkills = found[k]->m_specialSkills;
            kinds[k]->m_summonAppearanceMode = found[k]->m_summonAppearanceMode;
            kinds[k]->m_summonRaces = found[k]->m_summonRaces;
        }
    }
    bool wet = false;
    int ground = testGround(t_mobX, t_mobY, &wet);
    if (ground < 0) { fprintf(stderr, "MOB Fehler: Spalte nicht geladen\n"); t_mob = 0; return; }
    t_mobPin = plasma::Vector<3, cube::Fixed>(plasma::Vector<3, int>(t_mobX, t_mobY, ground + 2));
    player->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
    int lowest = ground, highest = ground, wetColumns = wet ? 1 : 0;
    for (int i = 0; i < t_mob; i++) {
        cube::Creature *kind = kinds[i % 2];
        int ring = i / 10;
        float angle = ((i % 10) * 36.0f + ring * 12.0f) * 3.14159265f / 180.0f;
        float radius = 6.0f + 3.0f * (ring % 3);
        int x = t_mobX + (int)lroundf(cosf(angle) * radius), y = t_mobY + (int)lroundf(sinf(angle) * radius);
        bool columnWet = false;
        int z = testGround(x, y, &columnWet);
        if (z < lowest) lowest = z;
        if (z > highest) highest = z;
        if (columnWet) wetColumns++;
        long long id = 0x7f00000000ll + wave * 1000ll + i;
        cube::Creature *mob = new cube::Creature(id);
        mob->m_entityData = kind->m_entityData;
        mob->m_id = id;
        mob->m_entityData.position = plasma::Vector<3, cube::Fixed>(plasma::Vector<3, int>(x, y, z + 2));
        mob->m_entityData.origin = mob->m_entityData.position;
        mob->m_entityData.multipliers.hp *= t_mobHp;
        mob->m_entityData.hp = mob->maxHP();
        mob->m_behavior = kind->m_behavior->clone();
        mob->m_despawnDistance = kind->m_despawnDistance;
        mob->m_specialSkills = kind->m_specialSkills;
        mob->m_summonAppearanceMode = kind->m_summonAppearanceMode;
        mob->m_summonRaces = kind->m_summonRaces;
        w.m_creatures[id] = mob;
        if (t_mobHp < 100.0f) t_living.push_back(id);
    }
    fprintf(stderr, "MOB n=%d x=%d y=%d ground=%d spanne=%d..%d nass=%d hp=%.0f\n", t_mob, t_mobX, t_mobY, ground, lowest,
            highest, wetColumns, w.m_creatures[0x7f00000000ll + wave * 1000ll]->m_entityData.hp);
    wave++;
    t_mobDone = t_mob;
    t_mob = 0;
}
static void testCombat()
{
    static std::map<long long, float> lastHp;
    static long long hits, kills, meleeFrames;
    static int meleePeak;
    static Uint64 lastReport;
    cube::Creature *player = g_game->m_player;
    if (!player) return;
    if (t_mob) testMob();
    if (t_pinWait) {
        bool wet = false;
        int ground = testGround(t_pinX, t_pinY, &wet);
        t_mobPin = plasma::Vector<3, cube::Fixed>(plasma::Vector<3, int>(t_pinX, t_pinY, ground < 0 ? 300 : ground + 2));
        t_mobDone = 1;
        if (ground >= 0) { t_pinWait = false; fprintf(stderr, "PIN x=%d y=%d ground=%d nass=%d\n", t_pinX, t_pinY, ground, (int)wet); }
    }
    if (t_mobDone) {
        player->m_entityData.position = t_mobPin;
        player->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
    }
    std::map<long long, float> hp;
    cube::Creature *best = 0;
    float bestDistance = 80.0f * 80.0f;
    int enemies = 0, melee = 0, near12 = 0;
    auto deep = [](cube::Creature *c) {
        unsigned flags = c->m_entityData.flags;
        return (flags & (1 << cube::CreatureFlagInWater)) && !(flags & (1 << cube::CreatureFlagPartlyAboveWater));
    };
    for (auto &entry : g_game->m_world.m_creatures) {
        cube::Creature *other = entry.second;
        if (!other || other == player || !g_game->m_world.isHostile(player, other)) continue;
        plasma::Vector<3, float> d(other->m_entityData.position - player->m_entityData.position);
        float distance = d[0] * d[0] + d[1] * d[1];
        if (distance >= 80.0f * 80.0f) continue;
        float now = other->m_entityData.hp;
        auto was = lastHp.find(entry.first);
        if (was != lastHp.end()) {
            if (now < was->second) hits++;
            if (was->second > 0.0f && !(now > 0.0f)) kills++;
        }
        hp[entry.first] = now;
        if (!(now > 0.0f)) continue;
        enemies++;
        if (distance < 3.0f * 3.0f) melee++;
        if (distance < 12.0f * 12.0f) near12++;
        // A creature far up the mountain cannot be walked to; one below can.
        if (distance < bestDistance && !deep(other) && d[2] < 8.0f) { bestDistance = distance; best = other; }
    }
    lastHp.swap(hp);
    if (melee > meleePeak) meleePeak = melee;
    if (melee > 0) meleeFrames++;
    if (t_chase) {
        bool walk = false;
        if (best) {
            plasma::Vector<3, float> d(best->m_entityData.position - player->m_entityData.position);
            float yaw = atan2f(d[0], -d[1]) * 180.0f / 3.14159265f;
            g_game->m_targetCameraRotation[2] = yaw;
            g_game->m_cameraRotation[2] = yaw;
            walk = bestDistance > 2.5f * 2.5f && !deep(player) && !t_mobDone; // the load test stands in the middle
            // Near the target the camera also tilts towards it (90 = level, less = down), so that attacks aim at
            // an enemy above or below; farther away it keeps the tilt of the run.
            static float runPitch = g_game->m_targetCameraRotation[0];
            float pitch = runPitch;
            if (bestDistance < 8.0f * 8.0f)
                pitch = 90.0f + atan2f(d[2], sqrtf(bestDistance)) * 180.0f / 3.14159265f;
            g_game->m_targetCameraRotation[0] = pitch;
            g_game->m_cameraRotation[0] = pitch;
        }
        // Close to the target and no enemy hurt for 3 s (a creature just below an edge, out of the spells' way):
        // back off for a second.
        static long long lastHits;
        static Uint64 lastDamage, retreatUntil;
        Uint64 clock = SDL_GetTicksNS();
        if (hits != lastHits || !lastDamage) { lastHits = hits; lastDamage = clock; }
        if (best && bestDistance < 4.0f * 4.0f && clock - lastDamage > 3000000000ull) {
            retreatUntil = clock + 1000000000ull;
            lastDamage = clock;
        }
        bool retreat = clock < retreatUntil && !t_mobDone;
        t_keys[SDL_SCANCODE_S] = retreat;
        if (retreat) walk = false;
        t_keys[SDL_SCANCODE_W] = walk;
        // Against a wall on the way: jump, the key going down and up every quarter second.
        unsigned flags = player->m_entityData.flags;
        bool blocked = flags & ((1 << cube::CreatureFlagAtWall) | (1 << cube::CreatureFlagMovementBlocked));
        t_keys[SDL_SCANCODE_SPACE] = walk && blocked && (SDL_GetTicksNS() / 250000000ull) % 2 == 0;
    }
    Uint64 ticks = SDL_GetTicksNS();
    if (ticks - lastReport >= 10000000000ull) {
        lastReport = ticks;
        plasma::Vector<3, float> me(player->m_entityData.position);
        fprintf(stderr, "COMBAT t=%llu enemies80=%d near12=%d melee=%d meleePeak=%d meleeFrames=%lld hits=%lld kills=%lld chase=%d player z=%.1f flags=%u target=%s dist=%.1f\n",
                (unsigned long long)(ticks / 1000000), enemies, near12, melee, meleePeak, meleeFrames, hits, kills, (int)t_chase, me[2],
                player->m_entityData.flags, best ? "ja" : "keins", best ? sqrtf(bestDistance) : 0.0f);
        {
            float toTarget = 0.0f;
            if (best) {
                plasma::Vector<3, float> d(best->m_entityData.position - player->m_entityData.position);
                toTarget = atan2f(-d[0], d[1]) * 180.0f / 3.14159265f; // the character faces its +y
            }
            fprintf(stderr, "ATTACK class=%d ability=%d since=%d facing=%.1f toTarget=%.1f width=%.2f camyaw=%.1f campitch=%.1f\n",
                    (int)player->m_entityData.classType, (int)player->m_entityData.currentAbility,
                    (int)player->m_entityData.timeSinceAbility, player->m_entityData.rotation[2], toTarget,
                    player->m_entityData.appearance.size[0], g_game->m_cameraRotation[2], g_game->m_cameraRotation[0]);
        }
        for (auto &entry : g_game->m_world.m_creatures) {
            cube::Creature *other = entry.second;
            if (!other || other == player || !g_game->m_world.isHostile(player, other) || !(other->m_entityData.hp > 0.0f)) continue;
            plasma::Vector<3, float> d(other->m_entityData.position - player->m_entityData.position);
            if (d[0] * d[0] + d[1] * d[1] >= 80.0f * 80.0f) continue;
            fprintf(stderr, "ENEMY id=%lld race=%d hp=%.1f dx=%.1f dy=%.1f dz=%.1f flags=%u deep=%d\n", entry.first,
                    other->m_entityData.race, other->m_entityData.hp, d[0], d[1], d[2], other->m_entityData.flags, (int)deep(other));
        }
    }
}
// WANDER (test only): a player's day out, from its own random numbers (never the game's rand()). Segments of
// 1.5-5 s: straight on, a slow look around (40-120 deg/s), or a quick swing of 90 or 180 deg (720 deg/s); mostly
// forward, sometimes sideways or back; a jump every 0.8-3 s, held at a wall (climbing); turned away when stuck.
// Every 25-60 s a fight of 10 s: "chase" to the nearest enemy, the left button 1.5 s down, 0.1 s up.
static unsigned wanderRand() { t_wanderSeed = t_wanderSeed * 1664525u + 1013904223u; return t_wanderSeed >> 8; }
static float wanderUnit() { return (float)(wanderRand() & 0xffff) / 65536.0f; }
static void wanderButton(bool down)
{
    SDL_Event e; SDL_zero(e);
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = 1;
    if (down) t_buttons |= SDL_BUTTON_MASK(1); else t_buttons &= ~SDL_BUTTON_MASK(1);
    handleEvent(e);
}
static void testWander()
{
    static Uint64 last, segmentEnd, jumpAt, jumpUp, fightEnd, nextFight, clickAt, stuckCheck;
    static float yawRate, flickLeft, pitchTarget;
    static int keyMode;
    static bool buttonDown;
    static plasma::Vector<3, float> stuckFrom;
    cube::Creature *player = g_game->m_player;
    if (!player) return;
    Uint64 now = SDL_GetTicksNS();
    if (!last) { last = now; nextFight = now + 30000000000ull; stuckCheck = now; stuckFrom = plasma::Vector<3, float>(player->m_entityData.position); }
    float dt = (float)((now - last) / 1e9); last = now;
    if (dt > 0.1f) dt = 0.1f;
    if (!fightEnd && now >= nextFight) {
        t_chase = true; fightEnd = now + 10000000000ull; clickAt = now;
        t_keys[SDL_SCANCODE_A] = t_keys[SDL_SCANCODE_D] = false;
        fprintf(stderr, "WANDER kampf t=%llu\n", (unsigned long long)(now / 1000000));
    }
    if (fightEnd) {
        if (now < fightEnd) {
            if (now >= clickAt) { wanderButton(!buttonDown); buttonDown = !buttonDown; clickAt = now + (buttonDown ? 1500000000ull : 100000000ull); }
            return;
        }
        if (buttonDown) { wanderButton(false); buttonDown = false; }
        t_chase = false; fightEnd = 0;
        t_keys[SDL_SCANCODE_W] = t_keys[SDL_SCANCODE_S] = t_keys[SDL_SCANCODE_SPACE] = false;
        nextFight = now + (Uint64)((25.0f + 35.0f * wanderUnit()) * 1e9f);
        segmentEnd = 0;
    }
    if (now >= segmentEnd) {
        segmentEnd = now + (Uint64)((1.5f + 3.5f * wanderUnit()) * 1e9f);
        float kind = wanderUnit(), side = wanderUnit() < 0.5f ? -1.0f : 1.0f;
        yawRate = 0.0f; flickLeft = 0.0f;
        if (kind < 0.25f) {}
        else if (kind < 0.6f) yawRate = side * (40.0f + 80.0f * wanderUnit());
        else if (kind < 0.85f) flickLeft = side * 180.0f;
        else flickLeft = side * 90.0f;
        pitchTarget = 70.0f + 35.0f * wanderUnit();
        float k = wanderUnit();
        keyMode = k < 0.75f ? 0 : k < 0.85f ? 1 : k < 0.95f ? 2 : 3; // forward, forward left, forward right, back
    }
    if (now - stuckCheck > 3000000000ull) {
        plasma::Vector<3, float> here(player->m_entityData.position);
        float dx = here[0] - stuckFrom[0], dy = here[1] - stuckFrom[1];
        if (dx * dx + dy * dy < 2.0f * 2.0f) { flickLeft = 150.0f; jumpAt = now; fprintf(stderr, "WANDER fest\n"); }
        stuckFrom = here; stuckCheck = now;
    }
    float step = yawRate * dt;
    if (flickLeft != 0.0f) {
        float most = 720.0f * dt;
        step = flickLeft > 0.0f ? std::min(flickLeft, most) : std::max(flickLeft, -most);
        flickLeft -= step;
    }
    g_game->m_targetCameraRotation[2] += step;
    float &pitch = g_game->m_targetCameraRotation[0];
    float pitchStep = 60.0f * dt;
    pitch += std::max(-pitchStep, std::min(pitchStep, pitchTarget - pitch));
    t_keys[SDL_SCANCODE_W] = keyMode != 3;
    t_keys[SDL_SCANCODE_A] = keyMode == 1;
    t_keys[SDL_SCANCODE_D] = keyMode == 2;
    t_keys[SDL_SCANCODE_S] = keyMode == 3;
    unsigned flags = player->m_entityData.flags;
    bool wall = flags & ((1 << cube::CreatureFlagAtWall) | (1 << cube::CreatureFlagMovementBlocked));
    if (now >= jumpAt) { jumpUp = now + 200000000ull; jumpAt = now + (Uint64)((0.8f + 2.2f * wanderUnit()) * 1e9f); }
    t_keys[SDL_SCANCODE_SPACE] = now < jumpUp || wall;
}
static void testSpin()
{
    if (t_driftOn && g_game->m_player) {
        Uint64 now = SDL_GetTicksNS();
        float seconds = (float)((now - t_driftLast) / 1e9);
        t_driftLast = now;
        t_driftAt[0] = t_driftAt[0] + t_drift[0] * seconds;
        t_driftAt[1] = t_driftAt[1] + t_drift[1] * seconds;
        g_game->m_player->m_entityData.position = t_driftAt;
        g_game->m_player->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
    }
    if (t_god && g_game->m_player && g_game->m_player->m_entityData.hp > 0.0f) {
        // Every hit on the player shows as hit points below the maximum: logged (when the enemies attack).
        static int hits;
        float maximum = g_game->m_player->maxHP();
        if (g_game->m_player->m_entityData.hp < maximum)
            fprintf(stderr, "PLAYERHIT n=%d t=%llu\n", ++hits, (unsigned long long)(SDL_GetTicksNS() / 1000000));
        g_game->m_player->m_entityData.hp = maximum;
    }
    if (t_wander) testWander();
    testCombat();
    if (t_spin == 0.0f) return;
    Uint64 now = SDL_GetTicksNS();
    g_game->m_targetCameraRotation[2] -= t_spin * (float)((now - t_spinLast) / 1e9);
    t_spinLast = now;
}
static bool testKey(const bool *keys, SDL_Scancode sc) { return keys[sc] || t_keys[sc]; }
// Frame times (once per loop): every 10 s count, mean, max, >33/>50 ms.
static void testFrameStats()
{
    static Uint64 last = 0, windowStart = 0;
    static int frames = 0, over33 = 0, over50 = 0;
    static double sum = 0, worst = 0;
    Uint64 now = SDL_GetTicksNS();
    if (last) {
        double ms = (now - last) / 1e6;
        frames++; sum += ms; if (ms > worst) worst = ms;
        if (ms > 33) over33++;
        if (ms > 50) over50++;
    } else windowStart = now;
    last = now;
    if (now - windowStart >= 10000000000ull) {
        fprintf(stderr, "FRAMES n=%d mean=%.2fms max=%.1fms over33=%d over50=%d creatures=%d\n", frames, frames ? sum / frames : 0, worst, over33, over50, (int)g_game->m_world.m_creatures.size());
        windowStart = now; frames = over33 = over50 = 0; sum = worst = 0;
    }
}
// Saves the back buffer (resolved, before Present) as a PPM file.
static void testShot()
{
    if (t_shot.empty()) return;
    std::string path = t_shot; t_shot.clear();
    IDirect3DSurface9 *bb = 0, *rt = 0, *sys = 0;
    if (g_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb) != D3D_OK) return;
    D3DSURFACE_DESC d; bb->GetDesc(&d);
    g_device->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, 0);
    g_device->StretchRect(bb, 0, rt, 0, D3DTEXF_NONE);
    g_device->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, 0);
    if (rt && sys && g_device->GetRenderTargetData(rt, sys) == D3D_OK) {
        D3DLOCKED_RECT r;
        if (sys->LockRect(&r, 0, D3DLOCK_READONLY) == D3D_OK) {
            FILE *f = fopen(path.c_str(), "wb");
            fprintf(f, "P6\n%u %u\n255\n", d.Width, d.Height);
            for (UINT y = 0; y < d.Height; y++) {
                unsigned char *row = (unsigned char *)r.pBits + y * r.Pitch;
                for (UINT x = 0; x < d.Width; x++) { unsigned char px[3] = {row[x*4+2], row[x*4+1], row[x*4]}; fwrite(px, 1, 3, f); }
            }
            fclose(f);
            sys->UnlockRect();
        }
    }
    if (sys) sys->Release(); if (rt) rt->Release(); bb->Release();
}

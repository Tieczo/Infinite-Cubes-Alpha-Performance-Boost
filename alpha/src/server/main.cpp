#include "cube/WorldDimensions.h"
#include "cube/GameTime.h"
// The dedicated server initializes Winsock, the world and the socket server, then runs fixed-paced updates until q is entered. A worker generates nearby zones and unloads unused ones.
#include "cube/Server.h"
#include "cube/DefaultBlocks.h"
#include "cube/World.h"
#include "cube/Creature.h"
#include "cube/CreatureIds.h"
#include "Thread.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <sstream>
#include <mmsystem.h>

using namespace cube;
// A new seed is below SeedRange; the world steps every UpdateDelay milliseconds.
namespace {
enum {
    SeedRange = 1000000
};
enum {
    UpdateDelay = 20
};
// After a pass that generated no zone, the generation thread sleeps this long instead of spinning on a core (not in
// the original; idea from SirLefti's Cube-World-Server-Idle-CPU-Fix). Passes that generate a zone do not wait.
enum {
    GenerationIdleSleep = 10
};

// The settings after the seed in server.cfg (not in the original), one "key value" per line in any order; a missing,
// unknown or invalid line keeps the original's value, so a server.cfg with only the seed works as before. Read again
// once a second.
//   creatureDistance N       100 to 400 percent, default 100
//   guardianReliableBlock N  1: blocked hits cost a guardian warrior no block power, default 0
struct ServerSettings {
    int creatureDistance = 100;
    bool guardianReliableBlock = false;
};

ServerSettings readSettings()
{
    ServerSettings settings;
    std::ifstream config("server.cfg");
    std::string line;
    std::getline(config, line); // the seed
    while (std::getline(config, line)) {
        std::istringstream fields(line);
        std::string key;
        int value;
        if (!(fields >> key >> value))
            continue;
        if (key == "creatureDistance" && value >= 100 && value <= 400)
            settings.creatureDistance = value;
        else if (key == "guardianReliableBlock" && (value == 0 || value == 1))
            settings.guardianReliableBlock = value != 0;
    }
    return settings;
}

// The squared zone distance below which zones are generated and kept around a player: 9 as in the original,
// more with a larger creature distance so that the farther spawns have their zones.
int zoneDistanceFor(int creatureDistance)
{
    int radius = 3;
    if (creatureDistance > 100)
        radius += (150 * (creatureDistance - 100) + 100 * BlocksPerZone - 1) / (100 * BlocksPerZone);
    return radius * radius;
}
}

int main()
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    World *world = new World(0, 0);

    // The seed is kept in server.cfg; the first start draws one and writes it.
    int seed = rand() % SeedRange;
    std::ifstream configIn("server.cfg");
    if (configIn.good()) {
        configIn >> seed;
    } else {
        std::ofstream configOut("server.cfg");
        configOut << seed;
    }
    std::stringstream name;
    name << "server_" << seed;
    world->setup(seed, name.str());
    world->m_spriteManager.loadSprites("");

    Server *server = new Server(world);

    ServerSettings settings = readSettings();
    int creatureDistance = settings.creatureDistance;
    world->m_creatureDistanceScale = creatureDistance / 100.0f;
    world->m_guardianReliableBlock = settings.guardianReliableBlock;
    std::atomic<int> zoneDistance(zoneDistanceFor(creatureDistance));
    std::cout << "creature distance " << creatureDistance << "%" << std::endl;
    std::cout << "guardian reliable block " << (settings.guardianReliableBlock ? "on" : "off") << std::endl;

    // The zones the players are in, collected by the update loop for the generation thread.
    CRITICAL_SECTION playerZonesLock;
    InitializeCriticalSectionAndSpinCount(&playerZonesLock,
                                          0x80000400); // 1024 spins; the high bit preallocates the event
    std::list<plasma::Vector<2, int>> playerZones;
    bool generating = true;
    HANDLE generationThread = StartThread([world, server, &generating, &playerZonesLock, &playerZones,
                                           &zoneDistance]() {
        int lastCleanup = gameTime();
        while (generating) {
            EnterCriticalSection(&playerZonesLock);
            std::list<plasma::Vector<2, int>> zones(playerZones);
            if (server->getSeed() != world->getSeed() || server->getName() != world->getName()) {
                // The seed or the name changed: unload everything and set the world up again.
                world->enterRegions();
                for (int x = 0; x < 0x400; x++) {
                    for (int y = 0; y < 0x400; y++) {
                        Region *region = world->getRegion(x, y);
                        if (!region)
                            continue;
                        for (int i = 0; i < 64; i++) {
                            for (int j = 0; j < 64; j++) {
                                Zone *zone = region->m_zones[i][j];
                                if (zone)
                                    world->removeZone(zone->m_position[0], zone->m_position[1]);
                            }
                        }
                        world->removeRegion(x, y);
                        world->removeRecord(x, y);
                    }
                }
                world->setup(server->getSeed(), server->getName());
                world->leaveRegions();
                LeaveCriticalSection(&playerZonesLock);
                continue;
            }
            LeaveCriticalSection(&playerZonesLock);

            // Generate the missing zone nearest to a player (squared distance below 9 zones, more with a larger
            // creature distance).
            int keepDistance = zoneDistance.load();
            bool found = false;
            int bestDistance = keepDistance;
            plasma::Vector<2, int> best;
            for (std::list<plasma::Vector<2, int>>::iterator it = zones.begin(); it != zones.end(); ++it) {
                plasma::Vector<2, int> center = *it;
                int minX = center[0] - bestDistance;
                int maxX = center[0] + bestDistance;
                int minY = center[1] - bestDistance;
                int maxY = center[1] + bestDistance;
                if (minX < 0)
                    minX = 0;
                if (maxX >= 0x10000)
                    maxX = 0xffff;
                if (minY < 0)
                    minY = 0;
                if (maxY >= 0x10000)
                    maxY = 0xffff;
                for (int x = minX; x <= maxX; x++) {
                    for (int y = minY; y <= maxY; y++) {
                        int distance = (center - plasma::Vector<2, int>(x, y)).squaredLength();
                        if (distance < bestDistance && !world->zone(x, y)) {
                            best = plasma::Vector<2, int>(x, y);
                            bestDistance = distance;
                            found = true;
                        }
                    }
                }
            }
            if (found)
                world->generateZone(best[0], best[1]);
            else
                Sleep(GenerationIdleSleep);

            // Once a second, unload the zones, regions and records that no player is near.
            int now = gameTime();
            if (now - lastCleanup > 1000) {
                lastCleanup = now;
                for (int x = 0; x < 0x400; x++) {
                    for (int y = 0; y < 0x400; y++) {
                        Region *region = world->getRegion(x, y);
                        if (!region)
                            continue;
                        for (int i = 0; i < 64; i++) {
                            for (int j = 0; j < 64; j++) {
                                Zone *zone = region->m_zones[i][j];
                                if (!zone)
                                    continue;
                                bool used = false;
                                for (std::list<plasma::Vector<2, int>>::iterator it = zones.begin(); it != zones.end();
                                     ++it) {
                                    plasma::Vector<2, int> offset = zone->m_position - *it;
                                    if (offset.squaredLength() < keepDistance) {
                                        used = true;
                                        break;
                                    }
                                }
                                if (!used)
                                    world->removeZone(zone->m_position[0], zone->m_position[1]);
                            }
                        }
                        // The regions within 2 regions of a player's zone stay, and the records within 4.
                        bool regionUsed = false;
                        for (std::list<plasma::Vector<2, int>>::iterator it = zones.begin(); it != zones.end(); ++it) {
                            if (abs(x - (*it)[0] / ZonesPerRegion) <= 2 && abs(y - (*it)[1] / ZonesPerRegion) <= 2) {
                                regionUsed = true;
                                break;
                            }
                        }
                        if (!regionUsed)
                            world->removeRegion(x, y);
                        bool recordUsed = false;
                        for (std::list<plasma::Vector<2, int>>::iterator it = zones.begin(); it != zones.end(); ++it) {
                            if (abs(x - (*it)[0] / ZonesPerRegion) <= 4 && abs(y - (*it)[1] / ZonesPerRegion) <= 4) {
                                recordUsed = true;
                                break;
                            }
                        }
                        if (!recordUsed)
                            world->removeRecord(x, y);
                    }
                }
            }
        }
    });
    SetThreadPriority(generationThread, THREAD_PRIORITY_BELOW_NORMAL);
    server->start();

    timeBeginPeriod(1);
    int lastTime = gameTime();
    int lastConfigRead = lastTime;
    bool running = true;
    StartThread([&running]() {
        char key;
        // The console is read once a second.
        do {
            Sleep(1000);
            std::cin >> key;
        } while (key != 'q');
        running = false;
    });

    while (running) {
        int now = gameTime();
        ServerSettings newSettings = settings;
        if (now - lastConfigRead >= 1000) {
            lastConfigRead = now;
            newSettings = readSettings();
        }
        ServerUpdates incoming;
        server->takeHits(incoming.hits);
        server->takeBuffs(incoming.buffs);
        world->enterRegions();
        if (newSettings.guardianReliableBlock != settings.guardianReliableBlock) {
            world->m_guardianReliableBlock = newSettings.guardianReliableBlock;
            std::cout << "guardian reliable block " << (newSettings.guardianReliableBlock ? "on" : "off") << std::endl;
        }
        if (newSettings.creatureDistance != creatureDistance) {
            creatureDistance = newSettings.creatureDistance;
            world->m_creatureDistanceScale = creatureDistance / 100.0f;
            zoneDistance = zoneDistanceFor(creatureDistance);
            std::cout << "creature distance " << creatureDistance << "%" << std::endl;
        }
        settings = newSettings;
        ServerUpdates outgoing;
        world->update(now - lastTime, &outgoing, &incoming);
        lastTime = now;
        server->takeBullets(outgoing.projectiles);
        server->addServerUpdates(outgoing);

        EnterCriticalSection(&playerZonesLock);
        playerZones.clear();
        for (std::map<long long, Creature *>::iterator it = world->m_creatures.begin(); it != world->m_creatures.end();
             ++it) {
            Creature *creature = it->second;
            if (creature && creature->m_entityData.hostilityType == HostilityPlayer) {
                const plasma::Vector<3, Fixed> &position = creature->m_entityData.position;
                playerZones.push_back(plasma::Vector<2, int>(position[0], position[1]) / BlocksPerZone);
            }
        }
        LeaveCriticalSection(&playerZonesLock);
        world->leaveRegions();
        Sleep(UpdateDelay);
    }
    timeEndPeriod(1);

    std::cout << "stopping generation thread..." << std::endl;
    generating = false;
    WaitForSingleObject(generationThread, INFINITE);
    delete server;
    delete world;
    DeleteCriticalSection(&playerZonesLock);
    WSACleanup();
    return 0;
}

// The client uses Server for local update queues. The dedicated server also manages sockets, connection threads and player sessions.
#include "cube/Server.h"
#include "cube/HitPacket.h"
#include "cube/BuffPacket.h"
#include "cube/BulletPacket.h"
#include "cube/DefaultBlocks.h"
#include "cube/World.h"
#include "cube/Creature.h"
#include "cube/CreatureIds.h"
#include "cube/RaceIds.h"
#include "cube/BytesIO.h"
#include "cube/Network.h"
#include "Thread.h"
#include <iostream>
#include <map>
#include <mmsystem.h>

namespace cube {
// The listen backlog (anonymous: this file's own). The accept and send threads' values stay
// literals: a lambda's closure name follows its body.
namespace {
enum {
    ListenBacklog = 10
};
}

#ifdef CUBE_SERVER
// Writes a list's size and its elements' bytes.
template<class T> void writeList(BytesIO &data, const std::list<T> &list)
{
    int count = list.size();
    data.write(&count, sizeof(count));
    for (typename std::list<T>::const_iterator it = list.begin(); it != list.end(); ++it)
        data.write(&*it, sizeof(T));
}

Connection::Connection()
{
    InitializeCriticalSectionAndSpinCount(&lock, 0x80000400);
}

Connection::~Connection()
{
    DeleteCriticalSection(&lock);
}

Server::Server(World *world)
    : m_world(world),
      m_running(false)
{
    m_seed = world->m_seed;
    m_name = world->m_name;
}

Server::~Server()
{
    std::cout << "Stopping server..." << std::endl;
    m_running = false;
    closesocket(m_socket);
    WaitForSingleObject(m_acceptThread, INFINITE);
}

#endif

// Appends a tick's world events to what each player still has to receive (particles and sounds
// only within 200 blocks of the player's creature), then the contents of the zones the player
// asked for and the mission state of the zones the player dropped. A player without a creature is
// a null read in the code.
void Server::addServerUpdates(const ServerUpdates &updates)
{
    m_world->enterRegions();
    for (int i = 0; i < (int)m_connections.size(); i++) {
        if (!m_connections[i])
            continue;
        m_connections[i]->updates.blocks.insert(m_connections[i]->updates.blocks.end(), updates.blocks.begin(),
                                                updates.blocks.end());
        m_connections[i]->updates.hits.insert(m_connections[i]->updates.hits.end(), updates.hits.begin(),
                                              updates.hits.end());
        Creature *creature = m_world->creatureByID(m_connections[i]->creatureId);
        for (std::list<ServerUpdates::ParticlePacket>::const_iterator particle = updates.particles.begin();
             particle != updates.particles.end(); ++particle) {
            plasma::Vector<3, float> offset(particle->position - creature->m_entityData.position);
            if (offset.squaredLength() < 40000.0f)
                m_connections[i]->updates.particles.push_back(*particle);
        }
        for (std::list<ServerUpdates::SoundPacket>::const_iterator sound = updates.sounds.begin();
             sound != updates.sounds.end(); ++sound) {
            plasma::Vector<3, float> offset =
                sound->position - plasma::Vector<3, float>(creature->m_entityData.position);
            if (offset.squaredLength() < 40000.0f)
                m_connections[i]->updates.sounds.push_back(*sound);
        }
        m_connections[i]->updates.projectiles.insert(m_connections[i]->updates.projectiles.end(),
                                                     updates.projectiles.begin(), updates.projectiles.end());
        m_connections[i]->updates.items.insert(m_connections[i]->updates.items.end(), updates.items.begin(),
                                               updates.items.end());
        m_connections[i]->updates.objects.insert(m_connections[i]->updates.objects.end(), updates.objects.begin(),
                                                 updates.objects.end());
        m_connections[i]->updates.buildings.insert(m_connections[i]->updates.buildings.end(), updates.buildings.begin(),
                                                   updates.buildings.end());
        m_connections[i]->updates.pickedUpItems.insert(m_connections[i]->updates.pickedUpItems.end(),
                                                       updates.pickedUpItems.begin(), updates.pickedUpItems.end());
        m_connections[i]->updates.kills.insert(m_connections[i]->updates.kills.end(), updates.kills.begin(),
                                               updates.kills.end());
        m_connections[i]->updates.aggro.insert(m_connections[i]->updates.aggro.end(), updates.aggro.begin(),
                                               updates.aggro.end());
        m_connections[i]->updates.buffs.insert(m_connections[i]->updates.buffs.end(), updates.buffs.begin(),
                                               updates.buffs.end());
        m_connections[i]->updates.missions.insert(m_connections[i]->updates.missions.end(), updates.missions.begin(),
                                                  updates.missions.end());

        EnterCriticalSection(&m_connections[i]->lock);
        std::list<plasma::Vector<2, int>> zones(m_connections[i]->zones);
        std::list<plasma::Vector<2, int>> regionRequests(m_connections[i]->regionRequests);
        m_connections[i]->zones.clear();
        m_connections[i]->regionRequests.clear();
        LeaveCriticalSection(&m_connections[i]->lock);

        // A requested zone: its block changes, items and objects.
        for (std::list<plasma::Vector<2, int>>::iterator it = zones.begin(); it != zones.end(); ++it) {
            m_world->enterRegions();
            Zone *zone = m_world->zone((*it)[0], (*it)[1]);
            if (zone) {
                m_connections[i]->updates.blocks.insert(m_connections[i]->updates.blocks.end(),
                                                        zone->m_blockChanges.begin(), zone->m_blockChanges.end());
                m_connections[i]->updates.items.push_back(ServerUpdates::ItemListPacket(*zone));
                for (int index = 0; index < (int)zone->m_objects.size(); index++) {
                    ServerUpdates::ZoneObjectPacket object(plasma::Vector<3, int>((*it)[0], (*it)[1], index),
                                                           zone->m_objects[index]);
                    m_connections[i]->updates.objects.push_back(object);
                }
            }
            m_world->leaveRegions();
        }
        // A requested region: the mission state of its 8 x 8 records.
        for (std::list<plasma::Vector<2, int>>::iterator it = regionRequests.begin(); it != regionRequests.end();
             ++it) {
            m_world->enterRegions();
            for (int x = 0; x < 8; x++) {
                for (int y = 0; y < 8; y++) {
                    plasma::Vector<2, int> position((*it)[0] * 64 / 8 + x, (*it)[1] * 64 / 8 + y);
                    Region::Record *record = m_world->getRecord(position[0], position[1]);
                    if (record) {
                        ServerUpdates::MissionPacket mission;
                        mission.position = position;
                        mission.mission = record->mission;
                        m_connections[i]->updates.missions.push_back(mission);
                    }
                }
            }
            m_world->leaveRegions();
        }
    }
    m_world->leaveRegions();
}

// The main loop takes the packets the receiving threads queued: hits and buffs for the world's
// update, projectiles for the next broadcast.
void Server::takeBuffs(std::list<BuffPacket> &buffs)
{
    m_world->enterRegions();
    buffs.insert(buffs.end(), m_buffs.cbegin(), m_buffs.cend());
    m_buffs.clear();
    m_world->leaveRegions();
}

void Server::takeBullets(std::list<BulletPacket> &bullets)
{
    m_world->enterRegions();
    bullets.insert(bullets.end(), m_projectiles.cbegin(), m_projectiles.cend());
    m_projectiles.clear();
    m_world->leaveRegions();
}

void Server::takeHits(std::list<HitPacket> &hits)
{
    m_world->enterRegions();
    hits.insert(hits.end(), m_hits.cbegin(), m_hits.cend());
    m_hits.clear();
    m_world->leaveRegions();
}

#ifdef CUBE_SERVER

int receiveAll(SOCKET socket, char *buffer, int length, int flags)
{
    int originalLength = length;
    while (length > 0) {
        int received = recv(socket, buffer, length, flags);
        if (received <= 0)
            return received;
        length -= received;
        buffer += received;
    }
    return originalLength;
}

// Opens the listening socket on port 12345 and starts the thread that accepts the players. Each
// player gets a creature, a connection slot and two threads: one receives the player's packets,
// the other sends the world to the player.
void Server::start()
{
    std::cout << "Starting server..." << std::endl;
    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket == INVALID_SOCKET)
        std::cout << "Could not create server socket.";
    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(ServerPort);
    address.sin_addr.s_addr = INADDR_ANY;
    if (bind(m_socket, (sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
        std::cout << "Could not bind socket." << std::endl;
    if (listen(m_socket, ListenBacklog) == SOCKET_ERROR)
        std::cout << "Listen failed." << std::endl;
    m_running = true;
    std::cout << "Server started. Enter Q to quit." << std::endl;

    // The accept thread waits 200 ms before each accept, refuses a fifth player, and closes a refused
    // client after 500 ms.
    m_acceptThread = StartThread([this]() {
        HANDLE receiveThread = 0;
        HANDLE sendThread = 0;
        while (m_running) {
            Sleep(200);
            std::cout << "Waiting for connection." << std::endl;
            SOCKET client = accept(m_socket, 0, 0);
            if (client == INVALID_SOCKET) {
                std::cout << "Connection failed." << std::endl;
                continue;
            }

            // Stops at the first player found, so the count is at most one and the server never reports itself
            // full.
            m_world->enterRegions();
            int players = 0;
            World *world = m_world;
            for (int id = 1; id <= MaxPlayers; id++) {
                if (world->creatureByID(id) && world->creatureByID(id)->m_entityData.hostilityType == HostilityPlayer) {
                    players++;
                    break;
                }
            }
            world->leaveRegions();

            int packetId = 0;
            if (receiveAll(client, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR) {
                closesocket(client);
                continue;
            }
            if (packetId != PacketHello) {
                closesocket(client);
                continue;
            }
            int version = 0;
            if (receiveAll(client, (char *)&version, sizeof(version), 0) == SOCKET_ERROR) {
                closesocket(client);
                continue;
            }
            std::cout << "Client version: " << version << std::endl;
            if (players >= 4) {
                int reply = PacketServerFull;
                send(client, (char *)&reply, sizeof(reply), 0);
                Sleep(500);
                closesocket(client);
                std::cout << "Server is full. Closing client connection." << std::endl;
                continue;
            }
            if (version != ProtocolVersion) {
                int serverVersion = ProtocolVersion;
                send(client, (char *)&serverVersion, sizeof(serverVersion), 0);
                Sleep(500);
                closesocket(client);
                std::cout << "Wrong client version. Closing client connection." << std::endl;
                continue;
            }
            packetId = PacketWelcome;
            send(client, (char *)&packetId, sizeof(packetId), 0);
            std::cout << "New connection." << std::endl;

            // The first free creature id from 1 to 10 becomes the player's.
            world = m_world;
            long long creatureId = 0;
            for (int id = 1; id <= MaxPlayers; id++) {
                if (!world->creatureByID(id)) {
                    creatureId = id;
                    break;
                }
            }
            if (creatureId == 0) {
                closesocket(client);
                continue;
            }

            world->enterRegions();
            Creature *creature = new Creature(creatureId);
            m_world->m_creatures[creatureId] = creature;
            creature->reset();
            plasma::Vector<3, float> &spawnPoint = m_world->m_spawnPoint;
            for (int i = 0; i < 3; i++)
                creature->m_entityData.position[i] = Fixed(spawnPoint[i]);
            // A new player's box: 0.8 x 0.8 x 1.8 blocks, one and a half times over.
            creature->m_entityData.appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.8f) * 1.5f;
            creature->m_entityData.rotation = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
            creature->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
            creature->m_entityData.flags = 0;
            creature->m_entityData.level = 1;
            creature->m_entityData.hp = creature->maxHP();
            creature->m_entityData.hostilityType = HostilityPlayer;
            creature->m_entityData.race = RaceBandit;
            creature->m_entityData.origin = creature->m_entityData.position;
            creature->m_entityData.equipment.weaponRight.material = ItemMaterialIron;
            creature->m_entityData.equipment.weaponRight.category = ItemCategoryWeapon;
            creature->m_entityData.equipment.weaponRight.id = WeaponMace;
            creature->m_entityData.equipment.weaponLeft.material = ItemMaterialIron;
            creature->m_entityData.equipment.weaponLeft.category = ItemCategoryWeapon;
            creature->m_entityData.equipment.weaponLeft.id = WeaponMace;
            creature->m_entityData.classType = ClassWarrior;
            fillAppearance(creature->m_entityData.race, creature->m_entityData.appearance, 0);
            world->leaveRegions();

            // Fifty cookies and a wooden bow.
            Item item;
            item.category = ItemCategoryConsumable;
            item.id = ConsumableCookie;
            item.material = 0;
            item.level = 1;
            int cookies = 50;
            while (cookies--)
                creature->m_inventory.add(item);
            item.category = ItemCategoryWeapon;
            item.id = WeaponBow;
            item.material = ItemMaterialWood;
            item.level = 1;
            creature->m_inventory.add(item);

            // The first free connection slot is the player's number.
            m_world->enterRegions();
            int slot = 0;
            while (slot < (int)m_connections.size()) {
                if (!m_connections[slot]) {
                    m_connections[slot] = new Connection();
                    break;
                }
                slot++;
            }
            if (slot == (int)m_connections.size())
                m_connections.push_back(new Connection());
            m_connections[slot]->socket = client;
            m_connections[slot]->creatureId = creatureId;
            m_world->leaveRegions();
            std::cout << "Player " << slot << " joined." << std::endl;

            packetId = PacketEntityData;
            send(m_connections[slot]->socket, (char *)&packetId, sizeof(packetId), 0);
            send(m_connections[slot]->socket, (char *)&creatureId, sizeof(creatureId), 0);
            send(m_connections[slot]->socket, (char *)&creature->m_entityData, sizeof(Creature::EntityData), 0);
            packetId = PacketSeed;
            int seed = m_seed;
            send(m_connections[slot]->socket, (char *)&packetId, sizeof(packetId), 0);
            send(m_connections[slot]->socket, (char *)&seed, sizeof(seed), 0);

            std::vector<Connection *> &connections = m_connections;
            std::list<HitPacket> &hits = m_hits;
            std::list<BuffPacket> &buffs = m_buffs;
            std::list<BulletPacket> &projectiles = m_projectiles;
            bool &running = m_running;
            // The receiving thread queues the player's packets; it captures `this` without using it.
            receiveThread = StartThread([world, &connections, slot, creatureId, &hits, &buffs, &projectiles, &running,
                                         this]() {
                bool disconnected = false;
                while (running && connections[slot] && !disconnected) {
                    int packetId = 0;
                    if (receiveAll(connections[slot]->socket, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR)
                        break;
                    switch (packetId) {
                    case PacketEntityData: {
                        // The player's entity data, compressed.
                        BytesIO data;
                        int size = 0;
                        if (receiveAll(connections[slot]->socket, (char *)&size, sizeof(size), 0) == SOCKET_ERROR) {
                            disconnected = true;
                            continue;
                        }
                        if (size == 0) {
                            disconnected = true;
                            continue;
                        }
                        data.m_data.resize(size);
                        if (receiveAll(connections[slot]->socket, data.data(), size, 0) == SOCKET_ERROR) {
                            disconnected = true;
                            continue;
                        }
                        data.decompress();
                        // The packet's creature id is read and ignored: the data always goes to the connection's own
                        // Creature.
                        long long id = 0;
                        data.read(&id, sizeof(id));
                        world->enterRegions();
                        Creature::EntityData entityData(world->creatureByID(creatureId)->m_entityData);
                        world->leaveRegions();
                        readEntityData(data, entityData);
                        world->enterRegions();
                        world->creatureByID(creatureId)->m_entityData = entityData;
                        world->leaveRegions();
                        break;
                    }
                    case PacketInteraction: {
                        Interaction interaction;
                        if (receiveAll(connections[slot]->socket, (char *)&interaction, sizeof(interaction), 0) ==
                            SOCKET_ERROR) {
                            disconnected = true;
                            continue;
                        }
                        world->enterRegions();
                        world->creatureByID(creatureId)->m_interactions.push_back(interaction);
                        world->leaveRegions();
                        break;
                    }
                    case PacketChat: {
                        int length;
                        if (receiveAll(connections[slot]->socket, (char *)&length, sizeof(length), 0) == SOCKET_ERROR) {
                            disconnected = true;
                            continue;
                        }
                        if (length <= 0)
                            break;
                        // UTF-16 on the wire, whatever the size of wchar_t.
                        std::vector<unsigned short> units(length);
                        if (receiveAll(connections[slot]->socket, (char *)&units[0], length * 2, 0) == SOCKET_ERROR) {
                            disconnected = true;
                            continue;
                        }
                        std::wstring text(units.begin(), units.end());
                        // To every connection; an empty slot is not checked.
                        world->enterRegions();
                        for (int i = 0; i < (int)connections.size(); i++) {
                            Connection::ChatLine line;
                            line.text = text;
                            line.creatureId = creatureId;
                            connections[i]->chat.push_back(line);
                        }
                        world->leaveRegions();
                        break;
                    }
                    case PacketHit: {
                        HitPacket hit;
                        if (receiveAll(connections[slot]->socket, (char *)&hit, sizeof(hit), 0) == SOCKET_ERROR) {
                            disconnected = true;
                            continue;
                        }
                        world->enterRegions();
                        hits.push_back(hit);
                        world->leaveRegions();
                        break;
                    }
                    case PacketBuff: {
                        BuffPacket buff;
                        if (receiveAll(connections[slot]->socket, (char *)&buff, sizeof(buff), 0) == SOCKET_ERROR) {
                            disconnected = true;
                            continue;
                        }
                        world->enterRegions();
                        buffs.push_back(buff);
                        world->leaveRegions();
                        break;
                    }
                    case PacketProjectile: {
                        BulletPacket projectile;
                        if (receiveAll(connections[slot]->socket, (char *)&projectile, sizeof(projectile), 0) ==
                            SOCKET_ERROR) {
                            disconnected = true;
                            continue;
                        }
                        world->enterRegions();
                        projectiles.push_back(projectile);
                        world->leaveRegions();
                        break;
                    }
                    case PacketZoneRequest: {
                        plasma::Vector<2, int> zone;
                        if (receiveAll(connections[slot]->socket, (char *)&zone, sizeof(zone), 0) == SOCKET_ERROR) {
                            disconnected = true;
                            continue;
                        }
                        EnterCriticalSection(&connections[slot]->lock);
                        connections[slot]->zones.push_back(zone);
                        LeaveCriticalSection(&connections[slot]->lock);
                        break;
                    }
                    case PacketRegionRequest: {
                        plasma::Vector<2, int> region;
                        if (receiveAll(connections[slot]->socket, (char *)&region, sizeof(region), 0) == SOCKET_ERROR) {
                            disconnected = true;
                            continue;
                        }
                        EnterCriticalSection(&connections[slot]->lock);
                        connections[slot]->regionRequests.push_back(region);
                        LeaveCriticalSection(&connections[slot]->lock);
                        break;
                    }
                    }
                }
            });
            SetThreadPriority(receiveThread, THREAD_PRIORITY_BELOW_NORMAL);
            // The sending thread also removes the player when the connection ends.
            sendThread = StartThread([world, &connections, slot, creatureId, &running, this, receiveThread]() {
                // The entity data last sent to the player, and this tick's.
                std::map<long long, Creature::EntityData> sent;
                std::map<long long, Creature::EntityData> visible;
                bool disconnected = false;
                timeBeginPeriod(1);
                int startTime = timeGetTime();
                while (running && connections[slot]) {
                    visible.clear();
                    // Collected but never sent.
                    std::vector<Zone::AirshipState> objects;
                    world->enterRegions();

                    // Other players anywhere, other creatures within 200 blocks. After the first half second one
                    // creature the player has not seen yet is added per tick.
                    bool mayAdd = false;
                    if ((int)timeGetTime() - startTime > 500)
                        mayAdd = true;
                    for (std::map<long long, Creature *>::iterator it = world->m_creatures.begin();
                         it != world->m_creatures.end(); ++it) {
                        if (!it->second || it->first == creatureId)
                            continue;
                        if (it->second->m_entityData.hostilityType != HostilityPlayer) {
                            Creature *player = world->creatureByID(creatureId);
                            plasma::Vector<3, float> offset(it->second->m_entityData.position -
                                                            player->m_entityData.position);
                            if (offset.squaredLength() > 40000.0f)
                                continue;
                        }
                        if (sent.find(it->second->m_id) == sent.end()) {
                            if (!mayAdd)
                                continue;
                            mayAdd = false;
                        }
                        visible[it->second->m_id] = it->second->m_entityData;
                    }
                    for (std::map<long long, Zone::Airship *>::iterator it = world->m_airships.begin();
                         it != world->m_airships.end(); ++it) {
                        Zone::Airship *object = it->second;
                        if (!object)
                            continue;
                        Creature *player = world->creatureByID(creatureId);
                        plasma::Vector<3, float> offset(object->position - player->m_entityData.position);
                        // Airships within 512 blocks (squared).
                        if (offset.squaredLength() > 262144.0f)
                            continue;
                        objects.push_back(*object);
                    }
                    // Forget the creatures that left the world.
                    std::map<long long, Creature::EntityData> remaining;
                    for (std::map<long long, Creature::EntityData>::iterator it = sent.begin(); it != sent.end();
                         ++it) {
                        if (world->m_creatures.find(it->first) != world->m_creatures.end())
                            remaining[it->first] = it->second;
                    }
                    world->leaveRegions();
                    sent = remaining;

                    // Each visible creature's entity data, compressed: all of it the first time, then what changed.
                    for (std::map<long long, Creature::EntityData>::iterator it = visible.begin(); it != visible.end();
                         ++it) {
                        int packetId = PacketEntityData;
                        send(connections[slot]->socket, (char *)&packetId, sizeof(packetId), 0);
                        BytesIO data;
                        data.write(&it->first, sizeof(it->first));
                        if (sent.find(it->first) == sent.end())
                            writeEntityData(data, it->second, it->second, true);
                        else
                            writeEntityData(data, sent[it->first], it->second, false);
                        data.compress();
                        int size = data.size();
                        if (send(connections[slot]->socket, (char *)&size, sizeof(size), 0) == SOCKET_ERROR) {
                            disconnected = true;
                            break;
                        }
                        if (send(connections[slot]->socket, data.data(), size, 0) == SOCKET_ERROR) {
                            disconnected = true;
                            break;
                        }
                    }
                    int packetId = PacketEntitiesEnd;
                    send(connections[slot]->socket, (char *)&packetId, sizeof(packetId), 0);
                    sent = visible;

                    // Take the connection's queued events.
                    world->enterRegions();
                    int time = world->m_time;
                    int day = world->m_day;
                    ServerUpdates updates(connections[slot]->updates);
                    // The code leaves a critical section it never entered.
                    LeaveCriticalSection(&connections[slot]->lock);
                    connections[slot]->updates.blocks.clear();
                    connections[slot]->updates.hits.clear();
                    connections[slot]->updates.particles.clear();
                    connections[slot]->updates.sounds.clear();
                    connections[slot]->updates.projectiles.clear();
                    connections[slot]->updates.items.clear();
                    connections[slot]->updates.objects.clear();
                    connections[slot]->updates.buildings.clear();
                    connections[slot]->updates.pickedUpItems.clear();
                    connections[slot]->updates.kills.clear();
                    connections[slot]->updates.aggro.clear();
                    connections[slot]->updates.buffs.clear();
                    connections[slot]->updates.missions.clear();
                    // One chat line per tick.
                    Connection::ChatLine chat;
                    if (connections[slot]->chat.size() != 0) {
                        chat = connections[slot]->chat.front();
                        connections[slot]->chat.pop_front();
                    }
                    world->leaveRegions();

                    packetId = PacketTime;
                    if (send(connections[slot]->socket, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR)
                        break;
                    if (send(connections[slot]->socket, (char *)&day, sizeof(day), 0) == SOCKET_ERROR)
                        break;
                    if (send(connections[slot]->socket, (char *)&time, sizeof(time), 0) == SOCKET_ERROR)
                        break;
                    packetId = PacketWorldUpdate;
                    if (send(connections[slot]->socket, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR)
                        break;

                    // The events, compressed.
                    BytesIO data;
                    writeList(data, updates.blocks);
                    writeList(data, updates.hits);
                    writeList(data, updates.particles);
                    writeList(data, updates.sounds);
                    writeList(data, updates.projectiles);
                    writeList(data, updates.objects);
                    int count = updates.items.size();
                    data.write(&count, sizeof(count));
                    for (std::list<ServerUpdates::ItemListPacket>::iterator it = updates.items.begin();
                         it != updates.items.end(); ++it) {
                        data.write(&it->zone, sizeof(it->zone));
                        writeList(data, it->items);
                    }
                    // A failed entity-data send ends the thread only here.
                    if (disconnected)
                        break;
                    count = updates.buildings.size();
                    data.write(&count, sizeof(count));
                    for (std::list<ServerUpdates::BuildingListPacket>::iterator it = updates.buildings.begin();
                         it != updates.buildings.end(); it++) {
                        ServerUpdates::BuildingListPacket &record = *it;
                        data.write(&record.zone, sizeof(record.zone));
                        writeList(data, record.buildings);
                    }
                    writeList(data, updates.pickedUpItems);
                    writeList(data, updates.kills);
                    writeList(data, updates.aggro);
                    writeList(data, updates.buffs);
                    writeList(data, updates.missions);
                    data.compress();
                    int size = data.size();
                    if (send(connections[slot]->socket, (char *)&size, sizeof(size), 0) == SOCKET_ERROR)
                        break;
                    if (send(connections[slot]->socket, data.data(), size, 0) == SOCKET_ERROR)
                        break;
                    if (!chat.text.empty()) {
                        packetId = PacketChat;
                        if (send(connections[slot]->socket, (char *)&packetId, sizeof(packetId), 0) == SOCKET_ERROR)
                            break;
                        if (send(connections[slot]->socket, (char *)&chat.creatureId, sizeof(chat.creatureId), 0) ==
                            SOCKET_ERROR)
                            break;
                        int length = chat.text.size();
                        if (send(connections[slot]->socket, (char *)&length, sizeof(length), 0) == SOCKET_ERROR)
                            break;
                        std::vector<unsigned short> units(chat.text.begin(), chat.text.end()); // UTF-16
                        if (send(connections[slot]->socket, (char *)&units[0], length * 2, 0) == SOCKET_ERROR)
                            break;
                    }

                    // At most one tick per 20 ms.
                    static int lastTick;
                    int now = timeGetTime();
                    if (now - lastTick < 20)
                        Sleep(lastTick - now + 20);
                    lastTick = now;
                }
                timeEndPeriod(1);

                // The player has left: remove the creature and the connection.
                WaitForSingleObject(receiveThread, INFINITE);
                world->enterRegions();
                if (connections[slot]) {
                    closesocket(connections[slot]->socket);
                    delete world->m_creatures[creatureId];
                    world->m_creatures.erase(creatureId);
                    delete connections[slot];
                    connections[slot] = 0;
                }
                world->leaveRegions();
            });
            SetThreadPriority(sendThread, THREAD_PRIORITY_BELOW_NORMAL);
        }
        WaitForSingleObject(receiveThread, INFINITE);
        WaitForSingleObject(sendThread, INFINITE);
    });
}

#endif
}

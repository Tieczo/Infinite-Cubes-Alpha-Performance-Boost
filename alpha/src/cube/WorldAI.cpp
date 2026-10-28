// Path search traverses world blocks and maintains each creature's discovered and pending nodes.
#include "cube/WorldDimensions.h"
#include "cube/Creature.h"
#include "cube/WorldBlocks.h"

namespace cube {
// The path search's and the steering's values (anonymous: this file's own): distances in blocks,
// speeds in blocks per second, rates per millisecond.
namespace {
enum {
    PathRestartTime = 3000
}; // milliseconds a search may take before it starts over
enum {
    DirectApproachPathLength = 30
}; // a shorter path that ends short of the goal heads for it
enum {
    RetainedPendingNodes = 70
}; // the most promising pending nodes kept when the start moves
enum {
    NearWaypointDistanceSquared = 4
}; // squared blocks to the nearest point of the path
enum {
    WaterDescent = 2
}; // downwards, in water
enum {
    WallClimbRise = 3
}; // upwards, against an object
enum {
    ApproachSpeed = 8
}; // straight to the goal
enum {
    ClimbSpeed = 4
};
enum {
    UnsupportedPathSpeed = 8
}; // in the air
enum {
    NearWaypointAcceleration = 20
};
enum {
    FarWaypointAcceleration = 40
};
enum {
    LowObjectHeight = 3
}; // lower objects are passed over unless a ceiling is near
}

// Builds the creature's path from the search's results: the goal block (or the discovered block
// nearest to it when the goal was not reached) back to the start block through each block's
// parent, at most one step more than there are discovered blocks.
void World::finishPathSearch(Creature *creature)
{
    plasma::Vector<3, int> start = toBlock(creature->m_pathStart);
    // The goal block first, then each block's parent.
    plasma::Vector<3, int> next = toBlock(creature->m_pathGoal);
    creature->m_path.clear();
    plasma::Vector<3, int> block = next;
    if (creature->m_nearestPathNode != creature->m_pathNodes.end()) {
        const Creature::PathKey &key = creature->m_nearestPathNode->first;
        block = plasma::Vector<3, int>(key[0], key[1], key[2]);
    }
    creature->m_path.push_front(block);
    int steps = 0;
    while (block != start) {
        if (steps > (int)creature->m_pathNodes.size())
            return;
        steps++;
        Creature::PathNode *node = creature->findPathNode(block[0], block[1], block[2]);
        if (!node)
            return;
        next = node->parent;
        if (block == next)
            return;
        block = next;
        creature->m_path.push_front(next);
    }
}

// Follows the nearest point of the current path. When that point changes, keeps the most promising
// pending nodes whose parent chains reach it.
bool World::followPath(Creature *creature, int elapsed)
{
    Creature::EntityData &entity = creature->m_entityData;
    entity.acceleration = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
    plasma::Vector<3, float> offset(0.0f, 0.0f, 0.5f - entity.appearance.size[2] * 0.5f);
    plasma::Vector<3, int> goalOffset =
        toBlock(creature->m_pathGoal) - toBlock(entity.position + plasma::Vector<3, Fixed>(offset));
    if (goalOffset.squaredLength() <= creature->m_pathRadius * creature->m_pathRadius) {
        creature->clearPath();
        return false;
    }
    if (creature->m_path.empty())
        return false;
    if (creature->m_pathSearchTime > PathRestartTime) {
        creature->m_pathStart = entity.position + plasma::Vector<3, Fixed>(offset);
        beginPathSearch(creature);
    }
    if (creature->m_path.empty())
        return false;

    std::list<plasma::Vector<3, int>>::iterator closest = creature->m_path.begin();
    int nearestDistance = -1;
    if (!creature->m_path.empty()) {
        for (std::list<plasma::Vector<3, int>>::iterator it = creature->m_path.begin(); it != creature->m_path.end();
             ++it) {
            plasma::Vector<3, int> foot =
                toBlock(entity.position -
                        plasma::Vector<3, Fixed>(Fixed(0.0), Fixed(0.0), Fixed(entity.appearance.size[2] * 0.5f)));
            int distance = (foot - *it).squaredLength();
            if (nearestDistance < 0 || distance <= nearestDistance) {
                nearestDistance = distance;
                closest = it;
            }
        }
        if (nearestDistance == 0)
            creature->m_pathSearchTime = 0;
    }
    bool approachGoal = false;
    if (!(entity.flags & (1 << CreatureFlagHitObject)) && creature->m_path.size() < DirectApproachPathLength &&
        (creature->m_path.back() - toBlock(creature->m_pathGoal)).squaredLength() >
            creature->m_pathRadius * creature->m_pathRadius) {
        approachGoal = true;
    } else if (nearestDistance == 0 && *closest != creature->m_pathStartBlock) {
        creature->m_pathStartBlock = *closest;
        creature->m_pathStart = *closest;
        std::set<Creature::PathKey> retained;
        int count = RetainedPendingNodes;
        if ((int)creature->m_pendingPathNodes.size() < count)
            count = (int)creature->m_pendingPathNodes.size();
        for (int i = 0; i < count; i++) {
            std::set<Creature::PathKey>::iterator best = creature->m_pendingPathNodes.end();
            int heuristic = -1;
            for (std::set<Creature::PathKey>::iterator it = creature->m_pendingPathNodes.begin();
                 it != creature->m_pendingPathNodes.end(); ++it) {
                Creature::PathNode *node = creature->findPathNode((*it)[0], (*it)[1], (*it)[2]);
                if (node && (heuristic < 0 || node->heuristic < heuristic)) {
                    heuristic = node->heuristic;
                    best = it;
                }
            }
            if (heuristic >= 0) {
                retained.insert(*best);
                creature->m_pendingPathNodes.erase(best);
            }
        }
        creature->m_pendingPathNodes = retained;
        std::list<std::set<Creature::PathKey>::iterator> disconnected;
        std::map<Creature::PathKey, Creature::PathNode> nodes;
        Creature::PathNode *root = creature->findPathNode((*closest)[0], (*closest)[1], (*closest)[2]);
        for (std::set<Creature::PathKey>::iterator it = creature->m_pendingPathNodes.begin();
             it != creature->m_pendingPathNodes.end(); ++it) {
            Creature::PathNode *node = creature->findPathNode((*it)[0], (*it)[1], (*it)[2]);
            std::map<Creature::PathKey, Creature::PathNode> chain;
            Creature::PathKey key = {};
            key = *it;
            while (node && node != root) {
                Creature::PathNode *parent = creature->findPathNode(node->parent[0], node->parent[1], node->parent[2]);
                if (parent == node || !parent)
                    break;
                chain[key] = *node;
                key[0] = node->parent[0];
                key[1] = node->parent[1];
                key[2] = node->parent[2];
                node = parent;
            }
            if (node != root)
                disconnected.push_back(it);
            else
                nodes.insert(chain.begin(), chain.end());
        }
        Creature::PathKey key = {};
        key[0] = (*closest)[0];
        key[1] = (*closest)[1];
        key[2] = (*closest)[2];
        nodes[key] = *root;
        nodes[key].parent = *closest;
        for (std::list<std::set<Creature::PathKey>::iterator>::iterator it = disconnected.begin();
             it != disconnected.end(); ++it)
            creature->m_pendingPathNodes.erase(*it);
        creature->m_pathNodes = nodes;
        creature->m_nearestPathNode = creature->m_pathNodes.end();
        for (std::map<Creature::PathKey, Creature::PathNode>::iterator it = creature->m_pathNodes.begin();
             it != creature->m_pathNodes.end(); ++it) {
            if (creature->m_nearestPathNode == creature->m_pathNodes.end() ||
                it->second.heuristic < creature->m_nearestPathNode->second.heuristic)
                creature->m_nearestPathNode = it;
        }
    }

    const plasma::Vector<3, int> *target = &*closest;
    bool onGround =
        isOnGround(plasma::Vector<3, Fixed>(*target) +
                       plasma::Vector<3, Fixed>(Fixed(0.5), Fixed(0.5), Fixed(entity.appearance.size[2] * 0.5f)),
                   entity.appearance.size);
    bool nextOnGround = true;
    bool unsupported = false;
    int height = (*closest)[2];
    std::list<plasma::Vector<3, int>>::iterator next = closest;
    if (!approachGoal && closest != --creature->m_path.end()) {
        ++next;
        nextOnGround = false;
        target = &*next;
        height = (*next)[2];
        if (isOnGround(plasma::Vector<3, Fixed>(*target) +
                           plasma::Vector<3, Fixed>(Fixed(0.5), Fixed(0.5), Fixed(entity.appearance.size[2] * 0.5f)),
                       entity.appearance.size))
            nextOnGround = true;
        else
            unsupported = true;
    } else {
        unsupported = true;
    }
    if (!approachGoal && (onGround || nextOnGround) && height != (*closest)[2] && next != --creature->m_path.end()) {
        ++next;
        target = &*next;
        if (isOnGround(plasma::Vector<3, Fixed>(*target) +
                           plasma::Vector<3, Fixed>(Fixed(0.5), Fixed(0.5), Fixed(entity.appearance.size[2] * 0.5f)),
                       entity.appearance.size)) {
            unsupported = false;
            nextOnGround = true;
        } else {
            nextOnGround = false;
        }
    } else {
        nextOnGround = true;
    }
    plasma::Vector<3, float> direction(
        plasma::Vector<3, Fixed>(*target) +
        plasma::Vector<3, Fixed>(Fixed(0.5), Fixed(0.5), Fixed(entity.appearance.size[2] * 0.5f)) - entity.position);
    entity.setToggles((1 << CreatureToggleClimbing), nearestDistance < NearWaypointDistanceSquared &&
                                                         (entity.flags & (1 << CreatureFlagAtWall)) && !onGround);
    bool jump = direction[2] <= 1.0f && !(entity.flags & (1 << CreatureFlagHitObject)) && onGround && nextOnGround;
    if (direction.squaredLength() > 0.001f) {
        if (direction.squaredLength() > 1.0f)
            direction.normalize();
        if (direction[2] > 0.0f)
            direction[2] = 1.0f;
        else if (direction[2] < 0.0f && (entity.flags & (1 << CreatureFlagInWater)))
            direction[2] = -WaterDescent;
        if (entity.flags & (1 << CreatureFlagHitObject)) {
            direction[2] = WallClimbRise;
            plasma::Vector<3, int> low =
                toBlock(entity.position - plasma::Vector<3, Fixed>(entity.appearance.size * 0.5f));
            plasma::Vector<3, int> high =
                toBlock(entity.position + plasma::Vector<3, Fixed>(entity.appearance.size * 0.5f));
            low = low - plasma::Vector<3, int>(plasma::Vector<3, float>(1.0f, 1.0f, 0.0f));
            high += plasma::Vector<3, int>(plasma::Vector<3, float>(1.0f, 1.0f, 0.0f));
            for (int x = low[0] / BlocksPerObjectCell; x <= high[0] / BlocksPerObjectCell; x++) {
                for (int y = low[1] / BlocksPerObjectCell; y <= high[1] / BlocksPerObjectCell; y++) {
                    std::list<Zone::CellRecord> *objects = getCellObjects(x, y);
                    if (!objects)
                        continue;
                    for (std::list<Zone::CellRecord>::iterator it = objects->begin(); it != objects->end(); ++it) {
                        Zone::Object *object = it->object;
                        if ((object->type != StaticObjectDoor && object->type != StaticObjectBigDoor) ||
                            !object->active)
                            continue;
                        plasma::Vector<3, float> size = object->size;
                        if (object->rotation % 2 != 0) {
                            float swap = size[0];
                            size[0] = size[1];
                            size[1] = swap;
                        }
                        if (entity.position[0] + Fixed(entity.appearance.size[0] * 0.5f) + Fixed(1) <
                                object->position[0] - Fixed(size[0] * 0.5f) ||
                            entity.position[0] - Fixed(entity.appearance.size[0] * 0.5f) - Fixed(1) >=
                                object->position[0] + Fixed(size[0] * 0.5f) ||
                            entity.position[1] + Fixed(entity.appearance.size[1] * 0.5f) + Fixed(1) <
                                object->position[1] - Fixed(size[1] * 0.5f) ||
                            entity.position[1] - Fixed(entity.appearance.size[1] * 0.5f) - Fixed(1) >=
                                object->position[1] + Fixed(size[1] * 0.5f) ||
                            entity.position[2] + Fixed(entity.appearance.size[2] * 0.5f) < object->position[2] ||
                            entity.position[2] - Fixed(entity.appearance.size[2] * 0.5f) >=
                                object->position[2] + Fixed(size[2]))
                            continue;
                        object->active = false;
                        object->transitionTime = 0;
                    }
                }
            }
        }
        if (approachGoal && !entity.binaryToggles.climbing)
            return false;
        if (nearestDistance < NearWaypointDistanceSquared) {
            if (approachGoal && direction[2] >= 0.0f) {
                entity.velocity = direction * ApproachSpeed;
            } else {
                if (!approachGoal && direction[2] >= 0.0f && !jump) {
                    for (int i = 0; i < elapsed; i++)
                        entity.velocity[2] += (direction[2] * ClimbSpeed - entity.velocity[2]) * 0.05f;
                }
                float speed = entity.binaryToggles.fastMovement ? 12.0f : 6.0f;
                if (unsupported) {
                    for (int i = 0; i < elapsed; i++)
                        entity.velocity[0] += (direction[0] * UnsupportedPathSpeed - entity.velocity[0]) * 0.01f;
                    for (int i = 0; i < elapsed; i++)
                        entity.velocity[1] += (direction[1] * UnsupportedPathSpeed - entity.velocity[1]) * 0.01f;
                } else {
                    for (int i = 0; i < elapsed; i++)
                        entity.velocity[0] += (speed * direction[0] - entity.velocity[0]) * 0.001f;
                    for (int i = 0; i < elapsed; i++)
                        entity.velocity[1] += (speed * direction[1] - entity.velocity[1]) * 0.001f;
                }
            }
            entity.acceleration = direction * NearWaypointAcceleration;
        } else {
            entity.acceleration = direction * FarWaypointAcceleration;
        }
        entity.retreat = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
    }
    return true;
}

bool Creature::isPathNodePending(int x, int y, int z)
{
    Creature::PathKey key = {};
    key[0] = x;
    key[1] = y;
    key[2] = z;
    return m_pendingPathNodes.find(key) != m_pendingPathNodes.end();
}

// Diagonal horizontal steps cost 28, axial steps 20, and vertical steps 20.
int pathDistance(const plasma::Vector<3, int> &offset)
{
    int x = abs(offset[0]);
    int y = abs(offset[1]);
    int z = abs(offset[2]);
    int horizontal;
    if (x > y)
        horizontal = 10 * x + 4 * y;
    else
        horizontal = 4 * x + 10 * y;
    return 2 * (horizontal + 10 * z);
}

Creature::PathNode *Creature::findPathNode(int x, int y, int z)
{
    Creature::PathKey key = {};
    key[0] = x;
    key[1] = y;
    key[2] = z;
    std::map<PathKey, PathNode>::iterator it = m_pathNodes.find(key);
    if (it == m_pathNodes.end())
        return 0;
    return &it->second;
}

void Creature::setPathNode(int x, int y, int z, const PathNode &node)
{
    Creature::PathKey key = {};
    key[0] = x;
    key[1] = y;
    key[2] = z;
    m_pathNodes[key] = node;
    m_pendingPathNodes.insert(key);
    if (m_nearestPathNode == m_pathNodes.end() || node.heuristic < m_nearestPathNode->second.heuristic)
        m_nearestPathNode = m_pathNodes.find(key);
}

// Starts the creature's path search from m_pathStart towards m_pathGoal: the start block becomes
// the first node, its estimate the path distance to the goal block. The zone objects that the
// creature's box overlaps when it stands at the centre of the start block or of the goal block are
// recorded in m_overlappedObjects, so that the search does not take them for obstacles.
void World::beginPathSearch(Creature *creature)
{
    creature->clearPath();
    creature->m_pathStartBlock = toBlock(creature->m_pathStart);
    Creature::PathNode node;
    plasma::Vector<3, int> start = toBlock(creature->m_pathStart);
    creature->m_pathSearchTime = 0;
    float distance = (float)pathDistance(start - toBlock(creature->m_pathGoal));
    node.movementCost = 1;
    node.heuristic = (int)distance;
    node.totalCost = (int)distance;
    node.parent = start;
    creature->setPathNode(start[0], start[1], start[2], node);
    creature->m_overlappedObjects.clear();

    const plasma::Vector<3, float> &own = creature->m_entityData.appearance.size;
    // The box at the centre of the start block (the bottom half a creature height above the block's
    // centre).
    plasma::Vector<3, Fixed> position = plasma::Vector<3, Fixed>(toBlock(creature->m_pathStart)) +
                                        plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.5f, 0.5f, own[2] * 0.5f));
    int cellX = (int)position[0] / BlocksPerObjectCell;
    int cellY = (int)position[1] / BlocksPerObjectCell;
    for (int x = cellX - 1; x <= cellX + 1; x++) {
        for (int y = cellY - 1; y <= cellY + 1; y++) {
            std::list<Zone::CellRecord> *objects = getCellObjects(x, y);
            if (!objects)
                continue;
            for (std::list<Zone::CellRecord>::iterator it = objects->begin(); it != objects->end(); ++it) {
                Zone::Object *object = it->object;
                int type = object->type;
                if (type == StaticObjectSpikeTrap || type == StaticObjectFireTrap || type == StaticObjectLever)
                    continue;
                if ((type == StaticObjectDoor || type == StaticObjectStompTrap || type == StaticObjectBigDoor ||
                     type == StaticObjectWindow || type == StaticObjectGate) &&
                    !object->active)
                    continue;
                // The object's extent along x and y: its size turned by its rotation (a quarter turn swaps them).
                plasma::Vector<3, float> size = object->size;
                if (object->rotation % 2 != 0) {
                    float swap = size[0];
                    size[0] = size[1];
                    size[1] = swap;
                }
                if (position[0] + Fixed(own[0] * 0.5f) >= object->position[0] - Fixed(size[0] * 0.5f) &&
                    position[0] - Fixed(own[0] * 0.5f) < object->position[0] + Fixed(size[0] * 0.5f) &&
                    position[1] + Fixed(own[1] * 0.5f) >= object->position[1] - Fixed(size[1] * 0.5f) &&
                    position[1] - Fixed(own[1] * 0.5f) < object->position[1] + Fixed(size[1] * 0.5f) &&
                    position[2] + Fixed(own[2] * 0.5f) >= object->position[2] &&
                    position[2] - Fixed(own[2] * 0.5f) < object->position[2] + Fixed(size[2]))
                    creature->m_overlappedObjects.insert(it->object);
            }
        }
    }

    // The same at the centre of the goal block.
    position = plasma::Vector<3, Fixed>(toBlock(creature->m_pathGoal)) +
               plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.5f, 0.5f, own[2] * 0.5f));
    cellX = (int)position[0] / BlocksPerObjectCell;
    cellY = (int)position[1] / BlocksPerObjectCell;
    for (int x = cellX - 1; x <= cellX + 1; x++) {
        for (int y = cellY - 1; y <= cellY + 1; y++) {
            std::list<Zone::CellRecord> *objects = getCellObjects(x, y);
            if (!objects)
                continue;
            for (std::list<Zone::CellRecord>::iterator it = objects->begin(); it != objects->end(); ++it) {
                Zone::Object *object = it->object;
                int type = object->type;
                if (type == StaticObjectSpikeTrap || type == StaticObjectFireTrap || type == StaticObjectLever)
                    continue;
                if ((type == StaticObjectDoor || type == StaticObjectStompTrap || type == StaticObjectBigDoor ||
                     type == StaticObjectWindow || type == StaticObjectGate) &&
                    !object->active)
                    continue;
                // The object's extent along x and y: its size turned by its rotation (a quarter turn swaps them).
                plasma::Vector<3, float> size = object->size;
                if (object->rotation % 2 != 0) {
                    float swap = size[0];
                    size[0] = size[1];
                    size[1] = swap;
                }
                if (position[0] + Fixed(own[0] * 0.5f) >= object->position[0] - Fixed(size[0] * 0.5f) &&
                    position[0] - Fixed(own[0] * 0.5f) < object->position[0] + Fixed(size[0] * 0.5f) &&
                    position[1] + Fixed(own[1] * 0.5f) >= object->position[1] - Fixed(size[1] * 0.5f) &&
                    position[1] - Fixed(own[1] * 0.5f) < object->position[1] + Fixed(size[1] * 0.5f) &&
                    position[2] + Fixed(own[2] * 0.5f) >= object->position[2] &&
                    position[2] - Fixed(own[2] * 0.5f) < object->position[2] + Fixed(size[2]))
                    creature->m_overlappedObjects.insert(it->object);
            }
        }
    }
}

// One step of the path search (an A* search over blocks): the pending block with the lowest total
// cost is expanded. The creature's box at that block is tested against the blocks of the shell one
// block around it and against the zone objects there (except doors, gates, traps and the object it
// interacts with); blocked sides and the support below decide which of the eight horizontal
// neighbours it can step to (an axial step costs 10, a diagonal 14, both 10 more without a floor),
// whether it falls (no floor and no object below: cost 0) and whether it climbs (beside a wall or
// in water, with head room: 40 without a floor). The search is abandoned beyond 500 pending or
// 20,000 discovered blocks. False when there is nothing to expand or the goal block is reached.
// West, east, south and north stand for -x, +x, -y and +y.
bool World::continuePathSearch(Creature *creature)
{
    if (creature->m_pathNodes.empty())
        return false;
    if (creature->m_pendingPathNodes.size() > 500 || creature->m_pathNodes.size() > 20000) {
        creature->clearPath();
        return false;
    }
    plasma::Vector<3, int> goal = toBlock(creature->m_pathGoal);
    if (creature->m_pendingPathNodes.empty())
        return false;

    std::set<Creature::PathKey>::iterator best = creature->m_pendingPathNodes.begin();
    Creature::PathNode *bestNode = 0;
    int lowestCost = -1;
    for (std::set<Creature::PathKey>::iterator it = creature->m_pendingPathNodes.begin();
         it != creature->m_pendingPathNodes.end(); ++it) {
        Creature::PathNode *node = creature->findPathNode((*it)[0], (*it)[1], (*it)[2]);
        if (node && (!bestNode || node->totalCost < lowestCost)) {
            lowestCost = node->totalCost;
            best = it;
            bestNode = node;
        }
    }
    plasma::Vector<3, int> block((*best)[0], (*best)[1], (*best)[2]);
    creature->m_pendingPathNodes.erase(best);
    if (block == goal)
        return false;

    // The creature's box at the block (its bottom half a creature height above the block's centre) and
    // the blocks it covers.
    const plasma::Vector<3, float> &own = creature->m_entityData.appearance.size;
    plasma::Vector<3, Fixed> centre =
        plasma::Vector<3, Fixed>(block) + plasma::Vector<3, Fixed>(Fixed(0.5), Fixed(0.5), Fixed(own[2] * 0.5));
    plasma::Vector<3, int> low = toBlock(centre - plasma::Vector<3, Fixed>(own * 0.5f) +
                                         plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, 0.01f)));
    plasma::Vector<3, int> high = toBlock(centre + plasma::Vector<3, Fixed>(own * 0.5f));
    int x0 = low[0] - 1;
    int y0 = low[1] - 1;
    int z0 = low[2] - 1;
    int x1 = high[0] + 1;
    int y1 = high[1] + 1;
    int z1 = high[2] + 1;
    int cellX0 = x0 / BlocksPerObjectCell;
    int cellY0 = y0 / BlocksPerObjectCell;
    int cellX1 = x1 / BlocksPerObjectCell;
    int cellY1 = y1 / BlocksPerObjectCell;

    // The shell's solid blocks: the floor and ceiling below and above the box, its four walls, and the
    // edges and corners between them.
    bool objectWest = false, objectEast = false, objectSouth = false, objectNorth = false;
    bool floorWest = false, floorEast = false, floorSouth = false, floorNorth = false;
    bool onObject = false;
    bool wallWest = false, wallEast = false, wallSouth = false, wallNorth = false;
    bool floor = false;
    bool floorSW = false, floorSE = false, floorNW = false, floorNE = false;
    bool ceilingWest = false, ceilingEast = false, ceilingSouth = false, ceilingNorth = false;
    bool edgeSW = false, edgeSE = false, edgeNW = false, edgeNE = false;
    bool ceiling = false;
    bool inWater = (getBlock(centre[0], centre[1], centre[2] + 0.001, 0).type & BlockWater) != 0;
    for (int x = x0; x <= x1; x++) {
        for (int y = y0; y <= y1; y++) {
            for (int z = z0; z <= z1; z++) {
                if (x > x0 && x < x1 && y > y0 && y < y1 && z > z0 && z < z1)
                    continue;
                int type = getBlock(x, y, z, 0).type & ~BlockFlagMask;
                if (type == BlockAir || type == BlockWater)
                    continue;
                if (x == x0 && y == y0 && z == z0)
                    floorSW = true;
                if (x == x1 && y == y0 && z == z0)
                    floorSE = true;
                if (x == x0 && y == y1 && z == z0)
                    floorNW = true;
                if (x == x1 && y == y1 && z == z0)
                    floorNE = true;
                if (x == x0 && y == y0 && z > z0 && z < z1)
                    edgeSW = true;
                if (x == x1 && y == y0 && z > z0 && z < z1)
                    edgeSE = true;
                if (x == x0 && y == y1 && z > z0 && z < z1)
                    edgeNW = true;
                if (x == x1 && y == y1 && z > z0 && z < z1)
                    edgeNE = true;
                if (x > x0 && x < x1 && y > y0 && y < y1) {
                    if (z == z0)
                        floor = true;
                    if (z == z1)
                        ceiling = true;
                }
                if (x == x0 && y > y0 && y < y1 && z == z0)
                    floorWest = true;
                if (x == x1 && y > y0 && y < y1 && z == z0)
                    floorEast = true;
                if (y == y0 && x > x0 && x < x1 && z == z0)
                    floorSouth = true;
                if (y == y1 && x > x0 && x < x1 && z == z0)
                    floorNorth = true;
                if (x == x0 && y > y0 && y < y1 && z == z1)
                    ceilingWest = true;
                if (x == x1 && y > y0 && y < y1 && z == z1)
                    ceilingEast = true;
                if (y == y0 && x > x0 && x < x1 && z == z1)
                    ceilingSouth = true;
                // Tests at floor height: a solid block north of the floor blocks head room for low zone objects.
                if (y == y1 && x > x0 && x < x1 && z == z0)
                    ceilingNorth = true;
                if (x == x0 && y > y0 && y < y1 && z > z0 && z < z1)
                    wallWest = true;
                if (x == x1 && y > y0 && y < y1 && z > z0 && z < z1)
                    wallEast = true;
                if (y == y0 && x > x0 && x < x1 && z > z0 && z < z1)
                    wallSouth = true;
                if (y == y1 && x > x0 && x < x1 && z > z0 && z < z1)
                    wallNorth = true;
            }
        }
    }

    // The zone objects near the box: one under it supports it, one beside it blocks that side (a low
    // one only where the head room is blocked).
    for (int cellX = cellX0; cellX <= cellX1; cellX++) {
        for (int cellY = cellY0; cellY <= cellY1; cellY++) {
            std::list<Zone::CellRecord> *objects = getCellObjects(cellX, cellY);
            if (!objects)
                continue;
            for (std::list<Zone::CellRecord>::iterator it = objects->begin(); it != objects->end(); ++it) {
                if (creature->m_overlappedObjects.find(it->object) != creature->m_overlappedObjects.end())
                    continue;
                Zone::Object *object = it->object;
                plasma::Vector<3, float> size = object->size;
                if (object->rotation % 2 != 0) {
                    float swap = size[0];
                    size[0] = size[1];
                    size[1] = swap;
                }
                if (centre[0] + Fixed(own[0] * 0.5f) >= object->position[0] - Fixed(size[0] * 0.5f) &&
                    centre[0] - Fixed(own[0] * 0.5f) < object->position[0] + Fixed(size[0] * 0.5f) &&
                    centre[1] + Fixed(own[1] * 0.5f) >= object->position[1] - Fixed(size[1] * 0.5f) &&
                    centre[1] - Fixed(own[1] * 0.5f) < object->position[1] + Fixed(size[1] * 0.5f) &&
                    centre[2] + Fixed(own[2] * 0.5f) - 1 >= object->position[2] &&
                    centre[2] - Fixed(own[2] * 0.5f) - 1 < object->position[2] + Fixed(size[2]))
                    onObject = true;
                if (object->size[2] < LowObjectHeight && !ceiling && !ceilingWest && !ceilingEast && !ceilingSouth &&
                    !ceilingNorth)
                    continue;
                if (object->type == StaticObjectDoor || object->type == StaticObjectBigDoor ||
                    object->type == StaticObjectStompTrap || object->type == StaticObjectSpikeTrap ||
                    object->type == StaticObjectFireTrap)
                    continue;
                if (it->id == creature->m_interactionTarget)
                    continue;
                if (centre[0] + Fixed(own[0] * 0.5f) - 1 >= object->position[0] - Fixed(size[0] * 0.5f) &&
                    centre[0] - Fixed(own[0] * 0.5f) - 1 < object->position[0] + Fixed(size[0] * 0.5f) &&
                    centre[1] + Fixed(own[1] * 0.5f) >= object->position[1] - Fixed(size[1] * 0.5f) &&
                    centre[1] - Fixed(own[1] * 0.5f) < object->position[1] + Fixed(size[1] * 0.5f) &&
                    centre[2] + Fixed(own[2] * 0.5f) >= object->position[2] &&
                    centre[2] - Fixed(own[2] * 0.5f) < object->position[2] + Fixed(size[2]))
                    objectWest = true;
                if (centre[0] + Fixed(own[0] * 0.5f) + 1 >= object->position[0] - Fixed(size[0] * 0.5f) &&
                    centre[0] - Fixed(own[0] * 0.5f) + 1 < object->position[0] + Fixed(size[0] * 0.5f) &&
                    centre[1] + Fixed(own[1] * 0.5f) >= object->position[1] - Fixed(size[1] * 0.5f) &&
                    centre[1] - Fixed(own[1] * 0.5f) < object->position[1] + Fixed(size[1] * 0.5f) &&
                    centre[2] + Fixed(own[2] * 0.5f) >= object->position[2] &&
                    centre[2] - Fixed(own[2] * 0.5f) < object->position[2] + Fixed(size[2]))
                    objectEast = true;
                if (centre[0] + Fixed(own[0] * 0.5f) >= object->position[0] - Fixed(size[0] * 0.5f) &&
                    centre[0] - Fixed(own[0] * 0.5f) < object->position[0] + Fixed(size[0] * 0.5f) &&
                    centre[1] + Fixed(own[1] * 0.5f) - 1 >= object->position[1] - Fixed(size[1] * 0.5f) &&
                    centre[1] - Fixed(own[1] * 0.5f) - 1 < object->position[1] + Fixed(size[1] * 0.5f) &&
                    centre[2] + Fixed(own[2] * 0.5f) >= object->position[2] &&
                    centre[2] - Fixed(own[2] * 0.5f) < object->position[2] + Fixed(size[2]))
                    objectSouth = true;
                if (centre[0] + Fixed(own[0] * 0.5f) >= object->position[0] - Fixed(size[0] * 0.5f) &&
                    centre[0] - Fixed(own[0] * 0.5f) < object->position[0] + Fixed(size[0] * 0.5f) &&
                    centre[1] + Fixed(own[1] * 0.5f) + 1 >= object->position[1] - Fixed(size[1] * 0.5f) &&
                    centre[1] - Fixed(own[1] * 0.5f) + 1 < object->position[1] + Fixed(size[1] * 0.5f) &&
                    centre[2] + Fixed(own[2] * 0.5f) >= object->position[2] &&
                    centre[2] - Fixed(own[2] * 0.5f) < object->position[2] + Fixed(size[2]))
                    objectNorth = true;
            }
        }
    }

    // The axial neighbours: open on that side and something to stand on or hold on to (the floor, an
    // object below, the floor or walls along that side, or water). The west step does not count an
    // object below.
    bool west = false;
    if (!wallWest && !objectWest && (floor || floorWest || wallSouth || wallNorth || edgeSW || edgeNW || inWater)) {
        Creature::PathNode *node = creature->findPathNode(block[0] - 1, block[1], block[2]);
        if (!node) {
            int heuristic =
                pathDistance(plasma::Vector<3, int>(block[0] - 1, block[1], block[2]) - toBlock(creature->m_pathGoal));
            int cost = bestNode->movementCost + 10;
            if (!floor)
                cost += 10;
            creature->setPathNode(block[0] - 1, block[1], block[2],
                                  Creature::PathNode(cost, heuristic, cost + heuristic, block));
            west = true;
        } else if (creature->isPathNodePending(block[0] - 1, block[1], block[2])) {
            int cost = bestNode->movementCost + 10;
            if (!floor)
                cost += 10;
            if (cost < node->movementCost) {
                creature->setPathNode(block[0] - 1, block[1], block[2],
                                      Creature::PathNode(cost, node->heuristic, cost + node->heuristic, block));
                west = true;
            }
        }
    }
    bool east = false;
    if (!wallEast && !objectEast &&
        (floor || onObject || floorEast || wallSouth || wallNorth || edgeSE || edgeNE || inWater)) {
        Creature::PathNode *node = creature->findPathNode(block[0] + 1, block[1], block[2]);
        if (!node) {
            int heuristic =
                pathDistance(plasma::Vector<3, int>(block[0] + 1, block[1], block[2]) - toBlock(creature->m_pathGoal));
            int cost = bestNode->movementCost + 10;
            if (!floor)
                cost += 10;
            creature->setPathNode(block[0] + 1, block[1], block[2],
                                  Creature::PathNode(cost, heuristic, cost + heuristic, block));
            east = true;
        } else if (creature->isPathNodePending(block[0] + 1, block[1], block[2])) {
            int cost = bestNode->movementCost + 10;
            if (!floor)
                cost += 10;
            if (cost < node->movementCost) {
                creature->setPathNode(block[0] + 1, block[1], block[2],
                                      Creature::PathNode(cost, node->heuristic, cost + node->heuristic, block));
                east = true;
            }
        }
    }
    bool south = false;
    if (!wallSouth && !objectSouth &&
        (floor || onObject || floorSouth || wallWest || wallEast || edgeSW || edgeSE || inWater)) {
        Creature::PathNode *node = creature->findPathNode(block[0], block[1] - 1, block[2]);
        if (!node) {
            int heuristic =
                pathDistance(plasma::Vector<3, int>(block[0], block[1] - 1, block[2]) - toBlock(creature->m_pathGoal));
            int cost = bestNode->movementCost + 10;
            // This step costs twenty; other steps and already discovered destinations cost ten.
            if (!floor)
                cost += 20;
            creature->setPathNode(block[0], block[1] - 1, block[2],
                                  Creature::PathNode(cost, heuristic, cost + heuristic, block));
            south = true;
        } else if (creature->isPathNodePending(block[0], block[1] - 1, block[2])) {
            int cost = bestNode->movementCost + 10;
            if (!floor)
                cost += 10;
            if (cost < node->movementCost) {
                creature->setPathNode(block[0], block[1] - 1, block[2],
                                      Creature::PathNode(cost, node->heuristic, cost + node->heuristic, block));
                south = true;
            }
        }
    }
    bool north = false;
    if (!wallNorth && !objectNorth &&
        (floor || onObject || floorNorth || wallWest || wallEast || edgeNW || edgeNE || inWater)) {
        Creature::PathNode *node = creature->findPathNode(block[0], block[1] + 1, block[2]);
        if (!node) {
            int heuristic =
                pathDistance(plasma::Vector<3, int>(block[0], block[1] + 1, block[2]) - toBlock(creature->m_pathGoal));
            int cost = bestNode->movementCost + 10;
            if (!floor)
                cost += 10;
            creature->setPathNode(block[0], block[1] + 1, block[2],
                                  Creature::PathNode(cost, heuristic, cost + heuristic, block));
            north = true;
        } else if (creature->isPathNodePending(block[0], block[1] + 1, block[2])) {
            int cost = bestNode->movementCost + 10;
            if (!floor)
                cost += 10;
            if (cost < node->movementCost) {
                creature->setPathNode(block[0], block[1] + 1, block[2],
                                      Creature::PathNode(cost, node->heuristic, cost + node->heuristic, block));
                north = true;
            }
        }
    }

    // The diagonal neighbours: both axial steps taken, the corner column open, and the corner's floor
    // (or the floor, an object below, water).
    if (!edgeSW && west && south && (floor || onObject || floorSW || inWater)) {
        Creature::PathNode *node = creature->findPathNode(block[0] - 1, block[1] - 1, block[2]);
        if (!node) {
            int heuristic = pathDistance(plasma::Vector<3, int>(block[0] - 1, block[1] - 1, block[2]) -
                                         toBlock(creature->m_pathGoal));
            int cost = bestNode->movementCost + 14;
            if (!floor)
                cost += 10;
            creature->setPathNode(block[0] - 1, block[1] - 1, block[2],
                                  Creature::PathNode(cost, heuristic, cost + heuristic, block));
        } else if (creature->isPathNodePending(block[0] - 1, block[1] - 1, block[2])) {
            int cost = bestNode->movementCost + 14;
            if (!floor)
                cost += 10;
            if (cost < node->movementCost)
                creature->setPathNode(block[0] - 1, block[1] - 1, block[2],
                                      Creature::PathNode(cost, node->heuristic, cost + node->heuristic, block));
        }
    }
    if (!edgeSE && east && south && (floor || onObject || floorSE || inWater)) {
        Creature::PathNode *node = creature->findPathNode(block[0] + 1, block[1] - 1, block[2]);
        if (!node) {
            int heuristic = pathDistance(plasma::Vector<3, int>(block[0] + 1, block[1] - 1, block[2]) -
                                         toBlock(creature->m_pathGoal));
            int cost = bestNode->movementCost + 14;
            if (!floor)
                cost += 10;
            creature->setPathNode(block[0] + 1, block[1] - 1, block[2],
                                  Creature::PathNode(cost, heuristic, cost + heuristic, block));
        } else if (creature->isPathNodePending(block[0] + 1, block[1] - 1, block[2])) {
            int cost = bestNode->movementCost + 14;
            if (!floor)
                cost += 10;
            if (cost < node->movementCost)
                creature->setPathNode(block[0] + 1, block[1] - 1, block[2],
                                      Creature::PathNode(cost, node->heuristic, cost + node->heuristic, block));
        }
    }
    if (!edgeNW && north && west && (floor || onObject || floorNW || inWater)) {
        Creature::PathNode *node = creature->findPathNode(block[0] - 1, block[1] + 1, block[2]);
        if (!node) {
            int heuristic = pathDistance(plasma::Vector<3, int>(block[0] - 1, block[1] + 1, block[2]) -
                                         toBlock(creature->m_pathGoal));
            int cost = bestNode->movementCost + 14;
            if (!floor)
                cost += 10;
            creature->setPathNode(block[0] - 1, block[1] + 1, block[2],
                                  Creature::PathNode(cost, heuristic, cost + heuristic, block));
        } else if (creature->isPathNodePending(block[0] - 1, block[1] + 1, block[2])) {
            int cost = bestNode->movementCost + 14;
            if (!floor)
                cost += 10;
            if (cost < node->movementCost)
                creature->setPathNode(block[0] - 1, block[1] + 1, block[2],
                                      Creature::PathNode(cost, node->heuristic, cost + node->heuristic, block));
        }
    }
    if (!edgeNE && east && north && (floor || onObject || floorNE || inWater)) {
        Creature::PathNode *node = creature->findPathNode(block[0] + 1, block[1] + 1, block[2]);
        if (!node) {
            int heuristic = pathDistance(plasma::Vector<3, int>(block[0] + 1, block[1] + 1, block[2]) -
                                         toBlock(creature->m_pathGoal));
            int cost = bestNode->movementCost + 14;
            if (!floor)
                cost += 10;
            creature->setPathNode(block[0] + 1, block[1] + 1, block[2],
                                  Creature::PathNode(cost, heuristic, cost + heuristic, block));
        } else if (creature->isPathNodePending(block[0] + 1, block[1] + 1, block[2])) {
            int cost = bestNode->movementCost + 14;
            if (!floor)
                cost += 10;
            if (cost < node->movementCost)
                creature->setPathNode(block[0] + 1, block[1] + 1, block[2],
                                      Creature::PathNode(cost, node->heuristic, cost + node->heuristic, block));
        }
    }

    // Falling: nothing below.
    if (!floor && !onObject) {
        Creature::PathNode *node = creature->findPathNode(block[0], block[1], block[2] - 1);
        if (!node) {
            int heuristic =
                pathDistance(plasma::Vector<3, int>(block[0], block[1], block[2] - 1) - toBlock(creature->m_pathGoal));
            int cost = bestNode->movementCost;
            creature->setPathNode(block[0], block[1], block[2] - 1,
                                  Creature::PathNode(cost, heuristic, cost + heuristic, block));
        } else if (creature->isPathNodePending(block[0], block[1], block[2] - 1)) {
            int cost = bestNode->movementCost;
            if (cost < node->movementCost)
                creature->setPathNode(block[0], block[1], block[2] - 1,
                                      Creature::PathNode(cost, node->heuristic, cost + node->heuristic, block));
        }
    }
    // Climbing: head room, and a wall to hold on to or water.
    if (!ceiling && (wallWest || wallEast || wallSouth || wallNorth || inWater)) {
        Creature::PathNode *node = creature->findPathNode(block[0], block[1], block[2] + 1);
        if (!node) {
            int heuristic =
                pathDistance(plasma::Vector<3, int>(block[0], block[1], block[2] + 1) - toBlock(creature->m_pathGoal));
            int cost = bestNode->movementCost;
            if (!floor)
                cost += 40;
            creature->setPathNode(block[0], block[1], block[2] + 1,
                                  Creature::PathNode(cost, heuristic, cost + heuristic, block));
        } else if (creature->isPathNodePending(block[0], block[1], block[2] + 1)) {
            int cost = bestNode->movementCost;
            if (!floor)
                cost += 40;
            if (cost < node->movementCost)
                creature->setPathNode(block[0], block[1], block[2] + 1,
                                      Creature::PathNode(cost, node->heuristic, cost + node->heuristic, block));
        }
    }
    return true;
}

}

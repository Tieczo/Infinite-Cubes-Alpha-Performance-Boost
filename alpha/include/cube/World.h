// World owns zones, creatures, region records and shared simulation state. Construction
// initializes the world and dictionaries; destruction releases owned state. Unknown state remains
// opaque.
#pragma once
#include <windows.h>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>
#include "plasma/Vector.h"
#include "cube/Block.h"
#include "cube/BulletPacket.h"
#include "cube/Creature.h"
#include "cube/Database.h"
#include "cube/Fixed.h"
#include "cube/KillPacket.h"
#include "cube/Region.h"
#include "cube/SkillIds.h"
#include "cube/Speech.h"
#include "cube/SpriteManager.h"
#include "cube/Zone.h"
#include "cube/SkillIds.h"

namespace cube {

class Creature;
class House;
struct Formula;
struct Item;
class Sprite;
struct BulletPacket;
struct HitPacket;
struct ServerUpdates;

// World:placeSprite: colored voxels become doors, signs, crafting stations and residents of these
// roles; PlacementWater fills blue voxels.
enum SpritePlacementStyle {
    PlacementPlain = 0,
    PlacementHouse = 1,
    PlacementInn = 2,
    PlacementItemShop = 3,
    PlacementWeaponShop = 4,
    PlacementArmorShop = 5,
    PlacementAnalyzer = 6,
    PlacementAdapter = 7,
    PlacementWater = 8,
    PlacementWarriorHall = 9,
    PlacementRangerHall = 10,
    PlacementRogueHall = 11,
    PlacementMageHall = 12,
    PlacementSmithy = 13,
    PlacementCarpentersShop = 14,
    PlacementWeavingMill = 15,
};

// Building-list model IDs, named by the files loaded in World::World.
enum BuildingModel {
    BuildingWoodFloor = 0,
    BuildingWindowWall = 1,
    BuildingDoorWall = 2,
    BuildingRoof1 = 3,
    BuildingEntrance = 4,
    BuildingWall = 5,
    BuildingRoof2 = 6,
    BuildingRoof3 = 7,
    BuildingBalconyWall = 8,
    BuildingRoofWall = 9,
    BuildingStoneWell = 10,
    BuildingStoneStairs = 11
};

// World owns zones, creatures, region records and shared simulation state. Construction sets up the initial world and its dictionaries; destruction releases owned state. Unknown state remains opaque. Flags use bit indices and are combined as (1 << flag).
enum WorldDebugFlag {
    WorldDebugFastMovement = 0
};

// The path search's callers (the behaviors, GameController's player) give it up to
// PathSearchRoundsPerUpdate rounds of continuePathSearch per update, until the path holds more
// than PathSearchLengthLimit blocks.
enum {
    PathSearchRoundsPerUpdate = 10,
    PathSearchLengthLimit = 50
};
// A behavior's search starts a tenth of a block above the creature's feet; a companion's or an
// interaction's ends within PathGoalRadius of the goal (Creature::m_pathRadius).
enum {
    PathGoalRadius = 2
};

class World {
public:
    // Applies a saved block change using the default-block helpers in WorldBlocks.h.
    void setBlock(int x, int y, int z, const Block &block, Zone *zone);

    // RegionRecord stores the region column, firelands flag, humidity, temperature, seed and bookkeeping count. Landscape tiles copy it and measure distance from its column.
    struct RegionRecord {
        void generationRange(int &minimum, int &maximum);
        plasma::Vector<2, int> column; // a block column (distanceSquared)
        bool firelands;
        float humidity;
        float temperature;
        int seed;   // a seed
        int height; // landscape height offset in blocks
    };

    // Builds the members, sets up the world with seed 123 and no name, fills the key tables and their
    // reverse tables, loads the building models and places the spawn point. The flag is m_client.
    World(IDirect3DDevice9 *device, bool client);
    virtual ~World();

    // Returns the region at region coordinates, or null when unloaded. A region spans 64 by 64 zones.
    Region *getRegion(int x, int y)
    {
        if (x < 0 || y < 0 || x >= 0x400 || y >= 0x400)
            return 0;
        return m_regions[x][y];
    }
    // The record of a region (region coordinates, 0..0x3ff), or null.
    RegionRecord *getRegionRecord(int x, int y)
    {
        if (x < 0 || y < 0 || x >= 0x400 || y >= 0x400)
            return 0;
        return m_regionRecords[x][y];
    }
    // Generates a region landscape record using its derived seed.
    RegionRecord *generateRegionRecord(int x, int y);
    // The zone at zone coordinates, or null when it is not loaded.
    Zone *zone(int x, int y)
    {
        if (x < 0 || y < 0 || x >= 0x10000 || y >= 0x10000)
            return 0;
        Region *region = getRegion(x / 64, y / 64);
        if (!region)
            return 0;
        return region->m_zones[x % 64][y % 64];
    }
    // The cell of a zone column, or null when its region is not loaded.
    Region::Cell *getCell(int x, int y)
    {
        if (x < 0 || y < 0 || x >= 0x10000 || y >= 0x10000)
            return 0;
        Region *region = getRegion(x / 64, y / 64);
        if (!region)
            return 0;
        return &region->m_cells[(x % 64) * 64 + y % 64];
    }
    // The record of an 8 x 8 zone square, or null when its region is not loaded.
    Region::Record *getRecord(int x, int y)
    {
        if (x < 0 || y < 0 || x >= 0x2000 || y >= 0x2000)
            return 0;
        Region *region = getRegion(x * 8 / 64, y * 8 / 64);
        if (!region)
            return 0;
        return &region->m_records[(x % 8) * 8 + y % 8];
    }
    // The objects of an 8 x 8 column cell, or null when its zone is not loaded.
    std::list<Zone::CellRecord> *getCellObjects(int x, int y, Zone *zone = 0)
    {
        if (x < 0 || y < 0 || x >= 0x200000 || y >= 0x200000)
            return 0;
        if (!zone)
            zone = this->zone(x / 32, y / 32);
        else if (x < zone->m_position[0] * 256 / 8 || y < zone->m_position[1] * 256 / 8 ||
                 x >= zone->m_position[0] * 256 / 8 + 32 || y >= zone->m_position[1] * 256 / 8 + 32)
            return 0;
        if (!zone)
            return 0;
        return &zone->m_cells[(y % 32) * 32 + x % 32];
    }
    // A smooth offset of the landscape at block column (x, y), in blocks: noise of y and of x (seeds
    // 3423 and 23421) times 768. Both noise channels feed nearestRegionRecord and the map.
    plasma::Vector<2, float> landscapeOffset(int x, int y);
    // Blends regional temperature around a block column; zone generation stores it in Field::temperature.
    float blendedTemperature(int x, int y);
    // The influence of the zone square's record at block column (x, y) when the record is of kind 1,
    // else 0.
    float recordInfluence(int x, int y);
    // Returns the nearest loaded region record within one region of a noise-displaced block column, or
    // null.
    RegionRecord *nearestRegionRecord(int x, int y)
    {
        int minimumX = (x - 0x4000) / 0x4000;
        int minimumY = (y - 0x4000) / 0x4000;
        int maximumX = (x + 0x4000) / 0x4000;
        int maximumY = (y + 0x4000) / 0x4000;
        plasma::Vector<2, float> offset = landscapeOffset(x, y);
        int positionX = (int)(x + offset[0]);
        int positionY = (int)(y + offset[1]);
        RegionRecord *nearest = 0;
        int nearestDistance = 0;
        for (int i = minimumX; i <= maximumX; i++) {
            for (int j = minimumY; j <= maximumY; j++) {
                RegionRecord *record = getRegionRecord(i, j);
                if (record) {
                    int distance = (int)distanceSquared(record->column, positionX, positionY);
                    if (!nearest || distance < nearestDistance) {
                        nearest = record;
                        nearestDistance = distance;
                    }
                }
            }
        }
        return nearest;
    }
    // Terrain colour sampled at a block position.
    plasma::Vector<3, float> rockColor(int x, int y, int z, Zone *zone);
    plasma::Vector<3, float> snowColor(int x, int y);
    // The squared distance in blocks from a block column to the position (x, y).
    float distanceSquared(const plasma::Vector<2, int> &column, Fixed x, Fixed y);
    // Tests whether a creature box at the position collides with terrain.
    bool isBlocked(const plasma::Vector<3, Fixed> &position, const plasma::Vector<3, float> &size, bool testObjects);
    // Terrain-support cleanup and buff delivery (World.cpp).
    void removeUnsupportedPlants(int x1, int y1, int x2, int y2);
    void applyBuff(Creature *creature, const Creature::Buff &buff, ServerUpdates *updates);
    // Applies a hit to its target.
    void applyHit(const HitPacket &hit, ServerUpdates *updates, std::set<Zone *> *changedZones);
    // Resolves one creature's attack on another and queues the resulting hits. False when the attack
    // is void, missed or blocked.
    bool attackCreature(Creature *target, Creature *attacker, float damage, bool critical, bool powered,
                        float stunChance, const plasma::Vector<3, Fixed> &position,
                        const plasma::Vector<3, float> &direction, ServerUpdates *updates,
                        std::set<Zone *> *changedZones, bool magical, int ability, BulletPacket *projectile,
                        bool allowBlocking);
    // The path search between the creature's m_pathStart and m_pathGoal: set up, expand one block
    // (true while the search goes on) and turn the result into the creature's m_path.
    void beginPathSearch(Creature *creature);
    bool continuePathSearch(Creature *creature);
    void finishPathSearch(Creature *creature);
    // Where a dead player revives.
    plasma::Vector<3, Fixed> respawnPosition(const plasma::Vector<3, Fixed> &position);
    // A class skill with its own buff (GameController::update passes the skills 97 and 100..103).
    // ApplyBuffSkill (the buff a skill gives its user, with its packet)
    void applyBuffSkill(Creature *creature, int ability, ServerUpdates *updates);
    // Maintains the path-search tree and steers along its waypoints; update consumes the
    // waypoint-steering result.
    bool followPath(Creature *creature, int elapsed);
    // A block column, optionally restricted to an already selected zone.
    Field *getField(int x, int y, Zone *zone = 0)
    {
        if (x < 0 || y < 0 || x >= 0x1000000 || y >= 0x1000000)
            return 0;
        if (!zone)
            zone = this->zone(x / 256, y / 256);
        else if (x < zone->m_position[0] * 256 || y < zone->m_position[1] * 256 ||
                 x >= zone->m_position[0] * 256 + 256 || y >= zone->m_position[1] * 256 + 256)
            return 0;
        if (!zone)
            return 0;
        return &zone->m_fields[x % 256 + (y % 256) * 256];
    }

    const Block &getBlock(Fixed x, Fixed y, Fixed z, Zone *zone);

    // Missing terrain is solid. Above a column, sea level separates water from air; stored air below
    // sea level becomes water unless bit 6 is set.
    const Block &getBlock(int x, int y, int z, Zone *zone);

    // Clears a stored block to water at or below sea level and air above it.
    void destroyBlock(int x, int y, int z);

    // Lights the block columns of the rectangle (x0, y0)..(x1, y1) and margin columns around it.
    void computeLight(int x0, int y0, int x1, int y1, int margin, Zone *zone);
    // Block light in the range zero to 255: full for luminous blocks, at least five from the color
    // channel of air and water, and zero otherwise.
    int cubeLight(int x, int y, int z, Zone *zone, int channel);

    // The creature with the id, or null; m_creatures.find(id), else null.
    Creature *creatureByID(const long long &id)
    {
        std::map<long long, Creature *>::iterator it = m_creatures.find(id);
        if (it == m_creatures.end())
            return 0;
        return it->second;
    }
    // Probes straight walking with the creature's box, terrain climbs and an arrival tolerance of
    // width. The probe also succeeds after more than 25 blocks of progress.
    bool canWalkStraight(Creature *creature, const plasma::Vector<3, Fixed> &from, const plasma::Vector<3, Fixed> &to,
                         float width);
    // Moves a living creature out of the terrain (onto the top of the blocks its box overlaps) and out
    // of the airships within 64 blocks (to the nearest free place beside the voxels it overlaps).
    // Called by update for every creature; it corrects position after terrain and airship overlap.
    void resolveCollisions(Creature *creature);
    // The level of the creatures in a region: 1 in the central region (0x200, 0x200), rising by 0.75
    // per region of distance.
    int regionLevel(int x, int y);
    // Unload the record, the region (saved first) or the zone (saved unless the world saves nothing)
    // at region or zone coordinates, under both locks.
    void removeRecord(int x, int y);
    void removeRegion(int x, int y);
    void removeZone(int x, int y);
    // Generates the zone at zone coordinates; it fills and installs the Zone.
    void generateZone(int x, int y);
    // Zone generation helpers.
    float blendedFirelands(int x, int y); // Field::firelands.
    float vegetationDensity(int x, int y, Zone *zone);
    float oceanShare(int x, int y);
    Block terrainBlock(int x, int y, int z, float humidity, float temperature, Zone *zone);
    plasma::Vector<3, float> groundColor(int x, int y, int z, Zone *zone);
    // Base terrain color from four noise layers, clamped to zero through 255.
    plasma::Vector<3, float> soilColor(int x, int y);
    // Sand's colour at a block column. sandColor has the same noise-palette role.
    plasma::Vector<3, float> sandColor(int x, int y);
    // A lighter sand at a block column, from the sand's noise seeds. terrainBlock selects this
    // brighter sand palette for shore columns.
    plasma::Vector<3, float> beachColor(int x, int y);
    int levelAt(Fixed x, Fixed y);
    // Raises a rock, arch or tree of the given extent at the ground position.
    void buildRock(int x, int y, int z, int sizeX, int sizeY, int sizeZ, Zone *zone);
    void buildArch(int x, int y, int z, int sizeX, int sizeY, int sizeZ, Zone *zone);
    void buildTree(int x, int y, int z, int radius, int height, int kind, Zone *zone);
    // A blob of foliage of a radius around (x, y, z), in two colours, for a tree kind.
    void growFoliage(int x, int y, int z, float radius, float inner, plasma::Vector<3, float> color,
                     plasma::Vector<3, float> lightColor, int kind, Zone *zone);
    // How far a column lies from a river or a paved road, in falloff units the generator uses 1 - 50 *
    // distance.
    float riverDistance(int x, int y, Zone *zone);
    float roadDistance(int x, int y, Zone *zone);
    // The distance from a block column to the network of roads between the region records.
    float roadNetworkDistance(int x, int y);
    // The field's temperature of a column, or computed when the column is not loaded.
    float terrainTemperature(int x, int y, Zone *zone);
    // Lays out the dungeon of a region cell in the zone.
    void generateDungeon(Zone *zone, Region *region, int x, int y, int variant, bool large);
    // Dungeon generation helpers fill boxes with shaded interior stone and add objects. Flags are combined with block types.
    void fillBox(int x, int y, int z, const plasma::Vector<3, int> &size, const plasma::Vector<3, unsigned char> &color,
                 float roughness, Zone *zone, int flags);
    // Fills a box with interior blocks of type 6 in a colour.
    void fillInteriorBox(int x, int y, int z, const plasma::Vector<3, int> &size,
                         const plasma::Vector<3, unsigned char> &color, Zone *zone);
    // A wall torch for a dungeon variant.
    static Zone::Plant makeTorch(int variant, const plasma::Vector<3, Fixed> &position, float rotation);
    // An object against a dungeon wall: a chest, shelves, a table or a stool.
    static Zone::Object makeWallObject(const plasma::Vector<3, Fixed> &position, int rotation, int variant);
    // Fills creature inventory for a level and quality.
    static void fillInventory(Inventory *inventory, int level, int quality);
    // Builds a sprite's voxels into the world as blocks of blockType (plus the interior flag), turned
    // by rotation and cropped by margins; marker colours become objects, plants and creatures in the
    // building styles, and updates (when given) receives every block placed.
    void placeSprite(Sprite *sprite, const plasma::Vector<3, int> &position, int rotation, int blockType, int style,
                     Zone *zone, bool foundation, const plasma::Vector<4, int> &margins, ServerUpdates *updates);
    // Settles an object on the ground at its position; false where it cannot stand.
    bool settleObject(Zone::ObjectState &object, Zone *zone, bool needsFloor);
    // Puts up a camp of zone objects; false when there is no room.
    bool placeCamp(Zone *zone, const plasma::Vector<3, Fixed> &position);
    // Lays out a region record's settlement in a zone: houses, a market, fields, special buildings and
    // their inhabitants; false when no plot can hold a house.
    bool generateHouse(Zone *zone, Region::Record *record);
    // The colour of a paving stone at a block: a grey from two noises of the column and height, tinted
    // by three seeded noises.
    plasma::Vector<3, float> pavingColor(int x, int y, int z);
    // Fills a plaza vendor's stock. fillGeneralShop.
    void fillGeneralShop(Spawn *spawn, int x, int y);
    // Fills a region record's places with its creatures.
    void populateRecord(Zone *zone, Region::Record *record, std::vector<plasma::Vector<3, Fixed>> &places, int count);
    // A wild creature's race for a block at height z : fish below sea level, humanoid races when
    // asked, else by the terrain parameters; generateZone stores the returned race in its spawns.
    int randomCreatureRace(int x, int y, int z, bool humanoids);
    // Brings a zone's spawns up to date: its square's monster, or a fresh population.
    void refreshZonePopulation(Zone *zone);
    // Populates a zone afresh with a band of humanoids and adds days to its m_populationDays.
    void populateZone(Zone *zone, int days);
    // Gives an 8 x 8 zone square a new mission and queues its MissionPacket.
    void generateMission(int x, int y, ServerUpdates *updates);
    // Allocates, fills and installs a Region.
    Region *generateRegion(int x, int y);
    // Blends RegionRecord::humidity into Field::humidity.
    float blendedHumidity(int x, int y);
    // The sky colours, the lights and the fog at a position: render passes the first three to setSky
    // and the next two to setLight. Cube only.
    void lighting(const plasma::Vector<3, Fixed> &position, plasma::Vector<4, float> &skyColor0,
                  plasma::Vector<4, float> &skyColor1, plasma::Vector<4, float> &skyColor2,
                  plasma::Vector<4, float> &frontLight, plasma::Vector<4, float> &backLight,
                  plasma::Vector<4, float> &fogColor);
    // Advances the world by the elapsed milliseconds, collecting the events for the clients in
    // outgoing and applying the clients' packets from incoming, which may be null; both callers supply
    // tick milliseconds.
    void update(int milliseconds, ServerUpdates *outgoing, ServerUpdates *incoming);
    // The region (region coordinates) that a block column belongs to: the region whose record lies
    // nearest to the column displaced by noise. findNearestRegion, the same search.
    plasma::Vector<2, int> findNearestRegion(int x, int y);
    // Populates a region for the day: its monsters and missions, then its zones' spawns; sets
    // Region::m_populated.
    void populateRegion(int x, int y, ServerUpdates *updates);
    // The loaded region that a block column belongs to, or null.
    Region *regionAtColumn(int x, int y);
    // The wind a glider feels at a position: noise of the position, added to its velocity per second;
    // wind has the same glider role.
    plasma::Vector<3, float> wind(const plasma::Vector<3, Fixed> &position, const plasma::Vector<3, float> &velocity);
    // Gives a companion a sequence of combat and owner-following behaviors.
    void initCompanion(long long ownerId, Creature *companion);
    // Returns the block position unchanged.
    plasma::Vector<3, int> buildingPosition(const plasma::Vector<3, int> &position);
    // The time of day.
    int getTime() const { return m_time; }
    // The animation clock.
    int getClock() const { return m_clock; }
    // Deletes the creatures that are not players and whose spawn is in a loaded zone; their spawn
    // countdown is reset to zero.
    void resetCreatures();
    // Whether a creature may attack another (areEnemies), and whether two creatures are hostile.
    bool areEnemies(Creature *creature, Creature *other);
    bool isHostile(Creature *creature, Creature *other);
    // The target was attacked: aggro updates for it and its allies; onAttack queues the same aggro and
    // nearby alerts.
    void onAttack(Creature *attacker, Creature *target, ServerUpdates *updates);
    // Changes a zone object's active state and queues its sound.
    void setObjectActive(Zone::ObjectState *object, ServerUpdates *updates, bool active);
    // Settles a kill: xp for the local player and the killer's companion; GameController dispatches
    // KillPackets to this hook,.
    void onKill(const KillPacket &kill);
    // The light level at a world position in 0..1: the red channel (at least 5) of an air or water
    // block, full in a block of type 13, none in other blocks; returns the block light divided by 255.
    float lightLevel(const plasma::Vector<3, Fixed> &position);
    // The building model with the index, or null.
    Sprite *buildingSprite(int modelIndex);
    // The element of a zone's m_items or m_objects by id: zone coordinates and the index in the zone;
    // null when the zone is not loaded or the index is out of range. item and object have the same
    // lookup roles and order.
    Zone::ZoneItem *item(const plasma::Vector<3, int> &id);
    Zone::Object *object(const plasma::Vector<3, int> &id);
    // Dictionary keys by identifier, or an empty string for an unknown identifier.
    std::wstring abilityKey(int id);
    std::wstring creatureKey(int id);
    std::wstring skillKey(int id);
    // The keys by two ints, or L"".
    std::wstring settlementKey(int type, int subtype);
    std::wstring structureKey(int type, int subtype);
    std::wstring itemKey(int type, int subtype);
    // The dictionary key of an item material.
    std::wstring materialKey(int material);
    // The display name of an item.
    std::wstring createItemName(const Item &item);
    // The terrain height at a block column.
    float terrainHeight(int x, int y, Zone *zone);
    // Writes the region's missions and monsters to the world database.
    void saveRegion(int x, int y);
    // Writes a changed zone to the world database.
    void saveZone(Zone *zone);

    Item randomArmor(int level, int rarity, int classType);
    Item randomFormula(int level, int rarity);
    Item randomManaCube(int level, int rarity);
    Item randomItem(int level, int rarity);
    // Equips a spawn for its class and race, setting stat multipliers unless keepMultipliers is true.
    void equip(Spawn *spawn, bool keepMultipliers);
    // Stocks a weapon vendor's inventory. fillWeaponShop.
    void fillWeaponShop(Spawn *spawn, int x, int y);
    // Stocks an armour vendor's inventory. fillArmorShop. The three shop fillers follow equip in the
    // code order of both executables, as these
    void fillArmorShop(Spawn *spawn, int x, int y);
    // Leaves an item lying on the ground in its zone; appends ZoneItem and marks changedZones,.
    void dropItem(const Item &item, const plasma::Vector<3, Fixed> &position, float rotation, float scale,
                  std::set<Zone *> *changedZones);
    // Drops a killed creature's loot around it; applyHit calls it on death, as the release calls
    // DropLoot.
    void dropLoot(Creature *creature, std::set<Zone *> *changedZones);
    int randomHostileRace(int x, int y, int level);
    int randomMonsterRace(int x, int y, int level);
    // Marks every region unpopulated (a new day; update passes its outgoing updates, which are not
    // read).
    void clearRegionPopulationState(ServerUpdates *updates);
    int randomRaceVariant(int x, int y, int level, int race);
    Item randomPetFood(int level, int rarity);
    Item randomWeapon(int level, int rarity, int classType);
    // Starts a named world from its seed, draws random parameters, unloads and saves old regions,
    // opens Save/world_<name>.db and restores its saved time.
    void setup(int seed, const std::string &name);

    // A 0..1 factor for tinting a block at the position green.
    float colorBlend(int x, int y, int z, Zone *zone);
    // Clears a box of blocks, optionally with rough walls. Named like StructureBuilder::cutBox (a box
    // of empty blocks):
    void cutBox(int x, int y, int z, const plasma::Vector<3, int> &size, Zone *zone, bool rough);
    // Column humidity, computed from region records when the column is unloaded.
    float terrainHumidity(int x, int y, Zone *zone);
    // How densely plants grow at a block column.
    float vegetation(int x, int y, Zone *zone);
    // Particle bursts for an item's glowing spirits. createSpiritParticles
    void createSpiritParticles(const plasma::Vector<3, Fixed> &position, const plasma::Vector<3, float> &acceleration,
                               float scale, bool strong, const Item &item, ServerUpdates *updates);
    // The id of the zone object nearest to a position, within 4 blocks.
    plasma::Vector<3, int> nearestObject(const plasma::Vector<3, Fixed> &position);
    // Whether solid ground lies under a box; it tests the solid layer 0.2 blocks below the box's
    // bottom.
    bool isOnGround(const plasma::Vector<3, Fixed> &position, const plasma::Vector<3, float> &size);
    // Sets the blocks of a circle at height z or of a sphere; the 2D/3D squared-radius tests bound
    // SetBlock.
    void fillCircle(int x, int y, int z, float radius, const Block &block, Zone *zone, bool centered, bool tinted);
    void fillSphere(int x, int y, int z, float radius, const Block &block, Zone *zone, bool centered, bool tinted);
    // Fills the recipe for item; GameController's crafting screen shows its ingredients and required
    // Workstation.
    bool findFormula(const Item &item, Formula &formula);

    // The house whose interior contains these block coordinates.
    House *getHouse(int x, int y, int z);

    // Whether the straight line from a to b is free and no longer than the range. The flag is passed
    // on to rayLength.
    bool inRange(const plasma::Vector<3, Fixed> &a, const plasma::Vector<3, Fixed> &b, bool objects, float range);
    // How far a ray from the position goes in the direction, at most the length, through blocks whose
    // solidity is `solid`.
    float rayLength(const plasma::Vector<3, Fixed> &position, const plasma::Vector<3, float> &direction, float length,
                    bool solid, bool objects);

    // A point near (x, y) / 16384, displaced by noise; generateZone and roadDistance consume the two
    // seeded coordinate displacements.
    plasma::Vector<2, double> jitter(int x, int y);

    // Random rarity up to four, with three progressively rarer bonuses.
    int randomRarity(int rarity, bool increase);

    // The world name determines save filenames.
    const std::string &getName() const { return m_name; }
    // World seed.
    int getSeed() const { return m_seed; }

    plasma::Vector<3, float> &spawnPoint() { return m_spawnPoint; }
    void setLocalPlayer(Creature *player) { m_localPlayer = player; }
    // Define dictionary variables and tags for a dialog context.
    void addCreature(const std::wstring &name, int race, int seed, int variant, Speech::Context &context);
    void addItem(const std::wstring &name, const Item &item, Speech::Context &context);
    void addScenery(const std::wstring &name, Region::Record *scenery, Speech::Context &context);

    // Dictionary key selected by landscape kind and variant.
    std::wstring landscapeKey(int kind, int variant);
    // A proper name from syllable tables by race. the release's randomName
    std::wstring randomName(unsigned int seed, int race);

    std::wstring cellKey(Region::Cell *cell);
    // A two-syllable place name for a block column (World.cpp).
    std::wstring generateCoordinateName(int x, int y);

    // Protects region and zone access with separate locks.
    void enterRegions();
    void enterZones();
    void leaveRegions();
    void leaveZones();

    // The creatures by id, owned. LookAtPlayerBehavior::Update walks the tree from the head at and
    // reads each node's Creature* at, after the 8-byte key (World keeps its creatures by id the same
    // way).
    std::map<long long, Creature *> m_creatures;
    // Owned airships, deleted with the world.
    std::map<long long, Zone::Airship *> m_airships;
    // Projectiles moved and resolved by world updates.
    std::list<BulletPacket> m_projectiles;
    SpriteManager m_spriteManager;
    Speech m_speech;
    int m_debugFlags; // Initially zero.
    // Pet-food subtype identifiers used by randomPetFood and Speech.
    std::vector<int> m_petFoodRaces;
    // World name used in Save/world_<name>.db.
    std::string m_name;
    Database m_database;
    // The constructor client flag; while set, the world saves no zones or regions.
    bool m_client;
    Creature *m_localPlayer; // Initially null; assigned the local player by GameController.
    // The creature distance option (1 = the original): spawn and despawn radii are multiplied by it,
    // and creatures are brought to life from the zones within that reach of a player.
    float m_creatureDistanceScale;
    // The fast creature updates option: during the creatures' steps, pushing each other apart and
    // looking for targets go through the creatures in the cells around a creature instead of all of
    // them (the same creatures in the same order, minus those too far away to matter).
    bool m_fastCreatureUpdates;
    // Sorts the creatures into cells of CreatureCellSize blocks (at the start of the creatures' steps).
    void buildCreatureCells();
    // The creatures in the cells within range of (x, y), sorted by id like m_creatures; false when the
    // cells are not in use or out of date (a creature was added since), and m_creatures must be gone
    // through instead.
    bool nearbyCreatures(float x, float y, float range, std::vector<std::pair<long long, Creature *>> &found);
    std::unordered_map<long long, std::vector<std::pair<long long, Creature *>>> m_creatureCells;
    size_t m_creatureCellCount;   // m_creatures' size when the cells were made; 0 when not in use
    float m_creatureCellExtent;   // the largest horizontal size among the creatures (blocks)
    // Owned regions indexed by region coordinates; each spans 64-by-64 zones.
    Region *m_regions[0x400][0x400];
    RegionRecord *m_regionRecords[0x400][0x400]; // owned
    int m_clock;                                 // animation clock
    CRITICAL_SECTION m_regionLock;
    CRITICAL_SECTION m_zoneLock;
    // Spawn point in block coordinates. Construction samples terrain height at (8396928, 8396928);
    // setup uses zero height until generation updates it.
    plasma::Vector<3, float> m_spawnPoint;
    plasma::Vector<2, int> unused8000fc; // Initially (32768, 32768), otherwise unused.
    // Identifier-to-key dictionaries for creatures, objects, skills and abilities.
    std::map<int, std::wstring> m_creatureKeys;
    std::map<int, std::wstring> m_staticKeys;
    std::map<int, std::wstring> m_skillKeys;
    std::map<int, std::wstring> m_abilityKeys;
    // Dictionary keys indexed by a pair of integers for settlements, structures and item type/subtype.
    std::map<std::pair<int, int>, std::wstring> m_settlementKeys;
    std::map<std::pair<int, int>, std::wstring> m_structureKeys;
    std::map<std::pair<int, int>, std::wstring> m_itemKeys;
    // Reverse lookup tables for creature, object and item keys.
    std::map<std::wstring, int> m_creatureIds;
    std::map<std::wstring, int> m_staticIds;
    std::map<std::wstring, std::pair<int, int>> m_itemIds;
    // Building models loaded by the constructor, indexed zero to eleven.
    std::map<int, Sprite *> m_buildingSprites;
    // Time of day in milliseconds, initially noon (43200000), used by daily schedules.
    int m_time;
    enum {
        MillisecondsPerHour = 3600000
    };
    // The days passed: update counts one whenever m_time passes 24 hours.
    int m_day; // Initially zero.
    // World seed used with its name to reload after disconnection.
    int m_seed;
    // Seed-derived noise offsets from zero through 99999, grouped by use.
    int m_riverNoiseOffsets[4];
    int m_cliffNoiseOffsets[4];
    int m_generationSeed;
    int m_heightNoiseOffsets[20];
    int m_vegetationNoiseOffsets[2];
    // Unused seed-derived offsets set by World::setup.
    int unused8001e4[2];
    int unused8001ec[2];
    int m_grassNoiseOffsets[2];
    int m_coordinateWarpOffsets[8];
    int m_rockColorNoiseOffsets[6];
    int m_pavingColorNoiseOffsets[6];
    int m_groundColorNoiseOffsets[10];
    int m_sandColorNoiseOffsets[4];
    int unused800284[2];
    int m_coordinateNameSeeds[2];
    int m_treeKindNoiseOffsets[2];
};

}

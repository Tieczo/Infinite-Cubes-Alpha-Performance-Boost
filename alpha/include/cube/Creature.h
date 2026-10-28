// Creature represents a world creature, including the player. It stores network entity state,
// Appearance, inventory, pathfinding and animation state. Unidentified state remains opaque.
#pragma once
#include <winsock2.h>
#include "cube/SkillIds.h"
#include <array>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "plasma/Vector.h"
#include "plasma/Matrix.h"
#include "cube/Fixed.h"
#include "cube/Equipment.h"
#include "cube/Inventory.h"
#include "cube/Interaction.h"
#include "cube/Zone.h"

namespace cube {

enum {
    DefaultHealthMultiplier = 100,
    StunRecoveryDelayMs = 3000
};
// A roll (EntityData::rollTime, counted down by the world update) lasts 600 ms: the player's and
// the AI's dodges, the player's shuriken retreat and a ninja's attack rolls.
enum {
    RollDurationMs = 600
};

class CubeShader;
class Sprite;
class SpriteManager;
class Behavior;
class World;
struct ServerUpdates;

class Creature {
public:
    struct Appearance {
        // Default humanoid rest pose: part positions are model-space x/y/z offsets; scales preserve the
        // authored proportions.
        Appearance()
            : unsetPair(0, 0),
              hairColor(255, 255, 255),
              appearanceFlags(0),
              size(1.0f, 1.0f, 1.0f),
              headModel(-1),
              hairModel(-1),
              handsModel(-1),
              feetModel(-1),
              chestModel(-1),
              tailModel(-1),
              shoulderModel(-1),
              wingsModel(-1),
              headScale(1.01f),
              chestScale(1.0f),
              handsScale(1.0f),
              feetScale(0.98f),
              shoulderModelScale(1.0f),
              weaponScale(0.95f),
              tailScale(0.8f),
              shoulderArmorScale(1.0f),
              wingScale(1.0f),
              chestRotation(0),
              handsRotation(0, 0, 0),
              feetRotation(0),
              wingsRotation(0),
              tailRotation(0),
              chestPosition(0, 0, -5),
              headPosition(0, 0.5f, 5),
              handsPosition(6, 0, 0),
              feetPosition(3, 1, -10.5f),
              tailPosition(0, -8, 2),
              wingsPosition(0, 0, 0)
        {}

        // Member by member; the entity-data writer's changed test.
        bool operator!=(const Appearance &other) const
        {
            return unsetPair != other.unsetPair || hairColor != other.hairColor ||
                   appearanceFlags != other.appearanceFlags || size != other.size || headModel != other.headModel ||
                   hairModel != other.hairModel || handsModel != other.handsModel || feetModel != other.feetModel ||
                   chestModel != other.chestModel || tailModel != other.tailModel ||
                   shoulderModel != other.shoulderModel || wingsModel != other.wingsModel ||
                   headScale != other.headScale || chestScale != other.chestScale || handsScale != other.handsScale ||
                   feetScale != other.feetScale || shoulderModelScale != other.shoulderModelScale ||
                   weaponScale != other.weaponScale || tailScale != other.tailScale ||
                   shoulderArmorScale != other.shoulderArmorScale || wingScale != other.wingScale ||
                   chestRotation != other.chestRotation || handsRotation != other.handsRotation ||
                   feetRotation != other.feetRotation || wingsRotation != other.wingsRotation ||
                   tailRotation != other.tailRotation || chestPosition != other.chestPosition ||
                   headPosition != other.headPosition || handsPosition != other.handsPosition ||
                   feetPosition != other.feetPosition || wingsPosition != other.wingsPosition ||
                   tailPosition != other.tailPosition;
        }

        plasma::Vector<2, unsigned char> unsetPair;
        plasma::Vector<3, unsigned char> hairColor;
        unsigned short appearanceFlags;
        plasma::Vector<3, float> size;
        short headModel;
        short hairModel;
        short handsModel;
        short feetModel;
        short chestModel;
        short tailModel;
        short shoulderModel;
        short wingsModel;
        float headScale;
        float chestScale;
        float handsScale;
        float feetScale;
        float shoulderModelScale;
        float weaponScale;
        float tailScale;
        float shoulderArmorScale;
        float wingScale;
        float chestRotation;
        plasma::Vector<3, float> handsRotation;
        float feetRotation;
        float wingsRotation;
        float tailRotation;
        plasma::Vector<3, float> chestPosition;
        plasma::Vector<3, float> headPosition;
        plasma::Vector<3, float> handsPosition;
        plasma::Vector<3, float> feetPosition;
        plasma::Vector<3, float> tailPosition;
        plasma::Vector<3, float> wingsPosition;
    };

    // Sixteen one-bit toggles (2 bytes),. The behaviours clear CreatureToggleFastMovement.
    struct BinaryToggles {
        // Bit 0: climbing a wall.
        unsigned short climbing : 1;
        unsigned short active : 1;
        unsigned short faceAim : 1;
        unsigned short toggle3 : 1;
        // Bit 4: hang gliding.
        unsigned short gliding : 1;
        unsigned short combatSideOverride : 1;
        unsigned short fastMovement : 1;

        unsigned short dodge : 1;
        unsigned short missionComplete : 1;
        // Bit 9: the lamp; key F flips it and a lit lamp within ten blocks reveals a stealthy creature.
        unsigned short lampOn : 1;
        // Bit 10: sneaking: the stealth factor grows with skill 99.
        unsigned short sneaking : 1;
        unsigned short unknownToggles : 5; // bits 11..15, not yet examined
    };

    // These scalars travel as a single entity field; each getter multiplies by the corresponding
    // scalar.
    struct StatMultipliers {
        StatMultipliers()
            : hp(DefaultHealthMultiplier),
              attackSpeed(1),
              damage(1),
              armor(1),
              resistance(1)
        {}
        bool operator!=(const StatMultipliers &other) const
        {
            return hp != other.hp || attackSpeed != other.attackSpeed || damage != other.damage ||
                   armor != other.armor || resistance != other.resistance;
        }
        float hp;
        float attackSpeed; // scales tempo in attack timing
        float damage;      // scales base damage and block power
        float armor;
        float resistance;
    };

    // Network entity state, including position, appearance, equipment and combat state. Unidentified
    // state remains opaque.
    struct EntityData {
        EntityData();
        void resetMotion();
        void reset();
        // Sets or clears the supplied mask in binaryToggles.
        void setToggles(unsigned short mask, bool value)
        {
            unsigned short &toggles = *reinterpret_cast<unsigned short *>(&binaryToggles);
            if (value)
                toggles |= mask;
            else
                toggles &= ~mask;
        }

        plasma::Vector<3, Fixed> position;
        plasma::Vector<3, float> rotation;
        plasma::Vector<3, float> velocity;
        plasma::Vector<3, float> acceleration;
        plasma::Vector<3, float> retreat;
        float headRotation;
        unsigned int flags;
        unsigned char hostilityType;
        int race;
        unsigned char currentAbility;
        int timeSinceAbility; // integer milliseconds
        int hitCombo;
        int timeSinceHit; // integer milliseconds
        Appearance appearance;
        BinaryToggles binaryToggles;
        int rollTime;
        int stunTime; // Initially negative 3000.
        // Four synchronized integer values, copied separately.
        int actionLockTime; // milliseconds the creature cannot act (a hit's actionLockTime; animation 56)
        int slowedTime;     // remaining slow effect, milliseconds
        int sprintTime;     // Remaining speed boost in milliseconds.
        // Negative while recovering, rising towards zero; drinking adds to it. The model wears sign 2550
        // while it is negative.
        float revivalPenalty;
        unsigned char classType;
        unsigned char specialization;
        float charge;
        plasma::Vector<3, float> finalHandsRotation;
        plasma::Vector<3, float> initialHandsRotation;
        plasma::Vector<3, float> attackRotation;
        float hp;
        float mp;
        // Blocking costs damage divided by blockPower and adds the result to mana and charge, with a
        // minimum of negative one.
        float blockPower;
        StatMultipliers multipliers;
        unsigned char interactionState;
        unsigned char
            buildingModel; // synchronized byte: the building mode's model (World::update's InteractionBuild places it; render
                           // previews it)
        int level;         // 1 initially
        int xp;
        long long ownerId;
        long long focusId;
        unsigned char rarity; // Rarity zero to four scales health, loot quality and experience.
        // Initially negative one. Copied from the spawn and serialized with entity state; otherwise
        // unused.
        int unused19c;
        plasma::Vector<3, int>
            spawnSlot; // synchronized; the zone (x, y) and index of the spawn the creature came from, (-1, -1, 0) for
                       // none
        plasma::Vector<3, Fixed> origin; // left unset
        unsigned char
            speechExpression; // synchronized byte: the Speech::Expression of the creature's quest talk (GameController::talkTo)
        plasma::Vector<3, int> questZone; // synchronized; the zone its quest talk is about (x, y), (-1, -1, 0) for none
        Item actionItem;
        Equipment equipment;
        int skillLevels[SkillCount];
        int manaCubeCount; // pickups increment it; each four grant a skill point
        char name[16];
    };

    // List node size 0x20 : two links and 0x18 data. World decrements the integer lifetime and uses
    // strength as a float.
    struct Buff {
        Buff()
            : type(0),
              strength(0),
              timeRemaining(0),
              sourceId(0)
        {}
        unsigned char type;
        float
            strength; // a multiplier, a shield's remaining absorption (type 6), damage per tick or tameness; member too
        int timeRemaining; // milliseconds
        long long sourceId;
    };

    // Extent from Creature's next member, -. The pose Constructor is in another file.
    class AnimationState {
    public:
        AnimationState(Creature *creature);
        // Poses and draws the model using the view, projection, sprites, color, origin, hidden-part mask
        // and mount. A zero time step draws an after-image.
        void addSprites(CubeShader *shader, const plasma::Matrix<float> &view, const plasma::Matrix<float> &projection,
                        SpriteManager *spriteManager, int milliseconds, const plasma::Vector<4, float> &color,
                        const Fixed &originX, const Fixed &originY, int hiddenParts, Creature *mount);
        Creature *m_creature;
        int m_animationTime; // integer milliseconds
        int m_animation;
        plasma::Vector<3, float> m_leftHandOffset;
        plasma::Vector<3, float> m_rightHandOffset;
        plasma::Vector<3, float> m_leftHandRotation;
        plasma::Vector<3, float> m_rightHandRotation;
        // Six unused words initialized to zero.
        int unused03c;
        int unused040;
        int unused044;
        int unused048;
        int unused04c;
        int unused050;
        plasma::Vector<3, float> m_bodyRotation;
        plasma::Vector<3, float> m_creatureRotation;
        plasma::Vector<3, float> m_bodyOffset;
        plasma::Vector<3, float> m_headOffset;
        plasma::Vector<3, float> m_headRotation;
        plasma::Vector<3, float> m_tailRotation;
        plasma::Vector<3, float> m_handsTurn;
        float m_bodyTurn;
        plasma::Vector<3, float> m_creatureOffset;
        float
            m_glideLift; // the lift the creature's turn pivots about (half its height while gliding), blended by addSprites
        Sprite *m_headSprite;
        Sprite *m_hairSprite; // left unset by constructor
        Sprite *m_chestSprite;
        Sprite *m_shouldersSprite;
        Sprite *m_handsSprite;
        Sprite *m_wingsSprite;
        Sprite *m_tailSprite;
        Sprite *m_feetSprite;
        Sprite *m_rightWeaponSprite;
        Sprite *m_leftWeaponSprite; // left unset by constructor
        Sprite *m_shoulderArmorSprite;
        Sprite *m_neckSprite;
        // Part matrices are applied immediately before their corresponding sprites are drawn.
        plasma::Matrix<float> m_bodyMatrix;
        plasma::Matrix<float> m_handsMatrix;
        plasma::Matrix<float> m_chestMatrix;
        plasma::Matrix<float> m_headMatrix;
        plasma::Matrix<float> m_hairMatrix;
        plasma::Matrix<float> m_leftShoulderMatrix;
        plasma::Matrix<float> m_rightShoulderMatrix;
        plasma::Matrix<float> m_rightWeaponMatrix;
        plasma::Matrix<float> m_leftWeaponMatrix;
        plasma::Matrix<float> m_leftHandMatrix;
        plasma::Matrix<float> m_rightHandMatrix;
        plasma::Matrix<float> m_shoulderArmorMatrix;
        plasma::Matrix<float> m_neckMatrix;
        plasma::Matrix<float> m_leftWingMatrix;
        plasma::Matrix<float> m_rightWingMatrix;
        plasma::Matrix<float> m_leftFootMatrix;
        plasma::Matrix<float> m_rightFootMatrix;
        plasma::Matrix<float> m_tailMatrix;
        // Four strips of sixteen points forming weapon trails.
        plasma::Vector<3, float> m_trails[4][16];
        plasma::Vector<3, float> m_blendedFacing;
        bool m_hasSpriteUpdate;
        Fixed m_originX;
        Fixed m_originY;
    };

    // A discovered path-search block (0x18 bytes, the map's value). The start is exceptional: {1,
    // heuristic, heuristic, start}.
    struct PathNode {
        PathNode() {}

        PathNode(int movementCost, int heuristic, int totalCost, const plasma::Vector<3, int> &parent)
            : movementCost(movementCost),
              heuristic(heuristic),
              totalCost(totalCost),
              parent(parent)
        {}

        int movementCost;              // accumulated movement cost
        int heuristic;                 // estimated remaining distance
        int totalCost;                 // movementCost + heuristic, except at the start
        plasma::Vector<3, int> parent; // the block it was reached from
    };

    // One entry of a creature's daily schedule : from its time of day on, the creature keeps to its
    // place. SpawnLocationBehavior::Update selects by World::m_time and sends the creature along a path
    // to the selected position.
    struct ScheduleEntry {
        plasma::Vector<3, Fixed> position; // the place, a world position
        int time;                          // compared with World::m_time
    };

    // The first 0x14 saved bytes of the player's character style.
    struct CharacterStyle {
        CharacterStyle()
            : race(0),
              gender(0),
              face(0),
              haircut(0),
              hairColor(255, 255, 255)
        {}

        int race;
        unsigned char gender;
        int face;
        int haircut;
        plasma::Vector<3, unsigned char> hairColor;
    };

    // Player state, including saved world positions keyed by seed and name.
    struct PlayerData : CharacterStyle {
        typedef std::map<std::pair<int, std::string>, plasma::Vector<3, Fixed>> WorldPositions;
        PlayerData() {}

        std::list<Item> knownFormulas; // known formulas
        WorldPositions worldPositions; // Saved world positions.
        // The last world seed and name, stored as a pair with an initially zero seed.
        std::pair<int, std::string> lastWorld;
    };

    Creature(const long long &id);
    virtual ~Creature();

    // The most hit points the creature can have. maxHP, which sorts between maxCombo and maxStunTime
    // here too.
    float maxHP();
    // Clears the creature's state for a new player : entity data, interactions, inventory, path,
    // timers. Like Creature::reset, this clears entity state and owned containers; onSelectCharacter
    // uses it for a new character.
    void reset();

    // Stat helpers in CreatureStats.cpp
    bool canBeDamaged();
    bool canEquipItem(const Item &item);
    bool canTakeItem(const Item &item);
    bool canUseFormula(const Item &item);
    // Whether the player data's known formulas hold an equal item (false without player data).
    // KnowsFormula
    bool knowsFormula(const Item &item);
    // The id of the creature with the most aggro (0 if none). The release's maxAggroTarget
    long long maxAggroTarget();
    // Raises the level while the xp reach the level's maximum, taking it off and restoring full hp.
    void applyLevelUps(World *world);
    float armor();
    float resistance();
    // Ends what the creature is doing: motion, action, combat counters, aggro and cooldowns (hp is
    // kept). stopActions; the name sorts between spellPower and strikeTime.
    void stopActions();
    float damageScale(int ability);
    float abilitySkillBonus(int ability, int points);
    float manaCost(int ability, int points);
    int maxCombo();
    int attackDuration();
    bool isBlocking();
    // Attack starters and boss predicate, observed by CombatBehavior::Update. performAttack and
    // performSpecialAttack are
    bool performAttack();
    // Whether the attack in progress allows the right-button skill.
    bool canCancelAbility();
    bool isBoss();
    // A dodge's reward (applyDodgeBonus): for a rogue of specialization 1, mana and a buff, queued for
    // the clients.
    void applyDodgeBonus(ServerUpdates *updates);
    // The weapons the current attack uses (attackWeapons).
    std::list<Item *> attackWeapons();
    // How much damage a block absorbs per unit of blockPower (from the level and the weapons' damage).
    // BlockPower.
    float blockPower();
    // The damage of the creature's weapons and level. attackPower (the same sum over the weapons,
    // scaled by the current ability)
    float attackPower();
    // Walks skill cooldowns without changing them.
    void onHit();
    // Returns false, so cooldowns are never held.
    bool holdsCooldown(int ability);
    // The damage of the creature's spells. spellPower (the weapons' damage summed the same way).
    float spellPower();
    // Position plus an appearance-dependent rotated offset. sightPosition (forward by half the body
    // and up by 0.35 of it for AppearanceFlagHeadAimOrigin); the name sorts between setBuff and
    // SpellPower.
    plasma::Vector<3, Fixed> sightPosition();
    int nextSpecialAttackSkill();
    // Starts the next special attack skill when the current one is over (its hold time left out after
    // a hit), the creature is not rolling and has the mana.
    bool performSpecialAttack();
    int nextAttackSkill();
    int cooldown(int ability, int points = -1);
    float knockback(int ability);
    int anticipationTime(int ability = -1);
    int strikeTime(int ability = -1);
    int holdTime(int ability = -1);
    float tempo();
    bool canUseSkill(int ability);
    Buff *findBuff(int type);
    bool hasRandomChance(float chance);
    bool hasTwoHandedWeapon();
    bool isAttackingFromRange();
    bool isCaster();
    // Gliding: binary toggle 4 with a falling velocity, off the ground and neither stunned nor
    // rolling.
    bool isGliding();
    int maxStunTime();
    // The experience the next level needs. maxExperience
    int maxExperience();
    // Peaceful animal races, unless AppearanceFlagTinted, AppearanceFlagSummoned or
    // AppearanceFlagMissionLevel are set; World::areEnemies uses this to exempt peaceful creatures from
    // ordinary hostility.
    bool isPeaceful();
    int power();
    plasma::Vector<4, float> color();
    float critical();
    float criticalChance();
    bool rollCritical();
    float regeneration();
    float tempoBonus();
    void setBuff(const Buff &buff);

    // Clears both path-search block sets, the found path and overlapping objects before a new search.
    void clearPath()
    {
        m_pathNodes.clear();
        m_pendingPathNodes.clear();
        m_path.clear();
        m_nearestPathNode = m_pathNodes.end();
        m_overlappedObjects.clear();
    }

    // Returns the discovered path node at block coordinates (x, y, z), or null.
    PathNode *findPathNode(int x, int y, int z);
    // Whether the block is still pending expansion.
    bool isPathNodePending(int x, int y, int z);
    void setPathNode(int x, int y, int z, const PathNode &node);

    long long m_id;
    EntityData m_entityData;
    std::list<Buff> m_buffs;
    // Initially zero; height remaining to blend when drawing a step up.
    float m_stepOffset;
    float m_hitFlash; // Initially zero; hits set one, support rays 0.3, and hits without an attacker 0.5.
    // Initially zero; the walk-cycle weight grows during movement and fades afterwards.
    float m_walkAmplitude;
    // Zero in constructor; the walk cycle's phase.
    float m_walkPhase;
    float m_stealth; // sneaking builds it, light/movement remove it; detection and criticalChance consume it
    float m_stamina; // Initially one; dodging costs 0.25.
    // riding stamina is saved with the player, drains during riding movement and ends riding at zero.
    // A pet in water refills it.
    float m_ridingStamina; // Initially one.
    // Initially zero; saved and loaded with the player but otherwise unused.
    int unused119c;
    plasma::Vector<3, float> m_wallNormal;  // unset; the wall a climbing creature moves along
    std::set<long long> m_struckCreatures;  // signed creature ids
    std::set<long long> m_dodgingCreatures; // same node type
    unsigned int unused11bc;                // never written or read
    long long m_petId;                      // Initially zero; identifies the pet controlled while riding.
    // The summoned companion identifier. Kills grant its experience and update the pet item with its
    // level and experience.
    long long m_companionId; // zero in constructor
    long long
        m_opponentId; // zero in constructor; the creature it fights: set by both sides of a hit and by CombatBehavior;
                      // World::onKill clears it
    unsigned int m_leapDuration; // zero in constructor; a jump's flight time in milliseconds (speed / 50 * 1000,
                                 // GameController::update)
    Inventory m_inventory;
    std::list<Interaction> m_interactions;
    unsigned int
        m_swingCount; // zero in constructor; AnimationState counts the swings by it (every third double swing turns the
                      // Creature)
    unsigned int m_rollTime;                  // zero in constructor; a copy of EntityData::rollTime (World::update)
    unsigned char unused131c[4];              // never written or read
    plasma::Vector<3, Fixed> m_abilityTarget; // Aim position captured before releasing a spell.
    // When a server update preserves the old origin, the client draws from the previous pose and
    // counts smoothing time down from sixty.
    plasma::Vector<3, Fixed> m_previousDrawnPosition; // unset; m_drawnPosition when a server update arrived
    plasma::Vector<3, Fixed> m_drawnPosition;         // initially zero
    plasma::Vector<3, float> m_previousDrawnRotation; // unset; m_drawnRotation then
    plasma::Vector<3, float> m_drawnRotation;         // initially zero
    plasma::Vector<3, float> m_previousDrawnAim;      // Unset until the drawn aim is captured.
    // The drawn aim follows attackRotation smoothly, taking it directly between attacks; rendering
    // uses it for beam effects.
    plasma::Vector<3, float> m_drawnAim; // Initially unset.
    // Initially zero; positive values blend the drawn position and rotation towards the real pose.
    int m_smoothingTime;
    std::map<int, int> m_skillCooldowns;
    std::map<long long, float> m_aggro; // target creature id -> weight
    std::map<long long, float> m_hitCounts;
    // Initially zero; attack power reflects its mana cost, mana or charge spent, and affects stun
    // chance.
    float m_attackPower;
    unsigned char m_strikeLanded;   // zero in constructor; set once the attack under way has struck
    float m_fallStartHeight;        // zero in constructor; the z (blocks) where the current fall began
    unsigned char m_criticalStrike; // zero in constructor; the attack under way is a critical one
    unsigned int m_behaviorTime;    // zero in constructor
    float m_damageDealt; // zero in constructor; the damage of the hits it dealt since its last ten quiet seconds
    unsigned int m_damageDealtTime; // zero in constructor
    int m_timeSinceDamageDealt;     // zero in constructor; milliseconds since it last dealt damage
    unsigned int unused13d4;        // zero in constructor; nothing reads it
    float unused13d8;               // 1.0 in constructor; nothing reads it
    unsigned int unused13dc;        // Initially zero and unread.
    // Zeroed by construction, reset, stopActions and combat updates; unread.
    unsigned int unused13e0;
    Behavior *m_behavior;             // owned, nonvirtual deletion
    std::vector<int> m_specialSkills; // ability ids
    int m_summonAppearanceMode;       // 1 in constructor; the spawn's summonAppearanceMode
    std::vector<int> m_summonRaces;   // race ids
    unsigned int unused1404;          // Zeroed by construction, reset and stopActions; unread.
    // Path search traverses blocks between m_pathStart and m_pathGoal, keyed by three integer block
    // coordinates. The key is a fixed array (a vector of three in the original): the same order (both compare
    // lexicographically), without a heap allocation for every lookup.
    typedef std::array<int, 3> PathKey;
    std::map<PathKey, PathNode>::iterator m_nearestPathNode; // the discovered block with the lowest heuristic
    std::map<PathKey, PathNode> m_pathNodes;                 // all discovered blocks
    // Pending blocks, expanded by lowest totalCost, in key order (a set of keys in the original), each with
    // its node in m_pathNodes, or null when the block has none: the search scans them without a lookup.
    // Kept equal to findPathNode by setPathNode and by refreshPendingPathNodes after m_pathNodes is replaced.
    std::map<PathKey, PathNode *> m_pendingPathNodes;
    void refreshPendingPathNodes();
    plasma::Vector<3, int> m_pathStartBlock;
    plasma::Vector<3, Fixed> m_pathStart;
    plasma::Vector<3, Fixed> m_pathGoal;
    float m_pathRadius;   // how near the goal the path may end, in blocks
    int m_pathSearchTime; // zeroed by beginPathSearch; milliseconds of the search, which restarts after 3000
    std::list<plasma::Vector<3, int>> m_path; // Found path blocks, with the goal last.
    // Objects overlapping the start or goal are excluded from path obstacles. The set is cleared with
    // the path.
    std::set<Zone::Object *> m_overlappedObjects;
    // Cleared by reset and stopActions; nothing fills or reads this list.
    std::list<plasma::Vector<3, int>> unused1470;
    plasma::Vector<3, int> m_interactionTarget; // Zone coordinates and object index.
    // Daily schedule state tracks the last place reached, the next destination and entries ordered by
    // time.
    int m_scheduleReached;
    int m_scheduleEntry;
    std::vector<ScheduleEntry> m_schedule;
    AnimationState m_animationState;
    // Initially null; copied when saving the player character.
    PlayerData *m_playerData;
    float
        m_despawnDistance; // initially 200.0; a spawn's: World::update removes it beyond this plus 20 blocks from every player
    long long generationKey; // unset; a spawned creature's Spawn::generationKey (World::update)
    unsigned char
        m_dungeonMissionTarget; // initially zero; from its spawn: its death counts for a dungeon mission (World::applyHit)
    int m_shownLevel;           // initially 1; the level the HUD last announced (GameController::update)
    unsigned int m_shownXp;     // initially zero; the xp the HUD last announced
    unsigned int
        m_lastPetSkillLevel; // initially zero; the pet skill level the companion was last built with (World::update)
    Item m_lastPetItem;      // the pet item the companion was last built with
};

// Builds a creature appearance from its race and optional player style.
void fillAppearance(int &race, Creature::Appearance &appearance, const Creature::PlayerData *style);

class BytesIO;

// Fills sprite IDs, scales and model offsets for the selected race and optional player style.
void fillAppearance(int &race, Creature::Appearance &appearance, const Creature::PlayerData *style);

// Receives an entity-data update straight from the socket into the entity data (Network.cpp; the
// full entity list of GameController::connect's receiving thread); returns its size, 0 when the
// connection fails.
int receiveEntityData(SOCKET socket, Creature::EntityData &entityData);

}

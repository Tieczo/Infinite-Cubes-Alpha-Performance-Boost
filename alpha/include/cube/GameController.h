// GameController handles window input and owns the client world, rendering chunks, connection, workers and interface. Destruction stops workers, closes the socket and releases locks and owned members. Unidentified state remains opaque.
#pragma once
#include "cube/ParticleIds.h"
#include <windows.h>
#include <list>
#include <map>
#include <string>
#include <vector>
#include "plasma/Matrix.h"
#include "plasma/Vector.h"
#include "cube/BuffPacket.h"
#include "cube/BulletPacket.h"
#include "cube/Controller.h"
#include "cube/Database.h"
#include "cube/Fixed.h"
#include "cube/Formula.h"
#include "cube/HitPacket.h"
#include "cube/Inventory.h"
#include "cube/Item.h"
#include "cube/Options.h"
#include "cube/ServerUpdates.h"
#include "cube/World.h"
#include "cube/WorldMap.h"

struct IDirect3DDevice9;
struct IDirect3DTexture9;

namespace plasma {
class Engine;
class Node;
class Texture;
class TextShape;
class Widget;
}

namespace cube {

// Shared by GameController's files. The loaded mesh radius (update) is the distance to the nearest
// unmeshed chunk's centre less 22.4 blocks, which render's range adds back; the quests and
// missions a player is shown are those within QuestLevelRange levels of the player's.
enum {
    QuestLevelRange = 2
};

// UI positions, established by getHoveredEquipment and the input/frame updates.
enum EquipmentButton {
    EquipmentButtonWeaponLeft = 0,
    EquipmentButtonWeaponRight = 1,
    EquipmentButtonRingLeft = 2,
    EquipmentButtonRingRight = 3,
    EquipmentButtonNeck = 4,
    EquipmentButtonShoulder = 5,
    EquipmentButtonChest = 6,
    EquipmentButtonHands = 7,
    EquipmentButtonFeet = 8,
    EquipmentButtonLamp = 9,
    EquipmentButtonSpecial = 10,
    EquipmentButtonPet = 11
};
// Number of entries, not an ID.
enum {
    EquipmentButtonCount = 12
};
// Ability-bar positions, named by the input each button highlights.
enum AbilityBarSlot {
    AbilityBarMouseLeft = 0,
    AbilityBarMouseRight = 1,
    AbilityBarKey1 = 2,
    AbilityBarKey2 = 3,
    AbilityBarKey3 = 4,
    AbilityBarKey4 = 5,
    AbilityBarQuickItem = 6
};
// The constructor creates help first, then skills through system.
enum GameMenuButton {
    GameMenuHelp = 0,
    GameMenuSkills = 1,
    GameMenuCrafting = 2,
    GameMenuInventory = 3,
    GameMenuWorldMap = 4,
    GameMenuSystem = 5
};
// Number of entries, not an ID.
enum {
    GameMenuButtonCount = 6
};

class Audio;
class Server;
class BlueprintPreviewWidget;
class VoxelWidget;
class EnchantWidget;
class AdaptionWidget;
class CharacterStyleWidget;
class OptionsWidget;
class SystemWidget;
class ChatWidget;
class CubeShader;
class InventoryWidget;
class SkillWidget;
class XAudio2Engine;
class Creature;
class Sprite;
class SpriteWidget;
class SpeechWidget;
class StartMenuWidget;
class PreviewWidget;
class ObjectiveWidget;
class StatisticsWidget;
struct Chunk;
struct WorldInfo;

// Floating text drawn by update using its color and rise.
struct TextFX {
    // Rising at 100 pixels per second for a second.
    TextFX()
        : rise(-100.0f),
          duration(1000)
    {}

    int age;
    float rise;
    plasma::Vector<3, Fixed> position;
    float offsetX;
    float offsetY;
    plasma::Vector<4, float> color;
    std::wstring text;
    int duration;
    float pixelSize;
    int textFlags;
};

// A particle's lifetime unless its maker sets one (milliseconds).
enum {
    DefaultParticleLifetime = 4000
};

// A particle (0xb8 bytes: list nodes of 0xc0). createParticles makes particles from surface
// voxels; update moves them and render draws them.
struct Particle {
    // Initializes a particle.
    Particle()
        : type(ParticleLitCube),
          age(0),
          rotation(0.0f, 0.0f, 0.0f),
          velocity(0.0f, 0.0f, 0.0f),
          spin(0.0f, 0.0f, 0.0f),
          size(1.0f),
          collidesWithBlocks(false),
          motionDisabled(0),
          lifetime(DefaultParticleLifetime)
    {
        matrix.identity();
    }

    int type;
    int age;
    plasma::Vector<3, Fixed> position;
    plasma::Vector<3, Fixed> startPosition;
    plasma::Vector<3, float> rotation;
    plasma::Vector<4, float> color;
    plasma::Vector<3, float> velocity;
    plasma::Vector<3, float> spin;
    plasma::Matrix<float> matrix;
    float size;
    bool collidesWithBlocks;
    unsigned char motionDisabled;
    int lifetime;
};

// A temporary point light whose color fades over its duration.
struct Light {
    plasma::Vector<3, Fixed> position;
    plasma::Vector<3, float> color;
    float radius;
    int age;
    int duration;
};

// A received chat line with a creature identifier and text.
struct ChatMessage {
    long long creatureId;
    std::wstring text;
};

// A nearby creature used for life bars and picking, sorted nearest first.
struct NearbyCreature {
    struct Nearer {
        bool operator()(const NearbyCreature &a, const NearbyCreature &b) const
        {
            return a.distanceSquared < b.distanceSquared;
        }
    };
    Creature *creature;
    float distanceSquared; // from the player
    bool labelShown;       // false when added
};

// Displayed values round to two decimal places, with half values away from zero.
float roundHundredth(float value);

class GameController : public Controller {
public:
    // WinMain supplies the device, shader, engine, audio engine, options, display resolutions, server-mode flag and server address.
    GameController(IDirect3DDevice9 *device, CubeShader *shader, plasma::Engine *engine, Audio *audio, Options *options,
                   std::vector<plasma::Vector<2, int>> *resolutions, bool server, const std::string &serverAddress);
    virtual ~GameController();
    virtual bool isCursorVisible();
    virtual void handleKeyDown(unsigned char key);
    virtual void handleKeyUp(unsigned char key);
    virtual void resize(int width, int height);
    virtual void onKeyTyped(char character);
    virtual void handleMouseMove(float x, float y);
    virtual void handleButtonDown(int button);
    virtual void handleButtonUp(int button);
    virtual void handleMouseWheel(int delta);
    virtual void update(int milliseconds);
    virtual void render();

    // Enter and leave the three critical sections that guard the game state shared with the worker
    // threads (in this order, and the reverse).
    void beginUpdate();
    void endUpdate();

    // Plays a sound at a position: its volume falls with the distance from m_cameraPosition.
    void playSound(int sound, const plasma::Vector<3, Fixed> &position, float volume, float speed);
    // Plays a sound at the listener's position.
    void playSound(int sound);

    // The world list's Select button: starts the selected world, or shows the world creation page for
    // an empty slot (onSelectWorld).
    void onSelectWorld();
    // Switches the world list between the singleplayer and the multiplayer worlds (the button's text
    // names the other list).
    void onToggleWorlds();
    // The Done buttons of the character and world creation pages (the release's onDoneCharacter and
    // onDoneWorld): save the new slot.
    void onDoneCharacter();
    // Gives a new player character equipment and inventory for its class.
    void equipNewCharacter();
    void onDoneWorld();
    void onDeleteWorld();
    // Select and Delete Character button handlers.
    void onSelectCharacter();
    void onDeleteCharacter();
    // The start menu's back navigation between its screens.
    void onBack();
    // The nearest pet (HostilityPet) within 5 blocks of the player, or null.
    Creature *closestPet();
    // Whether a class master (AppearanceFlagClassMaster) of the player's class is within 4 blocks of
    // the player: the skill screen sells skill points only then. flag name; this name sorts between
    // std::set::insert and isCraftButtonHovered, its neighbours.
    bool isClassMasterNearby();
    // The player's equipment item under the pointer while the equipment screen is open, or null.
    Item *getHoveredEquipment();
    // The selected saved character, or null.
    Creature *selectedCharacter();
    // Key C: shows or hides the crafting screen and refreshes it.
    void toggleCraftingWidget();
    bool canMesh(int x, int y);
    void openInteraction();
    void applyOptions(const Options &options);
    Item *getHoveredIngredient();
    void toggleInventoryWidget();
    void useItem(ItemStack *stack);
    // Usable ring-menu items as inventory-tab and slot pairs.
    std::vector<plasma::Vector<2, int>> collectUsableItems();
    // Wraps specialization:<class>:<name> dictionary text at (x, y), first paragraph at size 12, then
    // 10. PreviewWidget::draw passes 1 for scale, which is not read.
    void writeSpecializationInfo(int classType, int specialization, int x, int y, float scale, int width);
    // Writes an item description at (x, y), optionally showing its name and allowed classes. The final
    // flag is unused.
    void writeItemInfo(Item *item, int x, int y, float scale, int width, bool showName, bool showClasses,
                       bool unusedFlag);
    // Writes skill descriptions and effects at the current and next level.
    void writeSkillInfo(int skill, int level, int x, int y, float scale, int width);
    // Fills @level, @health and ability-specific text variables.
    void getSkillDetails(int skill, int level, Speech::Context *context);
    void toggleSkillWidget();
    // Returns the inventory item under the identifier panel’s item frame.
    Item *getHoveredEnchantItem();
    // Makes the formula's result, consumes ingredients and plays SoundCraft.
    void craft();
    // Called by update for a mission's creature (restart) and for the creature the player talks to :
    // Starts or advances the creature's conversation in a speech widget.
    void talkTo(Creature *creature, bool restart);
    // R on a hovered object : advances the player's speech.
    void useObject(Zone::Object *object, bool advance);
    // Meshes a 32-by-32-block chunk into solid and water faces and uploads the buffers. The chunk worker calls this helper.
    // Chunks per side of the square kept meshed around the camera: 40 up to a render distance of
    // 100%, then growing with it to 80 (m_viewDistance) at 200%.
    int activeViewSide();
    void updateChunk(int x, int y);
    // Shades a face corner using neighboring block light and occlusion, with light stored in w. Plain colors are returned unchanged.
    plasma::Vector<4, float> shade(int x, int y, int z, const plasma::Vector<4, float> &color,
                                   const plasma::Vector<3, int> &normal, bool plain);
    Item *getHoveredFormula();
    plasma::Vector<2, int> getHoveredSlot();
    Item *getHoveredVendorItem();
    bool hasHoveredTradeItem();
    // Adds formulas learned at a level to the known-formula list.
    void learnFormulas(int level);
    // Tells in the chat about an item a creature received.
    void notifyItem(const Item &item, Creature *creature);
    // Key O: shows or hides the system menu.
    void toggleSystemWidget();
    void changeWorld(int seed, const std::string &name);
    // Leaves the server: saves the character, closes the socket, waits for the network threads and
    // reloads the local world.
    void disconnect();
    // Draws spirit cubes at their item-local positions, as in Renderer::drawCubes; each uses its
    // spirit's material colour.
    void drawCubes(const Item *item, const plasma::Matrix<float> &model, const plasma::Matrix<float> &view,
                   const plasma::Matrix<float> &projection, const plasma::Vector<4, float> &color, float glow);
    // Draws an item model rotated in degrees at window position (x, y).
    void drawItem(float x, float y, const plasma::Vector<3, float> &rotation, float scale, const Item *item,
                  float depth);
    // Draws a sprite model with a screen-space projection and rotation in degrees at window position
    // (x, y).
    void drawSpriteModel(float x, float y, const plasma::Vector<3, float> &rotation, float scale, Sprite *sprite,
                         float depth);
    // Connects to the server whose address is in the connect screen's edit field (the Connect button).
    void connectToServer();
    void connect(const std::string &address);
    // Draws three colored cubes per step circling along a vector from position, advancing with time.
    void drawImpact(const plasma::Vector<3, Fixed> &position, const plasma::Vector<3, float> &vector,
                    const plasma::Vector<3, float> &rotation, const plasma::Vector<4, float> &innerColor,
                    const plasma::Vector<4, float> &middleColor, const plasma::Vector<4, float> &outerColor, int time,
                    const plasma::Matrix<float> &view, const plasma::Matrix<float> &projection, float width,
                    float scale, float frequency, int count, bool clipped);
    // Breaks a sprite into particles: one for every other surface voxel.
    void createParticles(Sprite *sprite, const plasma::Matrix<float> &matrix,
                         const plasma::Vector<3, float> &unusedVector, const plasma::Vector<3, float> &velocity,
                         float unusedFactor, float originX, float originY, const plasma::Vector<4, float> &color,
                         int particleType);
    // Shows the connect screen with empty edit fields.
    void showConnectScreen();
    void saveCharacter(int index, Creature *creature);
    // Reads and decompresses a character database slot, preserving the creature position.
    void loadCharacter(int index, Creature *creature);
    // Loads a world name, seed, explored area and compressed preview voxels from the database.
    void loadWorldInfo(int index, WorldInfo *world);
    // The objective text of a region's mission.
    std::wstring missionText(Region::Record *record);
    void saveWorldInfo(int index, WorldInfo *world, bool lockWorld);
    // Sorts known crafting formulas into six tabs.
    void updateFormulas();
    // Fills crafting tab `tab` with a stack of each formula's result, as many as the player's
    // inventory can make. updateFormulaTab (same arguments; updateFormulas is its only caller).
    void updateFormulaTab(int tab, const std::vector<Formula> &formulas);
    // An image from the database, or from the file when the database does not have it.
    plasma::Texture *loadTexture(const std::string &path, Database *database);
    void updateWorldList();
    // Rebuilds the character list.
    void refreshCharacterList();
    // Gives the crafting window the tabs of the player's class : its weapons and armour, then amulets,
    // cooking, alchemy and ingredients. Interface::updateStyle, which sets the crafting pages for the
    // local player's class.
    void updateStyle();

    // Shows the Delete Character and Delete World buttons for selected list entries.
    void onEditCharacter();
    void onEditWorld();
    // Fills the trade window: the trading creature's stock, or on the second tab what the player sold,
    // newest first.
    void updateVendor();
    // Updates the trade window when it is the widget that changed (InventoryWidget calls it;
    // game::onChangeInventoryTab).
    void onChangeInventoryTab(void *widget);
    // The local player's movement from the keys for one update step: walking, swimming, climbing,
    // dodging and jumping, and the lock-on target.
    void moveCharacter(int milliseconds);
    // Rendering helpers draw boxes, cubes and timed spirals with fading colors. Visibility tests check block boxes and spheres against the frustum and camera range.
    bool isBoxVisible(const plasma::Vector<3, Fixed> &minimum, const plasma::Vector<3, Fixed> &maximum, float range);
    bool isSphereVisible(const plasma::Vector<3, Fixed> &position, float radius, float range);
    // Whether the camera rests at its focus after terrain collision; rendering then hides the player's head.
    bool isCameraAtFocus();
    void drawRisingCubeRing(const plasma::Vector<3, Fixed> &position, const plasma::Vector<4, float> &startColor,
                            const plasma::Vector<4, float> &endColor, int time, const plasma::Matrix<float> &view,
                            const plasma::Matrix<float> &projection, float radius, float scale, int count);
    void drawExpandingCubeSpiral(const plasma::Vector<3, Fixed> &position, const plasma::Vector<4, float> &startColor,
                                 const plasma::Vector<4, float> &endColor, int time, const plasma::Matrix<float> &view,
                                 const plasma::Matrix<float> &projection, float radius, float scale, float turns,
                                 int count);
    // Draws a chunk plant at its position, scale and rotation. Flag four enables clock-driven sway,
    // index varies its phase, and camera distance fades it between fadeStart and fadeEnd.
    void renderPlant(const Plant &plant, float distanceSquared, float fadeEnd, float fadeStart, int index,
                     const plasma::Matrix<float> &view, const plasma::Matrix<float> &projection,
                     const plasma::Vector<4, float> &tint);
    // One static entity of a chunk: sprite m_staticEntitySprites[entity.type] at the entity's
    // position, size and rotation; doors, gates, traps, chests and levers moved by how far they are
    // open; faded and tinted as renderPlant does (placeStaticEntity).
    void renderStaticEntity(const Zone::ObjectState &entity, float distanceSquared, float fadeEnd, float fadeStart,
                            const plasma::Matrix<float> &view, const plasma::Matrix<float> &projection,
                            const plasma::Vector<4, float> &tint);
    // The same cubes rising over an arc of the given radius around position (drawParticle).
    void drawArcingCubeShell(const plasma::Vector<3, Fixed> &position, const plasma::Vector<4, float> &startColor,
                             const plasma::Vector<4, float> &endColor, int time, const plasma::Matrix<float> &view,
                             const plasma::Matrix<float> &projection, float radius, float scale, int count);

    // Only the constructor and destructor touch it (the element type is not observed).
    std::list<int> unused138;
    plasma::Vector<3, Fixed> m_cameraPosition; // (10000, 10000, 60) blocks at first; the listener of playSound
    plasma::Vector<3, Fixed> m_cameraFocus;    // 0; the point the camera looks at: follows the player (update)
    Options m_options;                         // a copy of the options
    bool m_quit;                               // WinMain's loop posts WM_QUIT when set
    plasma::Vector<3, float> m_cameraRotation; // degrees; render/moveCharacter pass its components to Matrix::rotateX/Z
    plasma::Vector<3, float> m_targetCameraRotation;
    float m_targetCameraDistance; // 16 at first
    float m_cameraDistance;       // 16 at first; the wheel keeps it in 0..14
    float m_mapZoom;              // 1; blended towards m_targetMapZoom (update): the map's scale
    float m_targetMapZoom;        // 1 at first; the wheel keeps it in 0.01..10
    float m_actualCameraDistance; // camera distance after terrain collision (update); zero puts the camera at its focus
    float m_viewRadius;           // half the 100% view square in blocks (ChunkGridSide * 16)
    float m_loadedMeshRadius;     // Smoothed loaded-mesh radius minus 22.4 blocks; fog adds that distance back.
    // Draw positions relative to the negative camera focus to keep view calculations in small floats.
    Fixed m_drawOriginX;                  // minus m_cameraFocus's x (update): the origin things are drawn relative to
    Fixed m_drawOriginY;                  // minus m_cameraFocus's y
    float m_screenShake;                  // 0; camera shake amplitude, decays at 0.005 per millisecond (update)
    plasma::Matrix<float> m_cameraMatrix; // the camera's orientation (swimming moves along it)
    plasma::Matrix<float> m_worldViewMatrix; // world (absolute) to view space: playSound pans by it
    plasma::Matrix<float> m_viewMatrix;      // Transforms positions relative to the drawing origin into view space.
    // Corner zone of the square chunk grid maintained by the chunk worker.
    plasma::Vector<2, int>
        m_chunkOrigin; // the first chunk of the view square (render: chunks x, y in [it, it + m_viewDistance).
    plasma::Vector<2, int> m_cameraBlock; // The camera block, refreshed by update.
    // A zone coordinate; m_mapOffset is a block position relative to its corner. the map worker loads
    // around that position.
    plasma::Vector<2, int> m_cameraZone;
    // Updated under m_updateLock2; the send worker requests the pending zones and regions.
    std::list<plasma::Vector<2, int>> m_playerZones;    // the zones around the local players
    std::list<plasma::Vector<2, int>> m_zoneRequests;   // those zones still without data
    std::list<plasma::Vector<2, int>> m_regionRequests; // their regions still without data (may repeat)
    int m_viewDistance; // chunk-grid side (80); updateChunk indexes 32-column chunks modulo it
    Chunk *m_chunks;    // new[]; deleted by the destructor
    World m_world;
    CubeShader *m_shader;   // the constructor's second argument
    bool m_running;         // the worker threads run while it is set
    bool m_connected;       // to a server
    HANDLE m_chunkThread;   // started by the constructor
    HANDLE m_mapThread;     // started by the constructor
    HANDLE m_receiveThread; // Connection receiving and sending thread handles.
    HANDLE m_sendThread;
    std::string m_serverAddress;  // the constructor's last argument
    bool m_zoneGenerationRunning; // the third worker thread runs while it is set
    HANDLE m_zoneGenerationThread;
    CRITICAL_SECTION m_charactersLock;
    CRITICAL_SECTION m_streamingLock;
    CRITICAL_SECTION m_mapUpdateLock;
    CRITICAL_SECTION m_chunkUpdateLock;
    CRITICAL_SECTION m_updateLock2;
    ServerUpdates m_serverUpdates;
    // Crafting-tab icons loaded from data3.db and selected for the player class.
    plasma::Texture *m_craftHeavyArmorIcon;
    plasma::Texture *m_craftMediumArmorIcon;
    plasma::Texture *m_craftRogueArmorIcon;
    plasma::Texture *m_craftLightArmorIcon;
    plasma::Texture *m_craftMeleeWeaponsIcon;
    plasma::Texture *m_craftRangedWeaponsIcon;
    plasma::Texture *m_craftRogueWeaponsIcon;
    plasma::Texture *m_craftMagicWeaponsIcon;
    plasma::Texture *m_craftAmuletsIcon;
    plasma::Texture *m_craftCookingIcon;
    plasma::Texture *m_craftAlchemyIcon;
    plasma::Texture *m_craftIngredientsIcon; // inventory-ingredients.png
    Server *m_localServer;                   // owned; the local game's server, or 0 (deleted first by the destructor)
    SOCKET m_socket;                         // the server connection, or 0
    Creature *m_player;                      // the local player's creature
    int unused8006d4;                        // zeroed with m_target by update's picking; nothing reads it
    Creature *m_target;                      // the locked-on creature, or null
    IDirect3DTexture9 *m_shadowTexture;      // 256 x 256 A8R8G8B8, managed
    IDirect3DTexture9 *m_sunTexture;
    bool m_mapOpen; // key M; the wheel then changes m_targetMapZoom instead of m_cameraDistance
    bool m_slowMotion;
    bool m_drawPlayerOnly;
    int m_frameMilliseconds; // 0; the frame time in milliseconds (render's plant calls and timers)
    std::list<HitPacket> m_hits;
    std::list<BuffPacket> m_buffs;
    std::list<BulletPacket> m_projectiles;
    bool m_buildingMode;
    unsigned char unused800705[0x800710 - 0x800705]; // never written or read
    plasma::Engine *m_engine;
    Audio *m_audio;                              // the constructor's fourth argument
    std::vector<Sprite *> m_plantSprites;        // sprites of the world's sprite manager
    std::vector<Sprite *> m_staticEntitySprites; // sprites of the world's sprite manager
    Sprite *m_cubeSprite;                        // owned
    Sprite *m_buildCursorSprite;                 // owned
    std::list<TextFX> m_textFx;
    std::list<Particle> m_particles;
    std::list<Light> m_lights;
    plasma::Node *m_textFxNode;
    plasma::Node *m_minimapNode;
    plasma::Node *m_landNameNode; // The landname node in gui.plx.
    // Night-sky stars stored as direction and size.
    std::vector<plasma::Vector<4, float>> m_stars;
    plasma::Node *m_lifeBar;                         // "lifebar" of gui.plx
    plasma::Node *m_lifeBarFill;                     // its "bar"
    plasma::Node *m_companionLifeBar;                // a copy of the life bar
    plasma::Node *m_companionLifeBarFill;            // its "bar"
    std::vector<plasma::Node *> m_partyLifeBars;     // three copies of the life bar
    std::vector<plasma::Node *> m_partyLifeBarFills; // their "bar" children
    plasma::Node *m_enemyLifeBar;                    // life bar templates from gui.plx
    plasma::Node *m_friendLifeBar;
    plasma::Node *m_staticLifeBar;
    plasma::Node *m_neutralLifeBar;
    plasma::Node *m_enemyLifeBarSmall;
    plasma::Node *m_friendLifeBarSmall;
    plasma::Node *m_staticLifeBarSmall;
    plasma::Node *m_neutralLifeBarSmall;
    plasma::Node *m_lifeBars;                        // the creatures' life bars are drawn under it
    bool m_showAllLifeBars;                          // toggled by key V
    unsigned char unused8007b5[0x8007b8 - 0x8007b5]; // never written or read
    plasma::Node *m_manaBar;
    plasma::Node *m_manaBarFill;
    plasma::Node *m_chargeBar;
    plasma::Node *m_experienceBar;
    plasma::Node *m_experienceBarFill;
    plasma::Node *m_companionExperienceBar;     // a copy of the experience bar
    plasma::Node *m_companionExperienceBarFill; // its "bar"
    plasma::Node *m_ridingBar;
    plasma::Node *m_manaCubeBar;
    plasma::Node *m_questBar;
    plasma::Node *m_questBarFill;
    plasma::Node *m_hpBar;
    plasma::Node *m_hpBarFill;
    plasma::Node *m_castBar;
    plasma::Node *m_castBarFill;
    plasma::Node *m_staminaBar;
    plasma::Node *m_staminaBarFill;
    std::vector<plasma::Node *> m_abilityButtons; // six ability buttons and the quick item button
    std::vector<plasma::Node *> m_cooldowns;      // the ability buttons' "cooldown" children
    std::vector<int> m_barAbilities;
    std::map<int, plasma::Texture *> m_abilityIcons;                        // by ability id
    std::map<int, plasma::Texture *> m_skillIcons;                          // by skill
    std::map<std::pair<int, int>, plasma::Texture *> m_specializationIcons; // by class and specialization
    std::vector<plasma::Node *> m_menuButtons;  // skills, crafting, inventory, world map, system, help
    std::vector<plasma::Node *> m_skillButtons; // the eleven skills of the skill screen
    plasma::Node *m_specializationButtons[PlayerSpecializationCount]; // the skill screen's two specializations
    plasma::Node *m_landscapeNode;      // the name of the landscape the player is in (its shadow)
    plasma::TextShape *m_landscapeText; // the name over the shadow
    plasma::Node *m_infoNode;           // the information text of the game screen (its shadow)
    plasma::TextShape *m_infoText;      // the information text
    plasma::Node *m_questTag;           // from quest-tag.plx
    plasma::Node *m_smallQuestTag;      // from quest-tag.plx
    int unused800870;                   // Initially zero and unread.
    // Interface nodes instantiated from source nodes. Unidentified nodes retain opaque names.
    plasma::Node *m_startScreen;
    plasma::Node *m_cubeWorldLogo; // from start.plx
    plasma::Node *m_picromaLogo;   // from start.plx
    plasma::Node *m_characterCreationScreen;
    plasma::Node *m_gameScreen;
    plasma::Node *m_characterListScreen;
    plasma::Node *m_worldListScreen;
    plasma::Node *
        m_worldCreationScreen; // the world creation screen (seed and name); shown by onSelectWorld when no world is selected
    plasma::Node *m_connectScreen;
    plasma::Node *m_waitNode; // "Please wait..."
    plasma::Node *m_helpNode; // help.plx; hidden
    plasma::Node *m_mapScreen;
    plasma::Node *m_mapOverlayNode; // the map overlay widget's node
    plasma::Node *m_cursorNode;     // cursor.plx
    plasma::Node *m_crosshair;
    plasma::Node *m_zoomCrosshair;
    plasma::Node *m_lockedEnemy;
    plasma::Node *m_lockedFriend;
    plasma::Node *m_inventoryNode; // keys B and I
    plasma::Node *m_craftingNode;  // key C
    plasma::Node *m_tradePanel;
    plasma::Node *m_blackWidget;  // template from gui.plx
    plasma::Node *m_itemBox;      // template from gui.plx
    plasma::Node *m_wideItemBox;  // template from gui.plx
    plasma::Node *m_itemSelector; // template from gui.plx
    Creature *m_tradeCreature;    // 0
    plasma::Node *m_voxelPanel;
    plasma::Node *m_selector;
    plasma::Node *m_previewNode; // "preview" of gui.plx
    PreviewWidget *m_previewWidget;
    plasma::Node *m_equippedPreviewNode; // "equippedpreview" of gui.plx
    bool m_equipmentOpen;                // the equipment slots are shown
    bool m_skillsOpen;                   // toggled by key X
    bool m_gameMenuOpen;                 // toggled by Escape
    unsigned char unused8008f3;          // never written or read
    VoxelWidget *m_voxelWidget;          // hovered upgrade item
    plasma::Node *m_enchantPanel;
    EnchantWidget *m_enchantWidget; // the identifier panel
    plasma::Node *m_adaptionPanel;
    AdaptionWidget *m_adaptionWidget;            // handleButtonUp resets its item and indices
    plasma::Node *m_skillPanel;                  // placed left of the inventory by resize
    SkillWidget *m_skillWidget;                  // toggleSkillWidget copies the player's skill levels here
    plasma::Node *m_systemPanel;                 // the system menu's panel; key O; never shown with the inventory
    SystemWidget *m_systemWidget;                // The system-menu panel.
    plasma::Node *m_startMenuNode;               // the start menu widget's node; centred by resize
    StartMenuWidget *m_startMenuWidget;          // The start-menu panel.
    std::vector<plasma::Node *> m_speechBubbles; // four instances of "speech"
    std::vector<SpeechWidget *> m_speechWidgets; // their widgets
    SpeechWidget *m_dialogWidget;
    plasma::Node *m_dialogNode;
    SpeechWidget
        *m_conversationWidget; // 0; the bubble of the conversation the player started (update closes it when finished)
    plasma::Node *m_objectiveNode;
    ObjectiveWidget *m_objectiveWidget;
    plasma::Node *m_statisticsNode;
    StatisticsWidget *m_statisticsWidget;
    InventoryWidget *m_inventoryWidget;
    InventoryWidget *m_craftingWidget;
    InventoryWidget *m_tradeWidget;               // compared by onChangeInventoryTab
    plasma::Widget *m_characterWidget;            // resize moves its parent widget
    BlueprintPreviewWidget *m_blueprintPreview;   // The item currently hovered in the widget.
    CharacterStyleWidget *m_characterStyleWidget; // The containing panel.
    plasma::Node *m_characterStylePanel;          // the character style panel; placed at the bottom left by resize
    plasma::Node *m_createCharacterButton;
    plasma::Node *m_characterNameNode;           // the character creation's name label and edit
    std::vector<plasma::Node *> m_characterRows; // centred horizontally by resize
    std::vector<Creature *> m_characters;        // the saved characters; owned
    plasma::Node *m_selectCharacterButton;       // the selected character's Select/Create button (update)
    plasma::Node *m_characterMenuButton;         // a second button of the selected character (update)
    plasma::Node *m_deleteCharacterButton;       // "Delete Character"
    plasma::Node *m_backButton;
    plasma::Node *m_worldListButton; // its text names the other world list
    plasma::Node *m_connectScreenButton;
    plasma::Node *m_connectButton;
    plasma::Node *m_serverAddressNode; // its "edit" child holds the address
    bool m_showMultiplayerWorlds;
    unsigned char unused8009b1[0x8009b4 - 0x8009b1]; // never written or read
    plasma::Node *m_createWorldButton;
    plasma::Node *m_worldSeedNode;
    plasma::Node *m_worldNameNode;
    std::vector<plasma::Node *> m_worldRows;
    std::map<plasma::Node *, int> m_worldRowIndices;
    std::map<int, plasma::Node *> m_worldRowsByIndex;
    std::vector<WorldInfo *> m_worlds;   // owned
    plasma::Node *m_selectWorldButton;   // retained across world-list rebuilds
    plasma::Node *m_worldMenuButton;     // retained across world-list rebuilds
    plasma::Node *m_deleteWorldButton;   // "Delete World"
    plasma::Node *m_nameError;           // the character name error (its shadow)
    plasma::Node *m_connectionErrorNode; // has an "edit" child
    OptionsWidget *m_optionsWidget;      // The containing start-menu panel.
    plasma::Node *m_optionsPanel;        // the options widget's panel
    float m_characterListScrollSpeed;    // 0; the character list's scroll speed (update blends it to 0)
    float m_worldListScrollSpeed;        // 0; the world list's scroll speed
    int m_selectedCharacter;             // index into m_characters
    int m_selectedWorld;                 // index into m_worlds
    ChatWidget *m_chatWidget;
    SpriteWidget *m_quickItemWidget;
    plasma::Vector<3, float> m_previewRotation; // Rotation in degrees for item and sprite previews.
    // Equipment widgets ordered as weapons, rings, neck, shoulders, chest, hands, feet, lamp, special
    // and pet.
    std::vector<plasma::Widget *> m_equipmentSlots;
    std::vector<plasma::Node *> m_equipmentBoxes; // the slots' frames
    bool m_ringMenuOpen;
    int m_ringMenuSelection; // Initially zero.
    // The open ring menu blends its radius to 200 and turns to the selected entry in degrees.
    float m_ringRadius;   // 0
    float m_ringRotation; // 0
    int m_seed;           // local world's seed; constructor/changeWorld pass it to World::setup
    std::string m_worldName;
    unsigned char unused800a6c[0x800a70 - 0x800a6c]; // never written or read
    long long m_interactionCreatureId;               // entity selected for interaction
    plasma::Vector<3, int>
        m_hoveredGroundItem; // the ground item under the cursor (zone x, y, index; key E picks it up); (-1, -1, 0)
    plasma::Vector<3, int>
        m_hoveredObject; // the zone object under the cursor (zone x, y, index; key R uses it); (-1, -1, 0)
    plasma::Matrix<float> m_projectionMatrix; // view to screen space
    plasma::Node *m_comboPoints;              // "combopoints" of gui.plx
    plasma::Node *m_blueprintNode;            // hidden when skills open
    plasma::Node *m_chatNode;                 // the chat widget's node
    Inventory m_craftingInventory;
    Inventory m_tradeInventory;
    std::list<ItemStack> m_soldItems;
    WorldMap m_worldMap;
    // The map-view offset, held by value.
    plasma::Vector<3, float> getMapOffset() { return m_mapOffset; }
    plasma::Vector<3, float> m_mapOffset; // 0; blocks from the corner of zone m_cameraZone
    std::list<ChatMessage> m_chatMessages;
    std::list<std::wstring> m_outgoingChatQueue;
    std::list<Item> m_disenchantSpirits;
    int m_spiritTimer;                                  // 0
    unsigned char unused1000e74[0x1000e78 - 0x1000e74]; // never written or read
    float m_craftingProgress;                           // -1 when idle
    Formula m_craftingFormula;
    // Six frustum planes extracted from projection times view: left, right, bottom, top, near and far.
    plasma::Vector<4, float> m_frustum[6];
    bool m_showPathSearch;
    Database m_charactersDatabase;
    Database m_worldsDatabase;
};

}

// House owns a grid of building cells and vectors of generated positions and object identifiers. The generator lays out rooms and furnishings within its x, y and z cell extents.
#pragma once
#include <vector>
#include "plasma/Vector.h"
#include "cube/Block.h"
#include "cube/Zone.h"

namespace cube {

// GenerateHouse's fields choose one crop, with a matching plant and produce.
enum FarmCrop {
    FarmCropPumpkin = 0,
    FarmCropSunflower = 1,
    FarmCropBean = 2,
    FarmCropWheat = 3,
    FarmCropCorn = 4,
    FarmCropPineapple = 5
};
// Number of entries, not an ID.
enum {
    FarmCropCount = 6
};

// Settlement architecture selected by the sprite families in generateHouse.
enum CityArchitecture {
    CityFramework = 0,
    CityWood = 1,
    CityWhitewood = 2,
    CityStone = 3,
    CityClay = 4,
    CityWhiteclay = 5
};
// Ruin style 1 shares the antique sprites, but has its own residents; only the explicit
// sprite-family names are established here.
enum RuinStyle {
    RuinAntique = 0,
    RuinDesert = 2,
    RuinJungle = 3,
    RuinGravesite = 4
};

// The kinds of a House cell (Cell::kind): the base storey, the floors, the roof cells, and an open
// archway.
enum HouseCellKind {
    HouseCellEmpty = 0,
    HouseCellFloor = 1,
    HouseCellBase = 2,
    HouseCellRoof = 3,
    HouseCellArc = 5
};

// The kinds of a settlement plot (HousePlot::kind). 10..16 are a city's class and craft halls and
// 17..20 the ruins' monuments (World::generateHouse).
enum HousePlotKind {
    HousePlotEmpty = 0,
    HousePlotHouse = 2,
    HousePlotTower = 3,
    HousePlotLandingPlace = 4,
    HousePlotWell = 5,
    HousePlotField = 6,
    HousePlotType11Ground = 7,
    HousePlotPyramid = 8,
    HousePlotMarket = 9,
    HousePlotWarriorHall = 10,
    HousePlotRangerHall = 11,
    HousePlotRogueHall = 12,
    HousePlotMageHall = 13,
    HousePlotSmithy = 14,
    HousePlotWeavingMill = 15,
    HousePlotCarpentersShop = 16,
    HousePlotSphinx = 17,
    HousePlotAntiqueBuilding = 18,
    HousePlotObelisk = 19,
    HousePlotAntiqueLeaderBuilding = 20
};

// What a house cell's room holds (House::Cell::variant): the furnishing pass switches on it.
enum HouseCellVariant {
    HouseCellPlain = 0,   // a random table, shelf, cupboard...
    HouseCellBedroom = 1, // beds
    HouseCellDining = 2,  // a table with stools (the inn's ground floor)
    HouseCellCounter = 3  // a shop's counter; the house is not stood on it
};

// A house's resident role, shared by HousePlot::variant and House::variant. Cell::variant
// independently selects the room's furniture.
enum HouseVariant {
    HouseVariantHome = 0,
    HouseVariantInn = 1,
    HouseVariantItemShop = 2,
    HouseVariantWeaponShop = 3,
    HouseVariantArmorShop = 4,
    HouseVariantAnalyzer = 5
};

// One plot of the house grid. Sampling sets interiorTop; the default fill leaves it unset.
struct HousePlot {
    HousePlot()
        : minimum(0),
          maximum(0),
          kind(HousePlotEmpty),
          variant(HouseVariantHome),
          rotation(0),
          score(0)
    {}
    int minimum;     // lowest sampled column top
    int maximum;     // highest sampled column top
    int interiorTop; // a column top inside the plot's margin
    int kind;        // HousePlotKind
    int variant;     // HouseVariant, copied into House::variant
    int rotation;    // quarter turns, not reduced modulo four
    float score;     // the record's influence at the plot centre
};

class House {
public:
    // One layout cell (12 bytes). The generator selects cell kinds and orientations, then samples the
    // terrain below the chosen cells.
    struct Cell {
        Cell()
            : kind(HouseCellEmpty),
              rotation(0),
              selected(false),
              stairs(false),
              groundHeight(0),
              variant(HouseCellPlain)
        {}

        unsigned char kind;     // Layout kind.
        unsigned char rotation; // Rotation in quarter turns.
        bool selected;          // One of the highest sampled cells.
        // A stair floor reached from below: no carpet or house support is placed in the cell underneath.
        bool stairs;
        int groundHeight;      // maximum baseZ+size below the cell
        unsigned char variant; // HouseCellVariant
    };

    House(int sizeX, int sizeY, int sizeZ);
    virtual ~House();

    // Building geometry helpers.
    Cell &cell(int x, int y, int z)
    {
        transform(x, y, z);
        if (x >= 0 && y >= 0 && z >= 0 && x < m_sizeX && y < m_sizeY && z < m_sizeZ)
            return m_cells[(z * m_sizeY + y) * m_sizeX + x];
        static Cell outside;
        outside.kind = HouseCellEmpty;
        return outside;
    }

    // Extent along x and y after rotation, and height along z.
    int sizeX() const { return m_rotation % 2 ? m_sizeY : m_sizeX; }

    int sizeY() const { return m_rotation % 2 ? m_sizeX : m_sizeY; }

    int sizeZ() const { return m_sizeZ; }

    // Rotate into the stored cell grid, then apply its optional y reflection.
    void transform(int &x, int &y, int &z) const
    {
        switch (m_rotation % 4) {
        case 1:
            std::swap(x, y);
            x = m_sizeX - x - 1;
            break;
        case 2:
            x = m_sizeX - x - 1;
            y = m_sizeY - y - 1;
            break;
        case 3:
            std::swap(x, y);
            y = m_sizeY - y - 1;
            break;
        }
        if (m_reflectY)
            y = m_sizeY - y - 1;
    }

    int m_rotation;                    // quarter turns
    bool m_reflectY;                   // reflect y after rotation (same transform)
    plasma::Vector<3, int> m_position; // Block origin assigned by generation.
    // Initialized to (-1, -1, 0), otherwise unused.
    int unused18; // -1
    int unused1c; // -1
    int unused20; // Initially zero.
    // Ground-floor cell centers, entrance centers, counters, seat identifiers and bed identifiers used
    // by resident behaviors.
    std::vector<plasma::Vector<3, int>> rooms;
    std::vector<plasma::Vector<3, int>> entrances;
    std::vector<plasma::Vector<3, int>> counters;
    std::vector<plasma::Vector<3, int>> seats; // stools and benches
    std::vector<plasma::Vector<3, int>> beds;
    // House variant selecting the innkeeper or vendor role.
    int variant;
    int m_sizeX;
    int m_sizeY;
    int m_sizeZ;
    Cell *m_cells; // sizeX * sizeY * sizeZ cells
};

long long generationKey(int x, int y, int index);
// The generator passes a third argument (0) that the body ignores.
Zone::Object makeHouseObject(const plasma::Vector<3, Fixed> &position, int rotation, int style);
Zone::Object makeInteriorObject(const plasma::Vector<3, Fixed> &position, int rotation, int style);
// The generator passes a third argument (0) that the body ignores.
Zone::Object makeSmallHouseObject(const plasma::Vector<3, Fixed> &position, int rotation, int style);
// The generator passes a third argument (0) that the body ignores.
Zone::Object makeLargeHouseObject(const plasma::Vector<3, Fixed> &position, int rotation, int style);
// Radices of generationKey: zone pair followed by the spawn index.
enum {
    GeneratedCreatureZoneRadix = 65536,
    GeneratedCreatureIndexRadix = 256
};
// Added to every generationKey (not in the original): without it the zone (0, 0) gives the ids 0 (none) and
// 1..10 (the players'). Its low 32 bits are zero, so (int) casts of an id and the order of ids stay the same.
// Based on coremaze/Cube-World-GUID-Fix-Mod.
const long long GeneratedCreatureIdOffset = 0x10000000000LL;

}

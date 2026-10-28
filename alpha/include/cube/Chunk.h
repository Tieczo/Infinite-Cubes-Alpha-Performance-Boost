// Chunk holds the meshes of a 32-by-32-block rendering area. ChunkBuffer owns its three Direct3D buffers and releases them on destruction.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <list>
#include <vector>
#include "plasma/Vector.h"
#include "cube/Fixed.h"
#include "cube/Zone.h"

namespace cube {

struct ChunkBuffer {
    ChunkBuffer()
        : vertexBuffer(0),
          indexBuffer(0),
          waterIndexBuffer(0),
          baseZ(0)
    {}
    virtual ~ChunkBuffer()
    {
        if (vertexBuffer)
            vertexBuffer->Release();
        if (indexBuffer)
            indexBuffer->Release();
        if (waterIndexBuffer)
            waterIndexBuffer->Release();
    }

    IDirect3DVertexBuffer9 *vertexBuffer;
    IDirect3DIndexBuffer9 *indexBuffer;
    IDirect3DIndexBuffer9 *waterIndexBuffer; // Water-face indices, filled by updateChunk and bound by the water pass.
    // Draws indexed triangles using vertexCount and the solid or water primitive count, translated to
    // the chunk corner and base height.
    int vertexCount;
    int primitiveCount;
    int waterPrimitiveCount;
    int baseZ; // zeroed by the constructor
};

// The chunk plant lists contain the zone's plant records.

struct Chunk {
    Chunk();
    virtual ~Chunk();
    // Empties the chunk (its buffers, plants and static entities) under its lock and marks it as
    // holding no zone.
    void reset();

    std::list<ChunkBuffer *> chunkBuffers;    // owned
    std::list<ChunkBuffer *> newChunkBuffers; // owned
    plasma::Vector<2, int> position;          // Chunk-grid coordinates in 32-block units; (-1, -1) when empty.
    // The mesh bounds determine their midpoint and a radius of 1.5 times the distance to boundsMax.
    plasma::Vector<3, Fixed> boundsMin;
    plasma::Vector<3, Fixed> boundsMax;
    plasma::Vector<3, Fixed> center;
    float radius;
    float squaredCameraDistance;
    int remeshTime;            // 0
    bool needsRemesh;          // false
    unsigned char unused75[3]; // Unused alignment bytes.
    // Point lights reaching this chunk, nearest first and at most sixteen, stored as position/radius
    // and color.
    plasma::Vector<4, float> lightPositions[16];
    plasma::Vector<3, float> lightColors[16];
    int lightCount;  // 0 after reset
    // The entries of the two arrays that may not be zero (render clears only those); all sixteen at first.
    int lightsWritten;
    int vertexCount; // the vertices of the new meshes (updateChunk); the chunk thread sums it against its budget
    std::list<Plant> plants;
    // The plants of the list that render looks at for every chunk in every frame, in list order (not in
    // the original, used with the Fast Plant Scan option): the lamps (PlantFlagEmissive) and the plants
    // that call (birds, owls and crickets). indexPlants rebuilds them whenever plants changes.
    std::vector<Plant *> emissivePlants;
    std::vector<Plant *> callingPlants;
    void indexPlants();
    // New plants from meshing replace the current list under the lock when buffers are swapped.
    std::list<Plant> newPlants;
    CRITICAL_SECTION lock;
};

}

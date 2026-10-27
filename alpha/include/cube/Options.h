#pragma once
namespace cube {

enum {
    MaximumCreatureDistance = 600
}; // percent; at 800% the creature updates took too long for a playable frame rate
// Names are the options.cfg keys read and written by this file.
struct Options {
    Options();
    void load();
    void save();
    int fullscreen;
    int resolutionX;
    int resolutionY;
    int antiAliasing;
    int renderDistance;
    int soundVolume;
    int musicVolume;
    int cameraSpeed;
    int cameraSmoothness;
    int invertY;
    int language;
    int minTimeStep;
    int showFPS; // the frame rate after time, temperature and humidity (not in the original)
    // Percentages of the original distances (not in the original): how far plants are drawn, and how
    // far creatures are drawn and come to life.
    int plantDistance;
    int creatureDistance; // at most MaximumCreatureDistance
    int fastCreatureUpdates; // World::m_fastCreatureUpdates (not in the original)
    int fastTerrainLoading;  // the map thread lets a waiting chunk thread go first (not in the original)
};
}

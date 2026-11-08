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
    int fastPlantScan;       // render visits only the lamps and calling plants of a chunk (not in the original)
    int threadedRendering;   // a drawing thread makes the frame's device calls (not in the original)
    int spikeLog;            // spikes.log: long frames and what happened in them (SpikeLog.h; not in the original)
    int guardianReliableBlock; // World::m_guardianReliableBlock (not in the original)
    // Holding zoomKey narrows the view (GameController::updateZoom; not in the original). The key is a
    // virtual-key code, Caps Lock (0x14) at first: X opens the skills.
    int zoom;
    int zoomKey;
};
}

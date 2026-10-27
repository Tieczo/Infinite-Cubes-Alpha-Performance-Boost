#pragma once
namespace cube {
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
};
}

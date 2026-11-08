// Reads and writes the client's options. This build keeps them in its own file, options-InfiniteCubes.cfg, so that the
// original game's options.cfg is never changed (the original reads the render distance without a limit and drops the
// options it does not know). Until the own file exists, the original's options.cfg is read once instead.
#include "cube/Options.h"
#include <fstream>
#include <string>
namespace cube {
namespace {
const char *const OptionsFileName = "options-InfiniteCubes.cfg";
}
// The settings' defaults (anonymous: this file's own): the render distance, the volumes and the
// camera's speed and smoothness in percent, the frame's minimum time in milliseconds.
namespace {
enum {
    DefaultRenderDistance = 50
};
enum {
    DefaultVolume = 100
};
enum {
    DefaultCameraSpeed = 50
};
enum {
    DefaultCameraSmoothness = 80
};
enum {
    DefaultMinTimeStep = 9
};
enum {
    DefaultZoomKey = 0x14 // VK_CAPITAL
};
}

Options::Options()
    : fullscreen(0),
      resolutionX(0),
      resolutionY(0),
      antiAliasing(0),
      renderDistance(DefaultRenderDistance),
      soundVolume(DefaultVolume),
      musicVolume(DefaultVolume),
      cameraSpeed(DefaultCameraSpeed),
      cameraSmoothness(DefaultCameraSmoothness),
      invertY(0),
      language(0),
      minTimeStep(DefaultMinTimeStep),
      showFPS(0),
      plantDistance(100),
      creatureDistance(100),
      fastCreatureUpdates(1),
      fastTerrainLoading(1),
      fastPlantScan(1),
      threadedRendering(0),
      spikeLog(0),
      guardianReliableBlock(0),
      zoom(1),
      zoomKey(DefaultZoomKey)
{}

void Options::load()
{
    std::ifstream file(OptionsFileName);
    if (!file.good())
        file.open("options.cfg");
    while (file.good()) {
        std::string key;
        file >> key;
        if (!file.good())
            break;
        if (key == "fullscreen")
            file >> fullscreen;
        if (key == "resolutionX")
            file >> resolutionX;
        if (key == "resolutionY")
            file >> resolutionY;
        if (key == "antiAliasing")
            file >> antiAliasing;
        if (key == "renderDistance")
            file >> renderDistance;
        if (key == "soundVolume")
            file >> soundVolume;
        if (key == "musicVolume")
            file >> musicVolume;
        if (key == "cameraSpeed")
            file >> cameraSpeed;
        if (key == "cameraSmoothness")
            file >> cameraSmoothness;
        if (key == "language")
            file >> language;
        if (key == "invertY")
            file >> invertY;
        if (key == "minTimeStep")
            file >> minTimeStep;
        if (key == "showFPS")
            file >> showFPS;
        if (key == "plantDistance")
            file >> plantDistance;
        if (key == "creatureDistance")
            file >> creatureDistance;
        if (key == "fastCreatureUpdates")
            file >> fastCreatureUpdates;
        if (key == "fastTerrainLoading")
            file >> fastTerrainLoading;
        if (key == "fastPlantScan")
            file >> fastPlantScan;
        if (key == "threadedRendering")
            file >> threadedRendering;
        if (key == "spikeLog")
            file >> spikeLog;
        if (key == "guardianReliableBlock")
            file >> guardianReliableBlock;
        if (key == "zoom")
            file >> zoom;
        if (key == "zoomKey")
            file >> zoomKey;
    }
    if (creatureDistance > MaximumCreatureDistance)
        creatureDistance = MaximumCreatureDistance;
    zoomKey &= 0xff;
}

void Options::save()
{
    std::ofstream file(OptionsFileName);
    file << "fullscreen " << fullscreen << std::endl;
    file << "resolutionX " << resolutionX << std::endl;
    file << "resolutionY " << resolutionY << std::endl;
    file << "antiAliasing " << antiAliasing << std::endl;
    file << "renderDistance " << renderDistance << std::endl;
    file << "soundVolume " << soundVolume << std::endl;
    file << "musicVolume " << musicVolume << std::endl;
    file << "cameraSpeed " << cameraSpeed << std::endl;
    file << "cameraSmoothness " << cameraSmoothness << std::endl;
    file << "language " << language << std::endl;
    file << "invertY " << invertY << std::endl;
    file << "minTimeStep " << minTimeStep << std::endl;
    file << "showFPS " << showFPS << std::endl;
    file << "plantDistance " << plantDistance << std::endl;
    file << "creatureDistance " << creatureDistance << std::endl;
    file << "fastCreatureUpdates " << fastCreatureUpdates << std::endl;
    file << "fastTerrainLoading " << fastTerrainLoading << std::endl;
    file << "fastPlantScan " << fastPlantScan << std::endl;
    file << "threadedRendering " << threadedRendering << std::endl;
    file << "spikeLog " << spikeLog << std::endl;
    file << "guardianReliableBlock " << guardianReliableBlock << std::endl;
    file << "zoom " << zoom << std::endl;
    file << "zoomKey " << zoomKey << std::endl;
}

}

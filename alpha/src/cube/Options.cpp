// Reads and writes options.cfg for the client.
#include "cube/Options.h"
#include <fstream>
#include <string>
namespace cube {
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
      fastTerrainLoading(1)
{}

void Options::load()
{
    std::ifstream file("options.cfg");
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
    }
    if (creatureDistance > MaximumCreatureDistance)
        creatureDistance = MaximumCreatureDistance;
}

void Options::save()
{
    std::ofstream file("options.cfg");
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
}

}

// OptionsWidget displays display and gameplay settings.
#pragma once
#include "plasma/Widget.h"
#include "cube/Options.h"
#include <vector>
namespace cube {
class GameController;
class OptionsWidget : public plasma::Widget {
public:
    OptionsWidget(plasma::Engine *engine, plasma::Node *node, GameController *game, plasma::Node *leftButton,
                  plasma::Node *rightButton, plasma::Node *actionButtonTemplate);
    virtual void draw();
    virtual void resized();
    // Each option has paired decrease and increase handlers; fullscreen cycles in either direction.
    void AntiAliasingDecrease();
    void AntiAliasingIncrease();
    void CameraSmoothnessDecrease();
    void CameraSmoothnessIncrease();
    void CameraSpeedDecrease();
    void CameraSpeedIncrease();
    // Increasing the minimum time step lowers the FPS limit.
    void FPSLimitDecrease();
    void FPSLimitIncrease();
    void FullscreenIncrease();
    void InvertYToggle();
    void LanguageDecrease();
    void LanguageIncrease();
    void MusicVolumeDecrease();
    void MusicVolumeIncrease();
    void RenderDistanceDecrease();
    void RenderDistanceIncrease();
    void ShowFPSToggle();
    void PlantDistanceDecrease();
    void PlantDistanceIncrease();
    void CreatureDistanceDecrease();
    void CreatureDistanceIncrease();
    void FastCreatureUpdatesToggle();
    void FastTerrainLoadingToggle();
    void ResolutionDecrease();
    void ResolutionIncrease();
    void SoundVolumeDecrease();
    void SoundVolumeIncrease();
    void setOptions(const Options &options);
    // Apply applies options, Cancel hides the parent, and OK does both.
    void Apply();
    void Cancel();
    void Ok();
    void FullscreenDecrease();
    std::vector<plasma::Vector<2, int>> m_resolutions; // width/height pairs
    GameController *m_game;
    plasma::Node *m_previousFullscreen;
    plasma::Node *m_nextFullscreen;
    plasma::Node *m_previousResolution;
    plasma::Node *m_nextResolution;
    plasma::Node *m_previousAntiAliasing;
    plasma::Node *m_nextAntiAliasing;
    plasma::Node *m_previousRenderDistance;
    plasma::Node *m_nextRenderDistance;
    plasma::Node *m_previousSoundVolume;
    plasma::Node *m_nextSoundVolume;
    plasma::Node *m_previousMusicVolume;
    plasma::Node *m_nextMusicVolume;
    plasma::Node *m_previousCameraSpeed;
    plasma::Node *m_nextCameraSpeed;
    plasma::Node *m_previousCameraSmoothness;
    plasma::Node *m_nextCameraSmoothness;
    plasma::Node *m_previousInvertY;
    plasma::Node *m_nextInvertY;
    plasma::Node *m_previousShowFPS;
    plasma::Node *m_nextShowFPS;
    plasma::Node *m_previousPlantDistance;
    plasma::Node *m_nextPlantDistance;
    plasma::Node *m_previousCreatureDistance;
    plasma::Node *m_nextCreatureDistance;
    plasma::Node *m_previousFastCreatureUpdates;
    plasma::Node *m_nextFastCreatureUpdates;
    plasma::Node *m_previousFastTerrainLoading;
    plasma::Node *m_nextFastTerrainLoading;
    plasma::Node *m_previousMinTimeStep;
    plasma::Node *m_nextMinTimeStep;
    plasma::Node *m_previousLanguage;
    plasma::Node *m_nextLanguage;
    plasma::Node *m_applyButton;
    plasma::Node *m_okButton;
    plasma::Node *m_cancelButton;
    Options m_options;
    int m_resolutionIndex;
};
}

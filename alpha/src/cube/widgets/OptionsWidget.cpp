// OptionsWidget edits and applies display and gameplay settings.
#include "cube/DefaultBlocks.h"
#include "cube/widgets/OptionsWidget.h"
#include "cube/GameController.h"
#include <iostream>
#include <fstream>
#include "plasma/Node.h"
#include "plasma/Display.h"
#include "plasma/Engine.h"
#include "plasma/FontEngine.h"
#include "plasma/Font.h"
#include <sstream>
namespace cube {
// The arrow buttons' steps and the settings' bounds (anonymous: this file's own). The camera's
// speed and smoothness, the render distance and the volumes are percentages; minTimeStep is the
// frame's minimum in milliseconds.
namespace {
enum {
    MaximumAntiAliasing = 2
};
enum {
    CameraSmoothnessStep = 5
};
enum {
    MaximumCameraSmoothness = 100
};
enum {
    CameraSpeedStep = 5
};
enum {
    MinimumCameraSpeed = 10
};
enum {
    MaximumCameraSpeed = 100
};
enum {
    MaximumMinTimeStep = 50
};
enum {
    VolumeStep = 10
}; // the music's and the sounds'
enum {
    MaximumVolume = 100
};
enum {
    RenderDistanceStep = 5
};
enum {
    MinimumRenderDistance = 25
};
enum {
    MaximumRenderDistance = 200
};
}

// Each option gets a copy of the left and right arrow buttons; Apply, OK and Cancel are copies of
// the plain button. The action buttons are 110 x 15 pixels.
OptionsWidget::OptionsWidget(plasma::Engine *engine, plasma::Node *node, GameController *game, plasma::Node *leftButton,
                             plasma::Node *rightButton, plasma::Node *actionButtonTemplate)
    : Widget(engine, node),
      m_game(game)
{
    m_resolutionIndex = 0;
    m_previousFullscreen = leftButton->instance(node);
    m_previousFullscreen->setTransformation(leftButton->m_transformation->clone());
    m_previousFullscreen->setDisplay(leftButton->m_display->clone());
    m_previousFullscreen->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                        &OptionsWidget::FullscreenDecrease, true);
    m_nextFullscreen = rightButton->instance(node);
    m_nextFullscreen->setTransformation(rightButton->m_transformation->clone());
    m_nextFullscreen->setDisplay(rightButton->m_display->clone());
    m_nextFullscreen->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                    &OptionsWidget::FullscreenIncrease, true);
    m_previousResolution = leftButton->instance(node);
    m_previousResolution->setTransformation(leftButton->m_transformation->clone());
    m_previousResolution->setDisplay(leftButton->m_display->clone());
    m_previousResolution->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                        &OptionsWidget::ResolutionDecrease, true);
    m_nextResolution = rightButton->instance(node);
    m_nextResolution->setTransformation(rightButton->m_transformation->clone());
    m_nextResolution->setDisplay(rightButton->m_display->clone());
    m_nextResolution->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                    &OptionsWidget::ResolutionIncrease, true);
    m_previousAntiAliasing = leftButton->instance(node);
    m_previousAntiAliasing->setTransformation(leftButton->m_transformation->clone());
    m_previousAntiAliasing->setDisplay(leftButton->m_display->clone());
    m_previousAntiAliasing->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                          &OptionsWidget::AntiAliasingDecrease, true);
    m_nextAntiAliasing = rightButton->instance(node);
    m_nextAntiAliasing->setTransformation(rightButton->m_transformation->clone());
    m_nextAntiAliasing->setDisplay(rightButton->m_display->clone());
    m_nextAntiAliasing->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                      &OptionsWidget::AntiAliasingIncrease, true);
    m_previousRenderDistance = leftButton->instance(node);
    m_previousRenderDistance->setTransformation(leftButton->m_transformation->clone());
    m_previousRenderDistance->setDisplay(leftButton->m_display->clone());
    m_previousRenderDistance->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                            &OptionsWidget::RenderDistanceDecrease, true);
    m_nextRenderDistance = rightButton->instance(node);
    m_nextRenderDistance->setTransformation(rightButton->m_transformation->clone());
    m_nextRenderDistance->setDisplay(rightButton->m_display->clone());
    m_nextRenderDistance->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                        &OptionsWidget::RenderDistanceIncrease, true);
    m_previousSoundVolume = leftButton->instance(node);
    m_previousSoundVolume->setTransformation(leftButton->m_transformation->clone());
    m_previousSoundVolume->setDisplay(leftButton->m_display->clone());
    m_previousSoundVolume->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                         &OptionsWidget::SoundVolumeDecrease, true);
    m_nextSoundVolume = rightButton->instance(node);
    m_nextSoundVolume->setTransformation(rightButton->m_transformation->clone());
    m_nextSoundVolume->setDisplay(rightButton->m_display->clone());
    m_nextSoundVolume->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                     &OptionsWidget::SoundVolumeIncrease, true);
    m_previousMusicVolume = leftButton->instance(node);
    m_previousMusicVolume->setTransformation(leftButton->m_transformation->clone());
    m_previousMusicVolume->setDisplay(leftButton->m_display->clone());
    m_previousMusicVolume->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                         &OptionsWidget::MusicVolumeDecrease, true);
    m_nextMusicVolume = rightButton->instance(node);
    m_nextMusicVolume->setTransformation(rightButton->m_transformation->clone());
    m_nextMusicVolume->setDisplay(rightButton->m_display->clone());
    m_nextMusicVolume->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                     &OptionsWidget::MusicVolumeIncrease, true);
    m_previousCameraSpeed = leftButton->instance(node);
    m_previousCameraSpeed->setTransformation(leftButton->m_transformation->clone());
    m_previousCameraSpeed->setDisplay(leftButton->m_display->clone());
    m_previousCameraSpeed->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                         &OptionsWidget::CameraSpeedDecrease, true);
    m_nextCameraSpeed = rightButton->instance(node);
    m_nextCameraSpeed->setTransformation(rightButton->m_transformation->clone());
    m_nextCameraSpeed->setDisplay(rightButton->m_display->clone());
    m_nextCameraSpeed->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                     &OptionsWidget::CameraSpeedIncrease, true);
    m_previousCameraSmoothness = leftButton->instance(node);
    m_previousCameraSmoothness->setTransformation(leftButton->m_transformation->clone());
    m_previousCameraSmoothness->setDisplay(leftButton->m_display->clone());
    m_previousCameraSmoothness->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                              &OptionsWidget::CameraSmoothnessDecrease, true);
    m_nextCameraSmoothness = rightButton->instance(node);
    m_nextCameraSmoothness->setTransformation(rightButton->m_transformation->clone());
    m_nextCameraSmoothness->setDisplay(rightButton->m_display->clone());
    m_nextCameraSmoothness->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                          &OptionsWidget::CameraSmoothnessIncrease, true);
    m_previousInvertY = leftButton->instance(node);
    m_previousInvertY->setTransformation(leftButton->m_transformation->clone());
    m_previousInvertY->setDisplay(leftButton->m_display->clone());
    m_previousInvertY->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                     &OptionsWidget::InvertYToggle, true);
    m_nextInvertY = rightButton->instance(node);
    m_nextInvertY->setTransformation(rightButton->m_transformation->clone());
    m_nextInvertY->setDisplay(rightButton->m_display->clone());
    m_nextInvertY->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                 &OptionsWidget::InvertYToggle, true);
    m_previousMinTimeStep = leftButton->instance(node);
    m_previousMinTimeStep->setTransformation(leftButton->m_transformation->clone());
    m_previousMinTimeStep->setDisplay(leftButton->m_display->clone());
    m_previousMinTimeStep->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                         &OptionsWidget::FPSLimitDecrease, true);
    m_nextMinTimeStep = rightButton->instance(node);
    m_nextMinTimeStep->setTransformation(rightButton->m_transformation->clone());
    m_nextMinTimeStep->setDisplay(rightButton->m_display->clone());
    m_nextMinTimeStep->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                     &OptionsWidget::FPSLimitIncrease, true);
    m_previousLanguage = leftButton->instance(node);
    m_previousLanguage->setTransformation(leftButton->m_transformation->clone());
    m_previousLanguage->setDisplay(leftButton->m_display->clone());
    m_previousLanguage->connectWidget(L"leftbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                      &OptionsWidget::LanguageDecrease, true);
    m_nextLanguage = rightButton->instance(node);
    m_nextLanguage->setTransformation(rightButton->m_transformation->clone());
    m_nextLanguage->setDisplay(rightButton->m_display->clone());
    m_nextLanguage->connectWidget(L"rightbutton", plasma::WIDGET_EVENT_POINTER_PRESSED, this,
                                  &OptionsWidget::LanguageIncrease, true);
    m_applyButton = actionButtonTemplate->clone(node);
    m_okButton = actionButtonTemplate->clone(node);
    m_cancelButton = actionButtonTemplate->clone(node);
    m_applyButton->widget()->resizeTo(110.0f, 15.0f, true);
    m_applyButton->setText(L"Apply");
    m_okButton->widget()->resizeTo(110.0f, 15.0f, true);
    m_okButton->setText(L"OK");
    m_cancelButton->widget()->resizeTo(110.0f, 15.0f, true);
    m_cancelButton->setText(L"Cancel");
    m_applyButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_PRESSED, this, &OptionsWidget::Apply, true, true);
    m_okButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_PRESSED, this, &OptionsWidget::Ok, true, true);
    m_cancelButton->widget()->connect(plasma::WIDGET_EVENT_POINTER_PRESSED, this, &OptionsWidget::Cancel, true, true);
}

// The options screen: each setting's name at x 15 and its value centred right of x 240, 30 units
// apart, then the arrow buttons cyan under the pointer. The text is 12 pixels high, stroked 3; the
// value is centred in the width less 250.
void OptionsWidget::draw()
{
    std::wstringstream text;
    if (m_options.fullscreen)
        text << L"Fullscreen";
    else
        text << L"Windowed";
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Mode", 0.0f, 0.0f, 15.0f, 27.0f, 12.0f, 3.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Mode", 0.0f, 0.0f, 15.0f, 27.0f, 12.0f, 0.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 27.0f, 12.0f, 3.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 27.0f, 12.0f, 0.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    text.str(L"");
    if (!m_resolutions.empty())
        text << m_resolutions[m_resolutionIndex][0] << L" x " << m_resolutions[m_resolutionIndex][1];
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Resolution", 0.0f, 0.0f, 15.0f, 57.0f, 12.0f, 3.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Resolution", 0.0f, 0.0f, 15.0f, 57.0f, 12.0f, 0.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 57.0f, 12.0f, 3.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 57.0f, 12.0f, 0.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    text.str(L"");
    if (m_options.antiAliasing <= 0)
        text << L"Disabled";
    else
        text << m_options.antiAliasing * 2 << L"x";
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Anti-aliasing", 0.0f, 0.0f, 15.0f, 87.0f, 12.0f, 3.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Anti-aliasing", 0.0f, 0.0f, 15.0f, 87.0f, 12.0f, 0.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 87.0f, 12.0f, 3.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 87.0f, 12.0f, 0.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    text.str(L"");
    text << m_options.renderDistance << L"%";
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Render Distance", 0.0f, 0.0f, 15.0f, 117.0f, 12.0f, 3.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Render Distance", 0.0f, 0.0f, 15.0f, 117.0f, 12.0f, 0.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 117.0f, 12.0f, 3.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 117.0f, 12.0f, 0.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    text.str(L"");
    text << m_options.soundVolume << L"%";
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Sound FX Volume", 0.0f, 0.0f, 15.0f, 147.0f, 12.0f, 3.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Sound FX Volume", 0.0f, 0.0f, 15.0f, 147.0f, 12.0f, 0.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 147.0f, 12.0f, 3.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 147.0f, 12.0f, 0.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    text.str(L"");
    text << m_options.musicVolume << L"%";
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Music Volume", 0.0f, 0.0f, 15.0f, 177.0f, 12.0f, 3.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Music Volume", 0.0f, 0.0f, 15.0f, 177.0f, 12.0f, 0.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 177.0f, 12.0f, 3.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 177.0f, 12.0f, 0.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    text.str(L"");
    text << m_options.cameraSpeed << L"%";
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Camera Speed", 0.0f, 0.0f, 15.0f, 207.0f, 12.0f, 3.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Camera Speed", 0.0f, 0.0f, 15.0f, 207.0f, 12.0f, 0.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 207.0f, 12.0f, 3.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 207.0f, 12.0f, 0.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    text.str(L"");
    text << m_options.cameraSmoothness << L"%";
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Camera Smoothness", 0.0f, 0.0f, 15.0f, 237.0f, 12.0f,
                                          3.0f, plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Camera Smoothness", 0.0f, 0.0f, 15.0f, 237.0f, 12.0f,
                                          0.0f, plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 237.0f, 12.0f, 3.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 237.0f, 12.0f, 0.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    text.str(L"");
    text << (m_options.invertY ? "Yes" : "No");
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Invert Y Axis", 0.0f, 0.0f, 15.0f, 267.0f, 12.0f, 3.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Invert Y Axis", 0.0f, 0.0f, 15.0f, 267.0f, 12.0f, 0.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 267.0f, 12.0f, 3.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 267.0f, 12.0f, 0.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    text.str(L"");
    if (m_options.minTimeStep > 0)
        text << 1000 / m_options.minTimeStep << " fps";
    else
        text << "none";
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"FPS Limit", 0.0f, 0.0f, 15.0f, 297.0f, 12.0f, 3.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"FPS Limit", 0.0f, 0.0f, 15.0f, 297.0f, 12.0f, 0.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 297.0f, 12.0f, 3.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 297.0f, 12.0f, 0.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    text.str(L"");
    text << m_options.language;
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Language", 0.0f, 0.0f, 15.0f, 327.0f, 12.0f, 3.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(L"resource1.dat", L"Language", 0.0f, 0.0f, 15.0f, 327.0f, 12.0f, 0.0f,
                                          plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
                                          plasma::TEXT_FLAGS_NONE, -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 327.0f, 12.0f, 3.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    m_engine->getFontEngine()->renderText(
        L"resource1.dat", text.str(), 0.0f, 0.0f, (getContentWidth() - 250.0f) * 0.5f + 240.0f, 327.0f, 12.0f, 0.0f,
        plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements,
        plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 0.0f).m_elements, (1 << plasma::TEXT_CENTER), -1.0f, true);
    text.str(L"");
    m_previousFullscreen->display()->setFillColor(m_previousFullscreen->widget()->isPointerOver()
                                                      ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                      : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_nextFullscreen->display()->setFillColor(m_nextFullscreen->widget()->isPointerOver()
                                                  ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                  : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_previousResolution->display()->setFillColor(m_previousResolution->widget()->isPointerOver()
                                                      ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                      : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_nextResolution->display()->setFillColor(m_nextResolution->widget()->isPointerOver()
                                                  ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                  : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_previousAntiAliasing->display()->setFillColor(m_previousAntiAliasing->widget()->isPointerOver()
                                                        ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                        : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_nextAntiAliasing->display()->setFillColor(m_nextAntiAliasing->widget()->isPointerOver()
                                                    ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                    : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_previousRenderDistance->display()->setFillColor(m_previousRenderDistance->widget()->isPointerOver()
                                                          ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                          : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_nextRenderDistance->display()->setFillColor(m_nextRenderDistance->widget()->isPointerOver()
                                                      ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                      : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_previousSoundVolume->display()->setFillColor(m_previousSoundVolume->widget()->isPointerOver()
                                                       ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                       : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_nextSoundVolume->display()->setFillColor(m_nextSoundVolume->widget()->isPointerOver()
                                                   ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                   : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_previousMusicVolume->display()->setFillColor(m_previousMusicVolume->widget()->isPointerOver()
                                                       ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                       : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_nextMusicVolume->display()->setFillColor(m_nextMusicVolume->widget()->isPointerOver()
                                                   ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                   : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_previousCameraSpeed->display()->setFillColor(m_previousCameraSpeed->widget()->isPointerOver()
                                                       ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                       : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_nextCameraSpeed->display()->setFillColor(m_nextCameraSpeed->widget()->isPointerOver()
                                                   ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                   : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_previousCameraSmoothness->display()->setFillColor(m_previousCameraSmoothness->widget()->isPointerOver()
                                                            ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                            : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_nextCameraSmoothness->display()->setFillColor(m_nextCameraSmoothness->widget()->isPointerOver()
                                                        ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                        : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_previousInvertY->display()->setFillColor(m_previousInvertY->widget()->isPointerOver()
                                                   ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                   : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_nextInvertY->display()->setFillColor(m_nextInvertY->widget()->isPointerOver()
                                               ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                               : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_previousMinTimeStep->display()->setFillColor(m_previousMinTimeStep->widget()->isPointerOver()
                                                       ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                       : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_nextMinTimeStep->display()->setFillColor(m_nextMinTimeStep->widget()->isPointerOver()
                                                   ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                   : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_previousLanguage->display()->setFillColor(m_previousLanguage->widget()->isPointerOver()
                                                    ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                    : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
    m_nextLanguage->display()->setFillColor(m_nextLanguage->widget()->isPointerOver()
                                                ? plasma::Vector<4, float>(0.0f, 1.0f, 1.0f, 1.0f)
                                                : plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
}

void OptionsWidget::AntiAliasingDecrease()
{
    --m_options.antiAliasing;
    m_options.antiAliasing = m_options.antiAliasing < 0 ? 0 : m_options.antiAliasing;
}

void OptionsWidget::AntiAliasingIncrease()
{
    ++m_options.antiAliasing;
    m_options.antiAliasing =
        m_options.antiAliasing > MaximumAntiAliasing ? MaximumAntiAliasing : m_options.antiAliasing;
}

void OptionsWidget::Apply()
{
    m_game->applyOptions(m_options);
}

void OptionsWidget::CameraSmoothnessDecrease()
{
    m_options.cameraSmoothness -= CameraSmoothnessStep;
    m_options.cameraSmoothness = m_options.cameraSmoothness < 0 ? 0 : m_options.cameraSmoothness;
}

void OptionsWidget::CameraSmoothnessIncrease()
{
    m_options.cameraSmoothness += CameraSmoothnessStep;
    m_options.cameraSmoothness =
        m_options.cameraSmoothness > MaximumCameraSmoothness ? MaximumCameraSmoothness : m_options.cameraSmoothness;
}

void OptionsWidget::CameraSpeedDecrease()
{
    m_options.cameraSpeed -= CameraSpeedStep;
    m_options.cameraSpeed = m_options.cameraSpeed < MinimumCameraSpeed ? MinimumCameraSpeed : m_options.cameraSpeed;
}

void OptionsWidget::CameraSpeedIncrease()
{
    m_options.cameraSpeed += CameraSpeedStep;
    m_options.cameraSpeed = m_options.cameraSpeed > MaximumCameraSpeed ? MaximumCameraSpeed : m_options.cameraSpeed;
}

// Closes the options screen (its parent widget's node).
void OptionsWidget::Cancel()
{
    getParentWidget()->getNode()->setVisible(false);
}

void OptionsWidget::FPSLimitDecrease()
{
    ++m_options.minTimeStep;
    m_options.minTimeStep = m_options.minTimeStep > MaximumMinTimeStep ? MaximumMinTimeStep : m_options.minTimeStep;
}

void OptionsWidget::FPSLimitIncrease()
{
    --m_options.minTimeStep;
    m_options.minTimeStep = m_options.minTimeStep < 0 ? 0 : m_options.minTimeStep;
}

void OptionsWidget::FullscreenDecrease()
{
    std::cout << "click " << std::endl;
    m_options.fullscreen = (m_options.fullscreen + 1) % 2;
}

void OptionsWidget::FullscreenIncrease()
{
    m_options.fullscreen = (m_options.fullscreen + 1) % 2;
}

void OptionsWidget::InvertYToggle()
{
    m_options.invertY = !m_options.invertY;
}

void OptionsWidget::LanguageDecrease()
{
    --m_options.language;
}

void OptionsWidget::LanguageIncrease()
{
    ++m_options.language;
}

void OptionsWidget::MusicVolumeDecrease()
{
    m_options.musicVolume -= VolumeStep;
    m_options.musicVolume = m_options.musicVolume < 0 ? 0 : m_options.musicVolume;
}

void OptionsWidget::MusicVolumeIncrease()
{
    m_options.musicVolume += VolumeStep;
    m_options.musicVolume = m_options.musicVolume > MaximumVolume ? MaximumVolume : m_options.musicVolume;
}

// Applies the options and closes the screen.
void OptionsWidget::Ok()
{
    m_game->applyOptions(m_options);
    getParentWidget()->getNode()->setVisible(false);
}

void OptionsWidget::RenderDistanceDecrease()
{
    m_options.renderDistance -= RenderDistanceStep;
    m_options.renderDistance =
        m_options.renderDistance < MinimumRenderDistance ? MinimumRenderDistance : m_options.renderDistance;
}

void OptionsWidget::RenderDistanceIncrease()
{
    m_options.renderDistance += RenderDistanceStep;
    m_options.renderDistance =
        m_options.renderDistance > MaximumRenderDistance ? MaximumRenderDistance : m_options.renderDistance;
}

// The arrows, in pixels: the left ones at x 240, the right ones 15 from the right edge, the rows
// 30 apart from y 12 (the labels' baselines at 27, as draw places them); the action buttons 20
// from the edges.
void OptionsWidget::resized()
{
    plasma::Widget *button;
    m_previousFullscreen->m_widget->moveTo(240.0f, 12.0f, true);
    button = m_nextFullscreen->m_widget;
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 12.0f, true);

    m_previousResolution->m_widget->moveTo(240.0f, 42.0f, true);
    button = m_nextResolution->m_widget;
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 42.0f, true);

    m_previousAntiAliasing->m_widget->moveTo(240.0f, 72.0f, true);
    button = m_nextAntiAliasing->m_widget;
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 72.0f, true);

    m_previousRenderDistance->m_widget->moveTo(240.0f, 102.0f, true);
    button = m_nextRenderDistance->m_widget;
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 102.0f, true);

    m_previousSoundVolume->m_widget->moveTo(240.0f, 132.0f, true);
    button = m_nextSoundVolume->m_widget;
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 132.0f, true);

    m_previousMusicVolume->m_widget->moveTo(240.0f, 162.0f, true);
    button = m_nextMusicVolume->m_widget;
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 162.0f, true);

    m_previousCameraSpeed->m_widget->moveTo(240.0f, 192.0f, true);
    button = m_nextCameraSpeed->m_widget;
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 192.0f, true);

    m_previousCameraSmoothness->m_widget->moveTo(240.0f, 222.0f, true);
    button = m_nextCameraSmoothness->m_widget;
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 222.0f, true);

    m_previousInvertY->m_widget->moveTo(240.0f, 252.0f, true);
    button = m_nextInvertY->m_widget;
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 252.0f, true);

    m_previousMinTimeStep->m_widget->moveTo(240.0f, 282.0f, true);
    button = m_nextMinTimeStep->m_widget;
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 282.0f, true);

    m_previousLanguage->m_widget->moveTo(240.0f, 312.0f, true);
    button = m_nextLanguage->m_widget;
    button->moveTo(viewWidth() - button->viewWidth() - 15.0f, 312.0f, true);

    button = m_applyButton->m_widget;
    button->moveTo(20.0f, getViewHeight() - button->getViewHeight() - 20.0f, true);
    button = m_okButton->m_widget;
    button->moveTo((viewWidth() - 40.0f - button->viewWidth()) * 0.5f + 20.0f,
                   getViewHeight() - button->getViewHeight() - 20.0f, true);
    plasma::Widget *ok = m_okButton->m_widget;
    button = m_cancelButton->m_widget;
    // The code positions Cancel using the OK button's height.
    button->moveTo(viewWidth() - 20.0f - button->viewWidth(), getViewHeight() - ok->getViewHeight() - 20.0f, true);
}

void OptionsWidget::ResolutionDecrease()
{
    --m_resolutionIndex;
    if (m_resolutionIndex < 0)
        m_resolutionIndex = 0;
    if (!m_resolutions.empty()) {
        m_options.resolutionX = m_resolutions[m_resolutionIndex % m_resolutions.size()][0];
        m_options.resolutionY = m_resolutions[m_resolutionIndex % m_resolutions.size()][1];
    }
}

void OptionsWidget::ResolutionIncrease()
{
    ++m_resolutionIndex;
    if (m_resolutionIndex >= static_cast<int>(m_resolutions.size()))
        m_resolutionIndex = (int)(m_resolutions.size() - 1);
    if (!m_resolutions.empty()) {
        m_options.resolutionX = m_resolutions[m_resolutionIndex % m_resolutions.size()][0];
        m_options.resolutionY = m_resolutions[m_resolutionIndex % m_resolutions.size()][1];
    }
}

void OptionsWidget::SoundVolumeDecrease()
{
    m_options.soundVolume -= VolumeStep;
    m_options.soundVolume = m_options.soundVolume < 0 ? 0 : m_options.soundVolume;
}

void OptionsWidget::SoundVolumeIncrease()
{
    m_options.soundVolume += VolumeStep;
    m_options.soundVolume = m_options.soundVolume > MaximumVolume ? MaximumVolume : m_options.soundVolume;
}

void OptionsWidget::setOptions(const Options &options)
{
    m_options = options;
    m_resolutionIndex = 0;
    for (int i = 0; i < static_cast<int>(m_resolutions.size()); ++i) {
        if (m_resolutions[i] == plasma::Vector<2, int>(options.resolutionX, options.resolutionY))
            m_resolutionIndex = i;
    }
    if (!m_resolutions.empty()) {
        m_options.resolutionX = m_resolutions[m_resolutionIndex % m_resolutions.size()][0];
        m_options.resolutionY = m_resolutions[m_resolutionIndex % m_resolutions.size()][1];
    }
}
}

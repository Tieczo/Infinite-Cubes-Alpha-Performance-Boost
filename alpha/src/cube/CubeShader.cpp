// CubeShader manages Direct3D 9 shaders and their constants.
#include <cstring>
#include "cube/CubeShader.h"
// Shader headers are generated from the HLSL sources listed in shaders.json.
#include "cube/shaders/WorldVertexShader.h"
#include "cube/shaders/WorldPixelShader.h"
#include "cube/shaders/SkyPixelShader.h"
#include "cube/shaders/WaterPixelShader.h"
#include "cube/shaders/UnusedPixelShader.h"
#include "cube/shaders/CloudPixelShader.h"

namespace cube {
using plasma::Matrix;
using plasma::Vector;

// A shader constant's four floats, built from a colour.
struct ShaderColor {
    float r, g, b, a;

    ShaderColor() {}
    ShaderColor(float r, float g, float b, float a)
        : r(r),
          g(g),
          b(b),
          a(a)
    {}
    operator const float *() const { return &r; }
};

CubeShader::CubeShader(IDirect3DDevice9 *device)
    : m_device(device)
{
    m_vertexShader = 0;
    m_pixelShader = 0;
    m_skyPixelShader = 0;
    m_cloudPixelShader = 0;
    m_waterPixelShader = 0;
    m_unusedPixelShader = 0;
    m_vertexDeclaration = 0;
}

CubeShader::~CubeShader()
{
    if (m_vertexShader)
        m_vertexShader->Release();
    if (m_pixelShader)
        m_pixelShader->Release();
    if (m_waterPixelShader)
        m_waterPixelShader->Release();
    if (m_skyPixelShader)
        m_skyPixelShader->Release();
    if (m_cloudPixelShader)
        m_cloudPixelShader->Release();
    if (m_unusedPixelShader)
        m_unusedPixelShader->Release();
    if (m_vertexDeclaration)
        m_vertexDeclaration->Release();
}

void CubeShader::apply()
{
    m_device->SetVertexDeclaration(m_vertexDeclaration);
    m_device->SetVertexShader(m_vertexShader);
    m_device->SetPixelShader(m_pixelShader);
}

void CubeShader::applyCloud()
{
    m_device->SetVertexDeclaration(m_vertexDeclaration);
    m_device->SetVertexShader(m_vertexShader);
    m_device->SetPixelShader(m_cloudPixelShader);
}

void CubeShader::applySky()
{
    m_device->SetVertexDeclaration(m_vertexDeclaration);
    m_device->SetVertexShader(m_vertexShader);
    m_device->SetPixelShader(m_skyPixelShader);
}

void CubeShader::applyWater()
{
    m_device->SetVertexDeclaration(m_vertexDeclaration);
    m_device->SetVertexShader(m_vertexShader);
    m_device->SetPixelShader(m_waterPixelShader);
}

bool CubeShader::initialize()
{
    // The vertex declaration is the chunks' (cube/Vertex.h: a UBYTE4 position whose fourth byte names
    // the face, then a D3DCOLOR); the registers are those the shaders' constant tables give their
    // variables.
    D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                    {0, 4, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
                                    D3DDECL_END()};
    if (m_device->CreateVertexDeclaration(elements, &m_vertexDeclaration) != D3D_OK)
        return false;
    if (m_device->CreateVertexShader(worldVertexShader, &m_vertexShader) != D3D_OK)
        return false;
    m_pointLightPositions = 0;
    m_skyColor1 = 0;
    m_worldViewProjMatrix = 20;
    m_worldViewMatrix = 24;
    m_worldMatrix = 27;
    m_materialColor = 32;
    m_darknessColor = 37;
    m_lightDirection = 33;
    m_lightFrontColor = 34;
    m_lightBackColor = 35;
    m_ambientColor = 38;
    m_white = 39;
    m_fogScale = 30;
    m_fogGradientTranslation = 31;
    m_pointLightColors = 10;
    m_cameraPosition = 36;
    m_skyColor2 = 1;
    m_fogColor = 2;
    m_shininess = 4;
    m_alpha = 3;
    if (m_device->CreatePixelShader(worldPixelShader, &m_pixelShader) != D3D_OK)
        return false;
    if (m_device->CreatePixelShader(skyPixelShader, &m_skyPixelShader) != D3D_OK)
        return false;
    if (m_device->CreatePixelShader(cloudPixelShader, &m_cloudPixelShader) != D3D_OK)
        return false;
    if (m_device->CreatePixelShader(waterPixelShader, &m_waterPixelShader) != D3D_OK)
        return false;
    if (m_device->CreatePixelShader(unusedPixelShader, &m_unusedPixelShader) != D3D_OK)
        return false;
    return true;
}

void CubeShader::setAlpha(float alpha)
{
    float value[] = {alpha, 0, 0, 0};
    m_device->SetPixelShaderConstantF(m_alpha, value, 1);
}

void CubeShader::setCameraPosition(const Vector<3, float> &position)
{
    float value[] = {position[0], position[1], position[2], 0};
    m_device->SetVertexShaderConstantF(m_cameraPosition, value, 1);
}

void CubeShader::setDarknessColor(const Vector<4, float> &color)
{
    m_device->SetVertexShaderConstantF(m_darknessColor, &color[0], 1);
}

void CubeShader::setFogGradientTranslation(float distance)
// The sky gradient moves with the camera's tilt: 0 at 80 degrees, 1 at 50.
{
    float value[] = {(distance - 80.0f) / -30.0f, 0, 0, 0};
    m_device->SetVertexShaderConstantF(m_fogGradientTranslation, value, 1);
}

void CubeShader::setFogScale(float distance)
{
    if (distance > 0) {
        float value[] = {1.0f / distance, 0, 0, 0};
        m_device->SetVertexShaderConstantF(m_fogScale, value, 1);
    }
}

void CubeShader::setLight(const Vector<4, float> &front, const Vector<4, float> &back,
                          const Vector<3, float> &direction, const Vector<4, float> &ambient)
{
    m_device->SetVertexShaderConstantF(m_lightFrontColor, &front[0], 1);
    m_device->SetVertexShaderConstantF(m_lightBackColor, &back[0], 1);
    m_device->SetVertexShaderConstantF(m_ambientColor, &ambient[0], 1);
    Vector<3, float> normalized = direction.normalized();
    float value[] = {normalized[0], normalized[1], normalized[2], 0};
    m_device->SetVertexShaderConstantF(m_lightDirection, value, 1);
}

void CubeShader::setMaterialColor(const Vector<4, float> &color)
{
    m_device->SetVertexShaderConstantF(m_materialColor, &color[0], 1);
}

void CubeShader::setMatrices(const Matrix<float> &world, const Matrix<float> &view, const Matrix<float> &projection)
{
    Matrix<float> matrix = world;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 3; ++row)
            matrix[column + row * 4] = world[row + column * 4];
    Matrix<float> worldView = view * world;
    worldView.transpose();
    Matrix<float> worldViewProjection = projection * view * world;
    worldViewProjection.transpose();
    // The three matrices fill consecutive registers (projected 4, world-view 3, world 3): one upload of the
    // same values instead of three (not in the original).
    if (m_worldViewMatrix == m_worldViewProjMatrix + 4 && m_worldMatrix == m_worldViewMatrix + 3) {
        float values[10 * 4];
        memcpy(values, &worldViewProjection[0], 16 * sizeof(float));
        memcpy(values + 16, &worldView[0], 12 * sizeof(float));
        memcpy(values + 28, &matrix[0], 12 * sizeof(float));
        m_device->SetVertexShaderConstantF(m_worldViewProjMatrix, values, 10);
        return;
    }
    m_device->SetVertexShaderConstantF(m_worldMatrix, &matrix[0], 3);
    m_device->SetVertexShaderConstantF(m_worldViewMatrix, &worldView[0], 3);
    m_device->SetVertexShaderConstantF(m_worldViewProjMatrix, &worldViewProjection[0], 4);
}

void CubeShader::setPointLights(const Vector<4, float> *positions, const Vector<3, float> *colors)
{
    enum {
        UploadedPointLights = 10
    };
    // Converts all sixteen source colors; the shader receives the first ten.
    ShaderColor values[PointLightSourceCount];
    for (int i = 0; i < PointLightSourceCount; ++i)
        values[i] = ShaderColor(colors[i][0], colors[i][1], colors[i][2], 0);
    // The colors follow the positions in the registers: one upload of the same values instead of two (not in
    // the original).
    if (m_pointLightColors == m_pointLightPositions + UploadedPointLights) {
        float both[2 * UploadedPointLights * 4];
        memcpy(both, &positions[0][0], UploadedPointLights * 4 * sizeof(float));
        memcpy(both + UploadedPointLights * 4, values[0], UploadedPointLights * 4 * sizeof(float));
        m_device->SetVertexShaderConstantF(m_pointLightPositions, both, 2 * UploadedPointLights);
        return;
    }
    m_device->SetVertexShaderConstantF(m_pointLightPositions, &positions[0][0], UploadedPointLights);
    m_device->SetVertexShaderConstantF(m_pointLightColors, values[0], UploadedPointLights);
}

void CubeShader::setShininess(float shininess)
{
    float value[] = {shininess, 0, 0, 0};
    m_device->SetPixelShaderConstantF(m_shininess, value, 1);
}

void CubeShader::setSky(const Vector<4, float> &skyGradientStartColor, const Vector<4, float> &skyGradientEndColor,
                        const Vector<4, float> &fog)
{
    m_device->SetPixelShaderConstantF(m_skyColor1, &skyGradientStartColor[0], 1);
    m_device->SetPixelShaderConstantF(m_skyColor2, &skyGradientEndColor[0], 1);
    m_device->SetPixelShaderConstantF(m_fogColor, &fog[0], 1);
}

void CubeShader::setWhite(float white)
{
    float value[] = {white, 0, 0, 0};
    m_device->SetVertexShaderConstantF(m_white, value, 1);
}
}

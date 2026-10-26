// ScalableFont rasterizes outline glyphs and caches them for text rendering.
#include "plasma/ScalableFont.h"
#include "plasma/Engine.h"
#include "plasma/Font.h"
#include "plasma/Texture.h"
#include "plasma/Node.h"
#include "plasma/Shape.h"
#include "plasma/Transformation.h"
#include <fstream>
#include FT_GLYPH_H
#include <algorithm>
#include <cmath>
#include <cctype>

#include "plasma/StringConversion.h"
#include "plasma/PLXStreamReader.h"

// Limit applied to the requested stroke radius.
float g_maxFontStroke = 500.0f;

namespace plasma {
// The glyph textures (anonymous: this file's own): glyphs at least this wide are filtered
// linearly; a raster glyph is at most this large (pixels).
namespace {
enum {
    LinearFilterMinimumWidth = 100
};
enum {
    MaximumRasterSize = 1000
};
}

static inline TextureFormat glyphTextureFormat(int filter)
{
    TextureFormat format;
    format.pixelFormat = TEXTURE_RGBA;
    format.minFilter = filter;
    format.maxFilter = filter;
    format.horizontalWrap = TEXTURE_WRAP_NONE;
    format.verticalWrap = TEXTURE_WRAP_NONE;
    return format;
}

ScalableFont::ScalableFont(Engine *engine)
    : m_engine(engine),
      m_glyphs(0),
      m_strokeGlyphs(0),
      m_face(0),
      m_stroker(0),
      m_width(0.0f),
      m_height(0.0f),
      m_spacing(0.0f),
      m_leading(0.0f),
      m_plasmaRoot(0),
      m_hinted(true)
{
    m_ftTransform.xx = toFreeTypeFixed(1.0f);
    m_ftTransform.xy = toFreeTypeFixed(0.0f);
    m_ftTransform.yx = m_ftTransform.xy;
    m_ftTransform.yy = toFreeTypeFixed(1.0f);
    m_transform.identity();
    InitializeCriticalSectionAndSpinCount(&m_lock, 0x80000400);
}

ScalableFont::~ScalableFont()
{
    clear();
    DeleteCriticalSection(&m_lock);
}

void ScalableFont::collectUnusedGlyphs()
{
    lock();
    std::list<ScalableSizeKey> emptySizes;
    for (ScalableSizeCache::iterator size = m_sizeCache.begin(); size != m_sizeCache.end(); ++size) {
        if (size->second) {
            std::list<unsigned short> unused;
            for (ScalableGlyphMap::iterator glyph = size->second->begin(); glyph != size->second->end(); ++glyph) {
                if (glyph->second) {
                    if (glyph->second->m_used) {
                        glyph->second->m_used = false;
                    } else {
                        if (glyph->second->m_texture)
                            m_engine->deleteTexture(glyph->second->m_texture);
                        delete glyph->second;
                        m_textLayouts.clear();
                        unused.push_back((unsigned short)glyph->first);
                    }
                }
            }
            for (std::list<unsigned short>::iterator character = unused.begin(); character != unused.end(); ++character)
                size->second->erase((int)*character);
            if (size->second->empty()) {
                if (m_glyphs == size->second)
                    m_glyphs = 0;
                if (m_strokeGlyphs == size->second)
                    m_strokeGlyphs = 0;
                delete size->second;
                emptySizes.push_back(size->first);
            }
        }
    }
    for (std::list<ScalableSizeKey>::iterator key = emptySizes.begin(); key != emptySizes.end(); ++key)
        m_sizeCache.erase(*key);
    std::list<ScalableTransformKey> emptyTransforms;
    for (ScalableTransformCache::iterator transform = m_transformCache.begin(); transform != m_transformCache.end();
         ++transform) {
        if (transform->second) {
            std::list<unsigned short> unused;
            for (ScalableGlyphMap::iterator glyph = transform->second->begin(); glyph != transform->second->end();
                 ++glyph) {
                if (glyph->second) {
                    if (glyph->second->m_used) {
                        glyph->second->m_used = false;
                    } else {
                        if (glyph->second->m_texture)
                            m_engine->deleteTexture(glyph->second->m_texture);
                        delete glyph->second;
                        m_textLayouts.clear();
                        unused.push_back((unsigned short)glyph->first);
                    }
                }
            }
            for (std::list<unsigned short>::iterator character = unused.begin(); character != unused.end(); ++character)
                transform->second->erase((int)*character);
            if (transform->second->empty()) {
                if (m_glyphs == transform->second)
                    m_glyphs = 0;
                if (m_strokeGlyphs == transform->second)
                    m_strokeGlyphs = 0;
                delete transform->second;
                emptyTransforms.push_back(transform->first);
            }
        }
    }
    for (std::list<ScalableTransformKey>::iterator key = emptyTransforms.begin(); key != emptyTransforms.end(); ++key)
        m_transformCache.erase(*key);
    unlock();
}

void ScalableFont::clear()
{
    lock();
    m_textLayouts.clear();
    if (m_face) {
        FT_Done_Face(m_face);
        m_face = 0;
    }
    if (m_stroker) {
        FT_Stroker_Done(m_stroker);
        m_stroker = 0;
    }
    for (ScalableSizeCache::iterator size = m_sizeCache.begin(); size != m_sizeCache.end(); ++size) {
        if (size->second) {
            for (ScalableGlyphMap::iterator glyph = size->second->begin(); glyph != size->second->end(); ++glyph) {
                if (glyph->second)
                    delete glyph->second;
            }
            delete size->second;
        }
    }
    // M_glyphs and m_strokeGlyphs keep pointing at the deleted maps, as in plasma.
    m_sizeCache.clear();
    for (ScalableTransformCache::iterator transform = m_transformCache.begin(); transform != m_transformCache.end();
         ++transform) {
        if (transform->second) {
            for (ScalableGlyphMap::iterator glyph = transform->second->begin(); glyph != transform->second->end();
                 ++glyph) {
                if (glyph->second)
                    delete glyph->second;
            }
            delete transform->second;
        }
    }
    m_transformCache.clear();
    for (ScalableGlyphMap::iterator glyph = m_plasmaGlyphs.begin(); glyph != m_plasmaGlyphs.end(); ++glyph) {
        if (glyph->second)
            delete glyph->second;
    }
    m_plasmaGlyphs.clear();
    if (m_plasmaRoot) {
        m_engine->deleteNode(m_plasmaRoot);
        m_plasmaRoot = 0;
    }
    unlock();
}

void ScalableFont::renderText(const std::wstring &text, float x, float y, float size, float stroke, float spacing,
                              float leading, float width, const float *fill, const float *outline,
                              const float *extrusion, int flags, bool hinted)
{
    lock();
    Matrix<float> previous;
    m_engine->getModelViewMatrix(previous);
    prepare(size, stroke, spacing, leading, previous, hinted);
    if (!m_plasmaRoot) {
        Matrix<float> matrix;
        if (hinted) {
            Vector<2, float> scale(size / m_width, size / m_height);
            width = width / scale[0];
            matrix = previous;
            matrix.scale(scale[0], scale[1]);
            matrix.m_elements[12] = (float)(int)(matrix.m_elements[12] + 0.5f);
            matrix.m_elements[13] = (float)(int)(matrix.m_elements[13] + 0.5f);
            m_engine->loadMatrix(matrix);
        } else {
            matrix.identity();
            matrix.translate((float)(int)(previous.m_elements[12] + 0.5f),
                             (float)(int)(previous.m_elements[13] + 0.5f));
            m_engine->loadMatrix(matrix);
        }
    }
    m_engine->matrixLoaded();
    if (flags & (1 << TEXT_WRAP)) {
        std::wstring wrapped = text;
        wrapText(wrapped, width);
        drawGlyphs(wrapped, x, y, fill, outline, extrusion, flags);
    } else {
        drawGlyphs(text, x, y, fill, outline, extrusion, flags);
    }
    m_engine->loadMatrix(previous);
    unlock();
}

void ScalableFont::drawGlyphs(const std::wstring &text, float x, float y, const float *fill, const float *outline,
                              const float *extrusion, int flags)
{
    // A text drawn before in the same font state is drawn again from its record (not in the original): the same
    // rectangles in the same order, without measuring the text again. Vector glyphs (plasma fonts) are not
    // recorded.
    std::string layoutKey;
    ScalableTextLayout *layout = 0;
    if (!m_plasmaRoot) {
        layoutKey.assign((const char *)text.data(), text.size() * sizeof(wchar_t));
        auto add = [&layoutKey](const void *data, size_t size) { layoutKey.append((const char *)data, size); };
        add(&x, sizeof x);
        add(&y, sizeof y);
        add(&flags, sizeof flags);
        add(fill, 4 * sizeof(float));
        add(outline, 4 * sizeof(float));
        add(&m_transform.m_elements[0], 16 * sizeof(float));
        add(&m_glyphs, sizeof m_glyphs);
        add(&m_strokeGlyphs, sizeof m_strokeGlyphs);
        add(&m_hinted, sizeof m_hinted);
        add(&m_width, sizeof m_width);
        add(&m_height, sizeof m_height);
        add(&m_stroke, sizeof m_stroke);
        add(&m_spacing, sizeof m_spacing);
        add(&m_leading, sizeof m_leading);
        std::unordered_map<std::string, ScalableTextLayout>::iterator found = m_textLayouts.find(layoutKey);
        if (found != m_textLayouts.end()) {
            m_engine->enableTexture2D();
            for (const ScalableTextRectangle &rectangle : found->second.rectangles) {
                rectangle.texture->bind(1);
                m_engine->drawTexturedRectangle(
                    rectangle.position, rectangle.size, Vector<2, float>(0.0f, 0.0f), Vector<2, float>(1.0f, 1.0f),
                    *reinterpret_cast<const Vector<4, float> *>(rectangle.outline ? outline : fill));
            }
            m_engine->disableTexture2D();
            for (ScalableGlyph *glyph : found->second.glyphs)
                glyph->m_used = true;
            return;
        }
        if (m_textLayouts.size() >= 2048)
            m_textLayouts.clear();
        layout = &m_textLayouts[layoutKey];
    }
    float scale = 1.0f;
    if (m_plasmaRoot)
        scale = m_height / (m_maximum.m_elements[1] - m_minimum.m_elements[1]);
    Vector<2, float> minimum, maximum, alignedMinimum;
    getBounds(text, minimum, maximum, TEXT_FLAGS_NONE, -1, true);
    getBounds(text, alignedMinimum, maximum, flags, -1, true);
    alignedMinimum = alignedMinimum - minimum;
    x = alignedMinimum.m_elements[0] + x;
    Vector<2, float> pen(x, alignedMinimum.m_elements[1] + y);
    int length = (int)text.size();
    m_engine->enableTexture2D();
    if (m_plasmaRoot) {
        m_engine->pushFillColor();
        m_engine->pushOutlineColor();
        m_engine->pushExtrusionColor();
        m_engine->setFillColor(fill);
        m_engine->setOutlineColor(outline);
        m_engine->setExtrusionColor(extrusion);
    }
    Vector<2, float> lineMinimum, lineMaximum;
    getBounds(text, lineMinimum, lineMaximum, flags, 0, true);
    if (flags & (1 << TEXT_CENTER))
        pen.m_elements[0] = pen.m_elements[0] - (lineMaximum.m_elements[0] - lineMinimum.m_elements[0] -
                                                 maximum.m_elements[0] + alignedMinimum.m_elements[0]) *
                                                    0.5f;
    else if (flags & (1 << TEXT_RIGHT))
        pen.m_elements[0] = pen.m_elements[0] - (lineMaximum.m_elements[0] - lineMinimum.m_elements[0] -
                                                 maximum.m_elements[0] + alignedMinimum.m_elements[0]);
    for (int i = 0; i < length; ++i) {
        if (text[i] == L'\n') {
            getBounds(text, lineMinimum, lineMaximum, TEXT_FLAGS_NONE, i + 1, true);
            pen.m_elements[0] = x;
            if (flags & (1 << TEXT_CENTER))
                pen.m_elements[0] = x - (lineMaximum.m_elements[0] - lineMinimum.m_elements[0] - maximum.m_elements[0] +
                                         alignedMinimum.m_elements[0]) *
                                            0.5f;
            else if (flags & (1 << TEXT_RIGHT))
                pen.m_elements[0] = x - (lineMaximum.m_elements[0] - lineMinimum.m_elements[0] - maximum.m_elements[0] +
                                         alignedMinimum.m_elements[0]);
            pen.m_elements[1] = m_leading + m_height + pen.m_elements[1];
            continue;
        }
        ScalableGlyph *glyph = getGlyph(text[i]);
        float advance = 0.0f;
        if (glyph) {
            if (glyph->m_texture) {
                glyph->m_texture->bind(1);
                // The font interface passes colours as float arrays (RGBA).
                const Vector<4, float> &color = *reinterpret_cast<const Vector<4, float> *>(fill);
                if (m_hinted) {
                    Vector<2, int> glyphPosition(glyph->m_position);
                    Vector<2, int> transformedPosition(m_transform.transform(pen));
                    Vector<2, float> position(transformedPosition + glyphPosition);
                    m_engine->drawTexturedRectangle(position, glyph->m_size, Vector<2, float>(0.0f, 0.0f),
                                                    Vector<2, float>(1.0f, 1.0f), color);
                    if (layout)
                        layout->rectangles.push_back({glyph->m_texture, position, glyph->m_size, false});
                } else {
                    Vector<2, int> pixelPosition(m_transform.transform(pen) + glyph->m_position);
                    Vector<2, float> position(pixelPosition);
                    m_engine->drawTexturedRectangle(position, glyph->m_size, Vector<2, float>(0.0f, 0.0f),
                                                    Vector<2, float>(1.0f, 1.0f), color);
                    if (layout)
                        layout->rectangles.push_back({glyph->m_texture, position, glyph->m_size, false});
                }
            } else if (glyph->m_node) {
                if (layout) {
                    m_textLayouts.erase(layoutKey);
                    layout = 0;
                }
                Matrix<float> matrix;
                m_engine->getModelViewMatrix(matrix);
                Matrix<float> previous = matrix;
                Vector<2, float> position = pen + glyph->m_position * scale;
                matrix.translate(position);
                matrix.scale(scale, scale);
                m_engine->loadMatrix(matrix);
                glyph->m_node->draw(NODE_RENDER_NONE);
                m_engine->loadMatrix(previous);
            }
            advance = glyph->m_advance.m_elements[0];
        }
        if (m_stroke > 0.0f) {
            ScalableGlyph *stroke = getStrokeGlyph(text[i]);
            if (stroke && stroke->m_texture) {
                stroke->m_texture->bind(1);
                const Vector<4, float> &color = *reinterpret_cast<const Vector<4, float> *>(outline);
                if (m_hinted) {
                    Vector<2, int> glyphPosition(stroke->m_position);
                    Vector<2, int> transformedPosition(m_transform * pen);
                    Vector<2, float> position(transformedPosition + glyphPosition);
                    m_engine->drawTexturedRectangle(position, stroke->m_size, Vector<2, float>(0.0f, 0.0f),
                                                    Vector<2, float>(1.0f, 1.0f), color);
                    if (layout)
                        layout->rectangles.push_back({stroke->m_texture, position, stroke->m_size, true});
                } else {
                    Vector<2, int> pixelPosition(m_transform.transform(pen) + stroke->m_position);
                    Vector<2, float> position(pixelPosition);
                    m_engine->drawTexturedRectangle(position, stroke->m_size, Vector<2, float>(0.0f, 0.0f),
                                                    Vector<2, float>(1.0f, 1.0f), color);
                    if (layout)
                        layout->rectangles.push_back({stroke->m_texture, position, stroke->m_size, true});
                }
            }
        }
        if (i < length - 1) {
            if (!m_plasmaRoot) {
                FT_Vector kerning;
                if (m_hinted) {
                    FT_Get_Kerning(m_face, FT_Get_Char_Index(m_face, text[i]), FT_Get_Char_Index(m_face, text[i + 1]),
                                   FT_KERNING_DEFAULT, &kerning);
                    advance += (float)(kerning.x >> 6);
                } else {
                    FT_Get_Kerning(m_face, FT_Get_Char_Index(m_face, text[i]), FT_Get_Char_Index(m_face, text[i + 1]),
                                   FT_KERNING_UNSCALED, &kerning);
                    advance += (float)kerning.x * m_width / (float)m_face->units_per_EM;
                }
            }
            pen.m_elements[0] = m_spacing + pen.m_elements[0];
        }
        if (m_hinted) {
            pen.m_elements[0] += advance;
            pen.m_elements[0] = (float)(int)pen.m_elements[0];
        } else {
            pen.m_elements[0] = advance * scale + pen.m_elements[0];
        }
    }
    m_engine->disableTexture2D();
    if (layout) {
        for (size_t i = 0; i < text.size(); ++i) {
            ScalableGlyphMap *maps[2] = {m_glyphs, m_stroke > 0.0f ? m_strokeGlyphs : 0};
            for (ScalableGlyphMap *map : maps) {
                if (!map)
                    continue;
                ScalableGlyphMap::iterator glyph = map->find(text[i]);
                if (glyph != map->end() && glyph->second)
                    layout->glyphs.push_back(glyph->second);
            }
        }
    }
    if (m_plasmaRoot) {
        m_engine->popFillColor();
        m_engine->popOutlineColor();
        m_engine->popExtrusionColor();
    }
}

void ScalableFont::getBounds(const std::wstring &text, Vector<2, float> &minimum, Vector<2, float> &maximum, int flags,
                             int lineStart, bool advancesOnly)
{
    int start = 0;
    int length = (int)text.size();
    if (lineStart >= 0) {
        start = lineStart;
        for (int i = lineStart; i <= length; ++i) {
            if (text[i] == L'\n') {
                length = i;
                break;
            }
        }
    }
    Vector<2, float> totalMinimum, totalMaximum;
    minimum = Vector<2, float>(0.0f, -m_height);
    maximum = Vector<2, float>(0.0f, 0.0f);
    Vector<2, float> pen(0.0f, 0.0f);
    float scale = 1.0f;
    if (m_plasmaRoot)
        scale = m_height / (m_maximum.m_elements[1] - m_minimum.m_elements[1]);
    bool firstGlyph = true;
    if (advancesOnly)
        ++length;
    for (int i = start; i < length; ++i) {
        ScalableGlyph *glyph = getGlyph(text[i]);
        if (m_stroke > 0.0f)
            glyph = getStrokeGlyph(text[i]);
        if (glyph && !advancesOnly && text[i] != L'\n' && text[i] != L'\r') {
            if (m_plasmaRoot) {
                if (firstGlyph) {
                    minimum = pen + (glyph->m_minimum + glyph->m_position) * scale;
                    maximum = pen + (glyph->m_maximum + glyph->m_position) * scale;
                    firstGlyph = false;
                } else {
                    minimum.m_elements[0] = (std::min)(
                        minimum.m_elements[0],
                        (glyph->m_minimum.m_elements[0] + glyph->m_position.m_elements[0]) * scale + pen.m_elements[0]);
                    minimum.m_elements[1] = (std::min)(
                        minimum.m_elements[1],
                        (glyph->m_minimum.m_elements[1] + glyph->m_position.m_elements[1]) * scale + pen.m_elements[1]);
                    maximum.m_elements[0] = (std::max)(
                        maximum.m_elements[0],
                        pen.m_elements[0] + (glyph->m_maximum.m_elements[0] + glyph->m_position.m_elements[0]) * scale);
                    maximum.m_elements[1] = (std::max)(
                        maximum.m_elements[1],
                        pen.m_elements[1] + (glyph->m_maximum.m_elements[1] + glyph->m_position.m_elements[1]) * scale);
                }
            } else {
                if (firstGlyph) {
                    minimum = pen + glyph->m_position + Vector<2, float>(1.0f, 1.0f);
                    maximum = pen + glyph->m_position + glyph->m_size - Vector<2, float>(1.0f, 1.0f);
                    firstGlyph = false;
                } else {
                    minimum.m_elements[0] =
                        (std::min)(minimum.m_elements[0], pen.m_elements[0] + glyph->m_position.m_elements[0] + 1.0f);
                    minimum.m_elements[1] =
                        (std::min)(minimum.m_elements[1], pen.m_elements[1] + glyph->m_position.m_elements[1] + 1.0f);
                    maximum.m_elements[0] =
                        (std::max)(maximum.m_elements[0], pen.m_elements[0] + glyph->m_position.m_elements[0] +
                                                              glyph->m_size.m_elements[0] - 1.0f);
                    maximum.m_elements[1] =
                        (std::max)(maximum.m_elements[1], pen.m_elements[1] + glyph->m_position.m_elements[1] +
                                                              glyph->m_size.m_elements[1] - 1.0f);
                }
            }
        } else if (advancesOnly) {
            maximum.m_elements[0] = (std::max)(maximum.m_elements[0], pen.m_elements[0]);
            maximum.m_elements[1] = (std::max)(maximum.m_elements[1], pen.m_elements[1]);
        }
        if (text[i] == L'\n') {
            pen.m_elements[0] = 0.0f;
            pen.m_elements[1] = m_leading + m_height + pen.m_elements[1];
        } else {
            if (!text[i])
                break;
            if (glyph)
                pen.m_elements[0] = scale * glyph->m_advance.m_elements[0] + pen.m_elements[0];
            if (i < length - 1 && text[i + 1] != L'\n') {
                if (!m_plasmaRoot) {
                    FT_Vector kerning;
                    if (m_hinted) {
                        FT_Get_Kerning(m_face, FT_Get_Char_Index(m_face, text[i]),
                                       FT_Get_Char_Index(m_face, text[i + 1]), FT_KERNING_DEFAULT, &kerning);
                        pen.m_elements[0] += (float)(kerning.x >> 6);
                    } else {
                        FT_Get_Kerning(m_face, FT_Get_Char_Index(m_face, text[i]),
                                       FT_Get_Char_Index(m_face, text[i + 1]), FT_KERNING_UNSCALED, &kerning);
                        pen.m_elements[0] += (float)kerning.x * m_width / (float)m_face->units_per_EM;
                    }
                }
                pen.m_elements[0] = m_spacing + pen.m_elements[0];
                if (m_hinted)
                    pen.m_elements[0] = (float)(int)pen.m_elements[0];
            }
        }
    }
    if (flags) {
        getBounds(text, totalMinimum, totalMaximum, TEXT_FLAGS_NONE, -1, advancesOnly);
        if (flags & (1 << TEXT_CENTER)) {
            float center = (totalMinimum[0] + totalMaximum[0]) * 0.5f;
            minimum[0] -= center;
            maximum[0] -= center;
        } else if (flags & (1 << TEXT_RIGHT)) {
            minimum[0] -= totalMaximum[0];
            maximum[0] -= totalMaximum[0];
        }
        if (flags & (1 << TEXT_MIDDLE)) {
            float center = (totalMinimum[1] + totalMaximum[1]) * 0.5f;
            minimum[1] -= center;
            maximum[1] -= center;
        } else if (flags & (1 << TEXT_BOTTOM)) {
            minimum[1] -= totalMaximum[1];
            maximum[1] -= totalMaximum[1];
        }
    }
}

void ScalableFont::getCaret(const std::wstring &text, int index, Vector<2, float> &position, Vector<2, float> &size,
                            int flags)
{
    float scale = 1.0f;
    if (m_plasmaRoot)
        scale = m_height / (m_maximum.m_elements[1] - m_minimum.m_elements[1]);
    if (index > (int)text.size())
        index = (int)text.size();
    position = Vector<2, float>(0.0f, -m_height);
    size = Vector<2, float>(0.0f, m_height);
    Vector<2, float> minimum, maximum;
    getBounds(text, minimum, maximum, TEXT_FLAGS_NONE, -1, false);
    {
        Vector<2, float> lineMinimum, lineMaximum;
        getBounds(text, lineMinimum, lineMaximum, flags, 0, false);
        if (flags & (1 << TEXT_CENTER))
            position.m_elements[0] = position.m_elements[0] - (lineMaximum.m_elements[0] - lineMinimum.m_elements[0] -
                                                               maximum.m_elements[0] + minimum.m_elements[0]) *
                                                                  0.5f;
        else if (flags & (1 << TEXT_RIGHT))
            position.m_elements[0] = position.m_elements[0] - (lineMaximum.m_elements[0] - lineMinimum.m_elements[0] -
                                                               maximum.m_elements[0] + minimum.m_elements[0]);
    }
    for (int i = 0; i <= index; ++i) {
        if (text[i] == L'\n' && i != index) {
            position.m_elements[0] = 0.0f;
            Vector<2, float> lineMinimum, lineMaximum;
            getBounds(text, lineMinimum, lineMaximum, flags, i + 1, false);
            if (flags & (1 << TEXT_CENTER))
                position.m_elements[0] =
                    position.m_elements[0] - (lineMaximum.m_elements[0] - lineMinimum.m_elements[0] -
                                              maximum.m_elements[0] + minimum.m_elements[0]) *
                                                 0.5f;
            else if (flags & (1 << TEXT_RIGHT))
                position.m_elements[0] =
                    position.m_elements[0] - (lineMaximum.m_elements[0] - lineMinimum.m_elements[0] -
                                              maximum.m_elements[0] + minimum.m_elements[0]);
            position.m_elements[1] = m_leading + m_height + position.m_elements[1];
        } else {
            if (!text[i])
                break;
            ScalableGlyph *glyph = getGlyph(text[i]);
            if (glyph) {
                float advance = glyph->m_advance[0];
                if (i == index)
                    size.m_elements[0] = advance * scale;
                else
                    position.m_elements[0] = advance * scale + position.m_elements[0];
            }
            if (i < index - 1) {
                if (!m_plasmaRoot) {
                    FT_Vector kerning;
                    if (m_hinted) {
                        FT_Get_Kerning(m_face, FT_Get_Char_Index(m_face, text[i]),
                                       FT_Get_Char_Index(m_face, text[i + 1]), FT_KERNING_DEFAULT, &kerning);
                        position.m_elements[0] += (float)(kerning.x >> 6);
                    } else {
                        FT_Get_Kerning(m_face, FT_Get_Char_Index(m_face, text[i]),
                                       FT_Get_Char_Index(m_face, text[i + 1]), FT_KERNING_UNSCALED, &kerning);
                        position.m_elements[0] += (float)kerning.x * m_width / (float)m_face->units_per_EM;
                    }
                }
                position[0] += m_spacing;
                if (m_hinted)
                    position[0] = (float)(int)position[0];
            }
        }
    }
    if (flags) {
        Vector<2, float> minimum, maximum;
        getBounds(text, minimum, maximum, TEXT_FLAGS_NONE, -1, false);
        if (flags & (1 << TEXT_CENTER)) {
            float center = (minimum.m_elements[0] + maximum.m_elements[0]) * 0.5f;
            position.m_elements[0] = position.m_elements[0] - center;
        } else if (flags & (1 << TEXT_RIGHT))
            position.m_elements[0] = position.m_elements[0] - maximum.m_elements[0];
        if (flags & (1 << TEXT_MIDDLE)) {
            float center = (minimum.m_elements[1] + maximum.m_elements[1]) * 0.5f;
            position.m_elements[1] = position.m_elements[1] - center;
        } else if (flags & (1 << TEXT_BOTTOM))
            position.m_elements[1] = position.m_elements[1] - maximum.m_elements[1];
    }
}

ScalableGlyph *ScalableFont::getBitmapGlyph(ScalableGlyphMap *glyphs, unsigned short character, bool stroke)
{
    if (!glyphs)
        return 0;
    ScalableGlyphMap::iterator found = glyphs->find(character);
    if (found != glyphs->end()) {
        found->second->m_used = true;
        return found->second;
    }
    if (FT_Set_Char_Size(m_face, (long)(64.0f * m_width + 0.5f), (long)(m_height * 64.0f + 0.5f), 0, 0))
        return 0;
    if (FT_Load_Char(m_face, character, FT_LOAD_NO_BITMAP))
        return 0;
    FT_Glyph glyph;
    FT_Get_Glyph(m_face->glyph, &glyph);
    if (stroke)
        FT_Glyph_Stroke(&glyph, m_stroker, 1);
    FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, 0, 1);
    FT_Bitmap pixels = ((FT_BitmapGlyph)glyph)->bitmap;
    int width = pixels.width + 2;
    int height = pixels.rows + 2;
    Texture *texture = 0;
    if (width > 0 && height > 0) {
        // White, with the glyph's coverage as alpha and a transparent one-pixel border.
        std::vector<unsigned char> rgba(4 * width * height, 0);
        for (int x = 0; x < width; ++x) {
            for (int y = 0; y < height; ++y) {
                rgba[4 * (y * width + x) + 0] = 255;
                rgba[4 * (y * width + x) + 1] = 255;
                rgba[4 * (y * width + x) + 2] = 255;
                if (x <= 0 || y <= 0 || x > pixels.width || y > pixels.rows)
                    rgba[4 * (y * width + x) + 3] = 0;
                else
                    rgba[4 * (y * width + x) + 3] = pixels.buffer[(y - 1) * pixels.pitch + x - 1];
            }
        }
        // Small glyphs are sampled unfiltered.
        int filter = TEXTURE_FILTER_NEAREST;
        if (width >= LinearFilterMinimumWidth)
            filter = TEXTURE_FILTER_LINEAR;
        texture = m_engine->createTexture(width, height, &rgba[0], glyphTextureFormat(filter),
                                          std::wstring(L"") + (wchar_t)character, true);
    }
    ScalableGlyph *result = new ScalableGlyph;
    (*glyphs)[character] = result;
    // The metric arithmetic can overflow signed int before the conversion to float.
    result->m_position = Vector<2, int>(((FT_BitmapGlyph)glyph)->left - 1, -1 - ((FT_BitmapGlyph)glyph)->top);
    result->m_size = Vector<2, int>(width, height);
    result->m_node = 0;
    result->m_advance =
        Vector<2, float>((float)glyph->advance.x * 0.0000152587890625f, 0.0000152587890625f * (float)glyph->advance.y);
    result->m_texture = texture;
    result->m_used = true;
    FT_Done_Glyph(glyph);
    return result;
}

void ScalableFont::measure(const std::wstring &text, const Matrix<float> &matrix, float size, float stroke,
                           float spacing, float leading, float width, Vector<2, float> &minimum,
                           Vector<2, float> &maximum, int flags, bool hinted, int lineStart, bool advancesOnly)
{
    lock();
    prepare(size, stroke, spacing, leading, matrix, hinted);
    Vector<2, float> scale(size / m_width, size / m_height);
    if (hinted)
        width = width / scale[0];
    if (flags & (1 << TEXT_WRAP)) {
        std::wstring wrapped = text;
        wrapText(wrapped, width);
        getBounds(wrapped, minimum, maximum, flags, lineStart, advancesOnly);
    } else {
        getBounds(text, minimum, maximum, flags, lineStart, advancesOnly);
    }
    minimum *= scale;
    maximum *= scale;
    unlock();
}

void ScalableFont::getCaret(const std::wstring &text, int index, const Matrix<float> &matrix, float size, float stroke,
                            float spacing, float leading, float width, Vector<2, float> &position,
                            Vector<2, float> &extent, int flags, bool hinted)
{
    lock();
    prepare(size, stroke, spacing, leading, matrix, hinted);
    Vector<2, float> scale(size / m_width, size / m_height);
    if (hinted)
        width = width / scale[0];
    if (flags & (1 << TEXT_WRAP)) {
        std::wstring wrapped = text;
        wrapText(wrapped, width);
        getCaret(wrapped, index, position, extent, flags);
    } else {
        getCaret(text, index, position, extent, flags);
    }
    position *= scale;
    extent *= scale;
    unlock();
}

ScalableGlyph *ScalableFont::getTransformedGlyph(ScalableGlyphMap *glyphs, unsigned short character, bool stroke)
{
    if (!glyphs)
        return 0;
    ScalableGlyphMap::iterator found = glyphs->find(character);
    if (found != glyphs->end()) {
        found->second->m_used = true;
        return found->second;
    }
    int error = FT_Set_Char_Size(m_face, (long)(64.0f * m_width + 0.5f), (long)(m_height * 64.0f + 0.5f), 0, 0);
    FT_Set_Transform(m_face, &m_ftTransform, 0);
    if (error)
        return 0;
    if (FT_Load_Char(m_face, character, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING))
        return 0;
    Vector<2, float> dimensions((float)m_face->glyph->metrics.width * 0.015625f,
                                0.015625f * (float)m_face->glyph->metrics.height);
    Matrix<float> matrix;
    matrix.identity();
    matrix.m_elements[0] = (float)m_ftTransform.xx * 0.0000152587890625f;
    matrix.m_elements[1] = (float)-m_ftTransform.yx * 0.0000152587890625f;
    matrix.m_elements[5] = (float)m_ftTransform.yy * 0.0000152587890625f;
    matrix.m_elements[4] = 0.0000152587890625f * (float)-m_ftTransform.xy;
    Vector<2, float> corners[3];
    corners[0] = matrix.transform(Vector<2, float>(dimensions[0], 0.0f));
    corners[1] = matrix.transform(dimensions);
    corners[2] = matrix.transform(Vector<2, float>(0.0f, dimensions[1]));
    Vector<2, float> minimum(0.0f, 0.0f);
    Vector<2, float> maximum(0.0f, 0.0f);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            if (corners[i][j] < minimum[j])
                minimum[j] = corners[i][j];
        }
        for (int j = 0; j < 2; ++j) {
            if (corners[i][j] > maximum[j])
                maximum[j] = corners[i][j];
        }
    }
    dimensions = maximum - minimum;
    float area = dimensions.m_elements[1] * dimensions.m_elements[0];
    area = std::sqrt(area);
    float scale = 500.0f / area;
    if (scale >= 1.0f) {
        scale = 1.0f;
    } else {
        // The glyph would be larger than 500 pixels square: raster it smaller and scale the quad up
        // instead.
        matrix.m_elements[0] *= scale;
        matrix.m_elements[1] *= scale;
        matrix.m_elements[4] *= scale;
        matrix.m_elements[5] *= scale;
        FT_Matrix transform;
        transform.xx = toFreeTypeFixed(matrix.m_elements[0]);
        long yx = toFreeTypeFixed(matrix.m_elements[1]);
        transform.yx = -yx;
        transform.yy = toFreeTypeFixed(matrix.m_elements[5]);
        long xy = toFreeTypeFixed(matrix.m_elements[4]);
        transform.xy = -xy;
        FT_Set_Transform(m_face, &transform, 0);
        if (FT_Load_Char(m_face, character, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING))
            return 0;
    }
    FT_Glyph glyph;
    FT_Get_Glyph(m_face->glyph, &glyph);
    if (stroke) {
        FT_Stroker_Set(m_stroker, (long)(m_stroke * 64.0f * scale + 0.5f), FT_STROKER_LINECAP_ROUND,
                       FT_STROKER_LINEJOIN_ROUND, 0);
        FT_Glyph_Stroke(&glyph, m_stroker, 1);
    }
    FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, 0, 1);
    FT_Bitmap pixels = ((FT_BitmapGlyph)glyph)->bitmap;
    int width = pixels.width + 2;
    int height = pixels.rows + 2;
    Texture *texture = 0;
    if (width > 0 && height > 0) {
        std::vector<unsigned char> rgba(4 * width * height, 0);
        for (int x = 0; x < width; ++x) {
            for (int y = 0; y < height; ++y) {
                rgba[4 * (y * width + x) + 0] = 255;
                rgba[4 * (y * width + x) + 1] = 255;
                rgba[4 * (y * width + x) + 2] = 255;
                if (x <= 0 || y <= 0 || x > pixels.width || y > pixels.rows)
                    rgba[4 * (y * width + x) + 3] = 0;
                else
                    rgba[4 * (y * width + x) + 3] = pixels.buffer[(y - 1) * pixels.pitch + x - 1];
            }
        }
        int filter = TEXTURE_FILTER_NEAREST;
        if (width >= LinearFilterMinimumWidth)
            filter = TEXTURE_FILTER_LINEAR;
        texture = m_engine->createTexture(width, height, &rgba[0], glyphTextureFormat(filter),
                                          std::wstring(L"") + (wchar_t)character, true);
    }
    ScalableGlyph *result = new ScalableGlyph;
    (*glyphs)[character] = result;
    Vector<2, float> position((float)(((FT_BitmapGlyph)glyph)->left - 1), (float)(-1 - ((FT_BitmapGlyph)glyph)->top));
    position.m_elements[0] = position.m_elements[0] / scale;
    position.m_elements[1] = position.m_elements[1] / scale;
    result->m_position = position;
    Vector<2, float> size((float)width, (float)height);
    size.m_elements[0] = size.m_elements[0] / scale;
    size.m_elements[1] = size.m_elements[1] / scale;
    result->m_size = size;
    result->m_node = 0;
    result->m_advance = Vector<2, float>((float)m_face->glyph->metrics.horiAdvance * 0.015625f, 0.0f);
    result->m_texture = texture;
    result->m_used = true;
    FT_Done_Glyph(glyph);
    return result;
}

bool ScalableFont::load(FT_Library library, const std::wstring &path,
                        std::map<std::wstring, std::vector<unsigned char>> *fontData)
{
    clear();
    m_path = path;
    int dot = (int)path.rfind(L'.');
    std::wstring extension = path.substr(dot + 1, path.size() - dot - 1);
    // Narrow toupper on wchar_t, as in plasma; values outside its domain have UB.
    std::transform(extension.begin(), extension.end(), extension.begin(), toupper);
    if (extension == L"TTF" || extension == L"DAT") {
        if (fontData) {
            std::map<std::wstring, std::vector<unsigned char>>::iterator data = fontData->find(path);
            if (data != fontData->end() && !data->second.empty())
                return loadTrueTypeFromMemory(library, path, &data->second[0], (long)data->second.size());
        }
        return loadTrueType(library, path);
    }
    return loadPlasma(path);
}

bool ScalableFont::loadPlasma(const std::wstring &path)
{
    lock();
    std::ifstream file;
    file.open(streamPath(path), std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        unlock();
        return false;
    }
    file.close();
    int slash = (int)path.rfind(L'/');
    int backslash = (int)path.rfind(L'\\');
    int separator = (std::max)(slash, backslash);
    std::wstring name = path.substr(separator + 1, path.size() - separator - 1);
    m_plasmaRoot = m_engine->createNode(0, 0, 0, 0, name);
    m_family = name;
    m_style = L"";
    {
        std::vector<unsigned __int64> keys(1, 0);
        m_engine->readPlasmaFileIntoNode(path, m_plasmaRoot, (1 << LOAD_SHARED) | (1 << LOAD_NO_WIDGET_INIT),
                                         static_cast<Engine::Page *>(0), keys);
    }
    m_plasmaRoot->m_flags |= (1 << NODE_INTERNAL);
    m_minimum = Vector<2, float>(0.0f, 0.0f);
    m_maximum = Vector<2, float>(0.0f, 0.0f);
    for (std::list<Node *>::iterator child = m_plasmaRoot->m_children.begin(); child != m_plasmaRoot->m_children.end();
         ++child) {
        if ((*child)->getName().size() == 1 && (*child)->m_shape) {
            std::wstring glyphName = (*child)->getName();
            if (m_plasmaGlyphs.find(glyphName[0]) != m_plasmaGlyphs.end())
                continue;
            Shape *shape = (*child)->m_shape;
            Transformation *transformation = (*child)->m_transformation;
            Vector<2, float> minimum = transformation->m_matrix * shape->getMinimum();
            shape = (*child)->m_shape;
            transformation = (*child)->m_transformation;
            Vector<2, float> maximum = transformation->m_matrix * shape->getMaximum();
            Vector<2, float> nodeMinimum = (*child)->getMinimum(BOUNDS_FLAGS_NONE);
            Vector<2, float> nodeMaximum = (*child)->getMaximum(BOUNDS_FLAGS_NONE);
            if (!(*child)->isHitTestable())
                (*child)->setShape(0);
            ScalableGlyph *glyph = new ScalableGlyph;
            m_plasmaGlyphs[glyphName[0]] = glyph;
            glyph->m_advance = maximum - minimum;
            glyph->m_size = maximum - minimum;
            glyph->m_position = Vector<2, float>(minimum.m_elements[0], maximum.m_elements[1]) * -1.0f;
            glyph->m_node = *child;
            glyph->m_minimum = nodeMinimum;
            glyph->m_maximum = nodeMaximum;
            glyph->m_texture = 0;
            glyph->m_used = true;
            // The font bounds are replaced by each glyph's, not united, as in plasma.
            m_minimum = glyph->m_position;
            m_maximum = glyph->m_position + glyph->m_size;
        }
    }
    unlock();
    return true;
}

bool ScalableFont::loadTrueType(FT_Library library, const std::wstring &path)
{
    lock();
    int error = FT_New_Face(library, narrowString(path).c_str(), 0, &m_face);
    if (error) {
        unlock();
        return false;
    }
    m_family = widenString(std::string(m_face->family_name));
    m_style = widenString(std::string(m_face->style_name));
    const char *postscript = FT_Get_Postscript_Name(m_face);
    if (postscript)
        m_postscriptName = widenString(postscript);
    m_minimum = Vector<2, float>((float)m_face->bbox.xMin, (float)m_face->bbox.yMin) / (float)m_face->units_per_EM;
    m_maximum = Vector<2, float>((float)m_face->bbox.xMax, (float)m_face->bbox.yMax) / (float)m_face->units_per_EM;
    FT_Stroker_New(library, &m_stroker);
    unlock();
    return true;
}

bool ScalableFont::loadTrueTypeFromMemory(FT_Library library, const std::wstring &path, const unsigned char *data,
                                          long size)
{
    lock();
    int error = FT_New_Memory_Face(library, data, size, 0, &m_face);
    if (error) {
        unlock();
        return false;
    }
    m_family = widenString(std::string(m_face->family_name));
    m_style = widenString(std::string(m_face->style_name));
    const char *postscript = FT_Get_Postscript_Name(m_face);
    if (postscript)
        m_postscriptName = widenString(postscript);
    m_minimum = Vector<2, float>((float)m_face->bbox.xMin, (float)m_face->bbox.yMin) / (float)m_face->units_per_EM;
    m_maximum = Vector<2, float>((float)m_face->bbox.xMax, (float)m_face->bbox.yMax) / (float)m_face->units_per_EM;
    FT_Stroker_New(library, &m_stroker);
    unlock();
    return true;
}

void ScalableFont::setSize(float size, float stroke)
{
    setSize(size, size, stroke);
}

void ScalableFont::setSize(float width, float height, float stroke)
{
    if (height <= 0.0f)
        height = width;
    if (width <= 0.0f)
        width = height;
    if (width <= 0.0f)
        return;
    // A NaN stroke passes: only a negative one is rejected.
    if (stroke < 0.0f)
        return;
    m_width = width;
    m_height = height;
    m_stroke = stroke;
    FT_Set_Char_Size(m_face, (long)(width * 64.0f + 0.5f), (long)(height * 64.0f + 0.5f), 0, 0);
    FT_Set_Transform(m_face, 0, 0);
    m_transform.identity();
    m_hinted = true;
    ScalableSizeCache::iterator found = m_sizeCache.find(
        std::make_pair(std::make_pair((int)(m_width * 64.0f + 0.5f), (int)(m_height * 64.0f + 0.5f)), 0));
    if (found != m_sizeCache.end()) {
        m_glyphs = found->second;
    } else {
        m_glyphs = new ScalableGlyphMap;
        m_sizeCache[ScalableSizeKey(std::make_pair((int)(m_width * 64.0f + 0.5f), (int)(m_height * 64.0f + 0.5f)), 0)] =
            m_glyphs;
    }
    if (stroke > 0.0f) {
        found = m_sizeCache.find(
            std::make_pair(std::make_pair((int)(m_width * 64.0f + 0.5f), (int)(m_height * 64.0f + 0.5f)),
                           (int)(stroke * 64.0f + 0.5f)));
        if (found != m_sizeCache.end()) {
            m_strokeGlyphs = found->second;
        } else {
            m_strokeGlyphs = new ScalableGlyphMap;
            m_sizeCache[ScalableSizeKey(std::make_pair((int)(m_width * 64.0f + 0.5f), (int)(m_height * 64.0f + 0.5f)),
                                        (int)(stroke * 64.0f + 0.5f))] = m_strokeGlyphs;
        }
        FT_Stroker_Set(m_stroker, (long)(m_stroke * 64.0f + 0.5f), FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND,
                       0);
    } else {
        m_strokeGlyphs = 0;
    }
}

void ScalableFont::prepare(float size, float stroke, float spacing, float leading, const Matrix<float> &matrix,
                           bool hinted)
{
    if (m_plasmaRoot) {
        m_width = size;
        m_height = size;
        m_stroke = stroke;
        m_hinted = false;
        m_spacing = spacing;
        m_leading = leading;
    } else if (hinted) {
        Vector<2, float> extent;
        float rasterStroke, rasterSpacing, rasterLeading;
        calculateSize(matrix, size, stroke, spacing, leading, extent, rasterStroke, rasterSpacing, rasterLeading);
        setSize(extent.m_elements[0], extent.m_elements[1], rasterStroke);
        m_spacing = rasterSpacing;
        m_leading = rasterLeading;
    } else {
        setTransform(size, stroke, matrix);
        m_spacing = spacing;
        m_leading = leading;
    }
}

void ScalableFont::setTransform(float size, float stroke, const Matrix<float> &matrix)
{
    m_transform.m_elements[0] = matrix[0];
    m_transform.m_elements[1] = matrix[1];
    m_transform.m_elements[5] = matrix[5];
    m_transform.m_elements[4] = matrix[4];
    m_ftTransform.xx = toFreeTypeFixed(matrix[0]);
    // Negating LONG_MIN (an exceptional input) is signed-overflow UB, as in plasma.
    long yx = toFreeTypeFixed(matrix[1]);
    m_ftTransform.yx = -yx;
    m_ftTransform.yy = toFreeTypeFixed(matrix[5]);
    long xy = toFreeTypeFixed(matrix[4]);
    m_ftTransform.xy = -xy;
    // A NaN size enters this path: an unordered comparison is not <= zero.
    if (!(size <= 0.0f)) {
        m_hinted = false;
        m_width = size;
        m_height = size;
        Vector<2, float> x = matrix.transformDirection(Vector<2, float>(1.0f, 0.0f));
        Vector<2, float> y = matrix.transformDirection(Vector<2, float>(0.0f, 1.0f));
        float yLength = std::sqrt(y[0] * y[0] + y[1] * y[1]);
        float xLength = std::sqrt(x[0] * x[0] + x[1] * x[1]);
        m_stroke = (std::min)(xLength, yLength) * stroke;
        FT_Set_Char_Size(m_face, (long)(size * 64.0f + 0.5f), (long)(size * 64.0f + 0.5f), 0, 0);
        FT_Set_Transform(m_face, &m_ftTransform, 0);
        std::vector<long> transform(4);
        transform[0] = m_ftTransform.xx;
        transform[1] = m_ftTransform.xy;
        transform[2] = m_ftTransform.yx;
        transform[3] = m_ftTransform.yy;
        int sizeKey = (int)(size * 64.0f + 0.5f);
        ScalableTransformCache::iterator found =
            m_transformCache.find(std::make_pair(std::make_pair(sizeKey, 0), transform));
        if (found != m_transformCache.end()) {
            m_glyphs = found->second;
        } else {
            m_glyphs = new ScalableGlyphMap;
            m_transformCache[std::make_pair(std::make_pair(sizeKey, 0), transform)] = m_glyphs;
        }
        if (stroke > 0.0f) {
            int strokeKey = (int)(stroke * 64.0f + 0.5f);
            found = m_transformCache.find(std::make_pair(std::make_pair(sizeKey, strokeKey), transform));
            if (found != m_transformCache.end()) {
                m_strokeGlyphs = found->second;
            } else {
                m_strokeGlyphs = new ScalableGlyphMap;
                m_transformCache[std::make_pair(std::make_pair(sizeKey, strokeKey), transform)] = m_strokeGlyphs;
            }
            FT_Stroker_Set(m_stroker, (long)(m_stroke * 64.0f + 0.5f), FT_STROKER_LINECAP_ROUND,
                           FT_STROKER_LINEJOIN_ROUND, 0);
        } else {
            m_strokeGlyphs = 0;
        }
    }
}

void ScalableFont::calculateSize(const Matrix<float> &matrix, float requestedSize, float requestedStroke,
                                 float requestedSpacing, float requestedLeading, Vector<2, float> &size, float &stroke,
                                 float &spacing, float &leading)
{
    if (m_plasmaRoot) {
        size = Vector<2, float>(requestedSize, requestedSize);
        stroke = requestedStroke;
        spacing = requestedSpacing;
        leading = requestedLeading;
    } else {
        Vector<2, float> x = matrix.transformDirection(Vector<2, float>(1.0f, 0.0f));
        Vector<2, float> y = matrix.transformDirection(Vector<2, float>(0.0f, 1.0f));
        Vector<2, float> lengths;
        lengths[0] = std::sqrt(x[0] * x[0] + x[1] * x[1]);
        lengths[1] = std::sqrt(y[0] * y[0] + y[1] * y[1]);
        size = lengths * requestedSize;
        size[0] = (int)(size[0] * 64.0f + 0.5f) * 0.015625f;
        size[1] = (int)(size[1] * 64.0f + 0.5f) * 0.015625f;
        if (size[0] > MaximumRasterSize)
            size[0] = MaximumRasterSize;
        if (size[1] > MaximumRasterSize)
            size[1] = MaximumRasterSize;
        Vector<2, float> scale(size[0] / requestedSize, size[1] / requestedSize);
        stroke = (scale[0] + scale[1]) * 0.5f * requestedStroke;
        spacing = scale[0] * requestedSpacing;
        leading = scale[1] * requestedLeading;
        if (size[0] < 1.0f)
            size[0] = 1.0f;
        if (size[1] < 1.0f)
            size[1] = 1.0f;
        if (stroke > g_maxFontStroke)
            stroke = g_maxFontStroke;
    }
}

void ScalableFont::wrapText(std::wstring &text, float width)
{
    float advance = 0.0f;
    int length = (int)text.size();
    float lastBreakAdvance = 0.0f;
    int lastBreak = -1;
    float scale = 1.0f;
    if (m_plasmaRoot)
        scale = m_height / (m_maximum.m_elements[1] - m_minimum.m_elements[1]);
    for (int i = 0; i < length; ++i) {
        if (text[i] == L' ' || text[i] == L'\n' || text[i] == L'\r' || i == length - 1) {
            if (advance >= width) {
                if (lastBreak >= 0)
                    text[lastBreak] = L'\n';
                advance = advance - lastBreakAdvance;
            }
            lastBreak = i;
            lastBreakAdvance = advance;
        }
        if (text[i] == L'\n' || text[i] == L'\r') {
            advance = 0.0f;
            lastBreakAdvance = 0.0f;
        } else {
            ScalableGlyph *glyph = getGlyph(text[i]);
            float glyphAdvance = 0.0f;
            if (glyph)
                glyphAdvance = glyph->m_advance.m_elements[0];
            if (i < length - 1) {
                if (!m_plasmaRoot) {
                    FT_Vector kerning;
                    if (m_hinted) {
                        FT_Get_Kerning(m_face, FT_Get_Char_Index(m_face, text[i]),
                                       FT_Get_Char_Index(m_face, text[i + 1]), FT_KERNING_DEFAULT, &kerning);
                        glyphAdvance += (float)(kerning.x >> 6);
                    } else {
                        FT_Get_Kerning(m_face, FT_Get_Char_Index(m_face, text[i]),
                                       FT_Get_Char_Index(m_face, text[i + 1]), FT_KERNING_UNSCALED, &kerning);
                        glyphAdvance += (float)kerning.x * m_width / (float)m_face->units_per_EM;
                    }
                }
                advance = m_spacing + advance;
            }
            if (m_hinted)
                advance = (float)(int)(float)(glyphAdvance + advance);
            else
                advance = glyphAdvance * scale + advance;
        }
    }
}

}

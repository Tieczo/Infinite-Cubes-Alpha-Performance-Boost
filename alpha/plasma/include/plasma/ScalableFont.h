// ScalableFont rasterizes outline glyphs for rendering.
#pragma once
#include <windows.h>
#include <map>
#include <string>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>
#include "plasma/Matrix.h"
#include "plasma/Vector.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H

namespace plasma {

class Engine;
class Node;
class Texture;

// Rasterized glyph record.
struct ScalableGlyph {
    Vector<2, float> m_position;
    Vector<2, float> m_size;
    Vector<2, float> m_advance;
    Vector<2, float> m_minimum;
    Vector<2, float> m_maximum;
    Texture *m_texture;
    Node *m_node;
    bool m_used;
};

typedef std::map<int, ScalableGlyph *> ScalableGlyphMap;

// One glyph rectangle drawGlyphs drew: its texture, place and size, and whether in the outline colour.
struct ScalableTextRectangle {
    Texture *texture;
    Vector<2, float> position;
    Vector<2, float> size;
    bool outline;
};
// What drawGlyphs did for one text in one font state (not in the original): the rectangles in drawing order
// and every glyph it looked up (they are marked used again when the text is drawn from this record).
struct ScalableTextLayout {
    std::vector<ScalableTextRectangle> rectangles;
    std::vector<ScalableGlyph *> glyphs;
};
typedef std::pair<std::pair<int, int>, int> ScalableSizeKey;
typedef std::pair<std::pair<int, int>, std::vector<long>> ScalableTransformKey;
typedef std::map<ScalableSizeKey, ScalableGlyphMap *> ScalableSizeCache;
typedef std::map<ScalableTransformKey, ScalableGlyphMap *> ScalableTransformCache;

class ScalableFont {
public:
    explicit ScalableFont(Engine *engine);
    virtual ~ScalableFont();

    void lock() { EnterCriticalSection(&m_lock); }
    void collectUnusedGlyphs();
    void unlock() { LeaveCriticalSection(&m_lock); }

    void clear();
    bool load(FT_Library library, const std::wstring &path,
              std::map<std::wstring, std::vector<unsigned char>> *fontData);
    bool loadTrueType(FT_Library library, const std::wstring &path);
    bool loadTrueTypeFromMemory(FT_Library library, const std::wstring &path, const unsigned char *data, long size);
    bool loadPlasma(const std::wstring &path);
    void setSize(float size, float stroke);
    void setSize(float width, float height, float stroke);
    ScalableGlyph *getGlyph(unsigned short character)
    {
        if (m_plasmaRoot)
            return getPlasmaGlyph(character);
        if (m_hinted)
            return getBitmapGlyph(m_glyphs, character, false);
        return getTransformedGlyph(m_glyphs, character, false);
    }
    ScalableGlyph *getStrokeGlyph(unsigned short character)
    {
        if (m_hinted)
            return getBitmapGlyph(m_strokeGlyphs, character, true);
        return getTransformedGlyph(m_strokeGlyphs, character, true);
    }

    ScalableGlyph *getPlasmaGlyph(unsigned short character)
    {
        int key = character;
        ScalableGlyphMap::iterator glyph = m_plasmaGlyphs.find(key);
        if (glyph != m_plasmaGlyphs.end()) {
            glyph->second->m_used = true;
            return glyph->second;
        }
        return nullptr;
    }
    void setTransform(float size, float stroke, const Matrix<float> &matrix);
    ScalableGlyph *getBitmapGlyph(ScalableGlyphMap *glyphs, unsigned short character, bool stroke);
    ScalableGlyph *getTransformedGlyph(ScalableGlyphMap *glyphs, unsigned short character, bool stroke);
    void wrapText(std::wstring &text, float width);
    void calculateSize(const Matrix<float> &matrix, float requestedSize, float requestedStroke, float requestedSpacing,
                       float requestedLeading, Vector<2, float> &size, float &stroke, float &spacing, float &leading);
    void prepare(float size, float stroke, float spacing, float leading, const Matrix<float> &matrix, bool hinted);
    void renderText(const std::wstring &text, float x, float y, float size, float stroke, float spacing, float leading,
                    float width, const float *fill, const float *outline, const float *extrusion, int flags,
                    bool hinted);
    void drawGlyphs(const std::wstring &text, float x, float y, const float *fill, const float *outline,
                    const float *extrusion, int flags);
    void getCaret(const std::wstring &text, int index, Vector<2, float> &position, Vector<2, float> &size, int flags);
    void getCaret(const std::wstring &text, int index, const Matrix<float> &matrix, float size, float stroke,
                  float spacing, float leading, float width, Vector<2, float> &position, Vector<2, float> &extent,
                  int flags, bool hinted);
    void measure(const std::wstring &text, const Matrix<float> &matrix, float size, float stroke, float spacing,
                 float leading, float width, Vector<2, float> &minimum, Vector<2, float> &maximum, int flags,
                 bool hinted, int lineStart, bool advancesOnly);
    void getBounds(const std::wstring &text, Vector<2, float> &minimum, Vector<2, float> &maximum, int flags,
                   int lineStart, bool advancesOnly);

    static long toFreeTypeFixed(float value) { return (long)(value * 65536.0f); }

public:
    Engine *m_engine;
    std::wstring m_path;
    std::wstring m_style;
    std::wstring m_family;
    std::wstring m_postscriptName;
    ScalableSizeCache m_sizeCache;
    ScalableTransformCache m_transformCache;
    FT_Matrix m_ftTransform;
    Matrix<float> m_transform;
    ScalableGlyphMap *m_glyphs;
    ScalableGlyphMap *m_strokeGlyphs;
    ScalableGlyphMap m_plasmaGlyphs;
    FT_Face m_face;
    FT_Stroker m_stroker;
    float m_width;
    float m_height;
    float m_stroke;
    float m_spacing;
    float m_leading;
    Node *m_plasmaRoot;
    Vector<2, float> m_minimum;
    Vector<2, float> m_maximum;
    bool m_hinted;
    // drawGlyphs' records, keyed by the text and everything else it reads (not in the original); emptied whenever
    // glyphs are deleted.
    std::unordered_map<std::string, ScalableTextLayout> m_textLayouts;
    CRITICAL_SECTION m_lock;
};

}

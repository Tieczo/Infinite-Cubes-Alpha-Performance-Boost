// Engine implements scene ownership, font import and PLA/PLX loading.
#include "plasma/Engine.h"
#include "plasma/StringConversion.h"
#include <algorithm>
#include "plasma/FontEngine.h"
#include "plasma/Node.h"
#include "plasma/Widget.h"
#include "plasma/Button.h"
#include "plasma/ListWidget.h"
#include "plasma/ScrollSlider.h"
#include "plasma/GenericShape.h"
#include "plasma/TextShape.h"
#include "plasma/Edit.h"
#include "plasma/PopUpButton.h"
#include "plasma/ScrollButton.h"
#include "plasma/MeshShape.h"
#include "plasma/SmoothMeshShape.h"
#include "plasma/CurveShape.h"
#include "plasma/PlasmaFont.h"
#include "plasma/PixelFont.h"
#include "plasma/Movie.h"
#include "plasma/Shape.h"
#include "plasma/Texture.h"
#include "plasma/Display.h"
#include "plasma/Transformation.h"
#include "plasma/RenderSurface.h"
#include "plasma/Drawing.h"
#include "plasma/FileIds.h"
#include "plasma/StringConversion.h"
#include "plasma/PLXStreamReader.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>

// FreeType (freetype/freetype.h), linked statically.
#include <ft2build.h>
#include FT_FREETYPE_H

#include <FreeImage.h>

namespace plasma {
// The engine's defaults and the font import's values (anonymous: this file's own): the page
// (width, height, dots per inch), the viewport, the size at which FreeType renders the imported
// glyphs, their outlines' stroke width and the smallest outline step kept (squared, in pixels).
namespace {
enum {
    DefaultPageWidth = 1000
};
enum {
    DefaultPageHeight = 1000
};
enum {
    DefaultPageResolution = 72
};
enum {
    DefaultViewportWidth = 1000
};
enum {
    DefaultViewportHeight = 800
};
enum {
    ImportedGlyphPixelSize = 100
};
enum {
    ImportedStrokeWidth = 4
};
}

// PLA writer chunk positions retained as unused state.
static std::list<__int64> g_plaChunkPositions;

// The legacy PLA format: nested chunks of a 32-bit identifier, a 32-bit length and the payload.
// Every reader below takes the stream right after a chunk's identifier.

// A plain value. A short read leaves it uninitialised, as in plasma.
template<typename T> inline T readValue(std::istream &stream)
{
    T value;
    stream.read(reinterpret_cast<char *>(&value), sizeof(T));
    return value;
}

std::streampos enterChunk(std::istream &stream);
void skipChunk(std::istream &stream);
std::string readString(std::istream &stream);
std::wstring readWString(std::istream &stream);
void readAttributeSequence(std::istream &stream, Attribute *attribute, std::map<int, std::string> &names);

// The frames (1), the array length (2) and the sequences (3) of an array attribute.
template<typename T>
void readArrayAttribute(std::istream &stream, ContinuousArrayAttribute<T> *attribute, std::map<int, std::string> &names)
{
    std::streampos end = enterChunk(stream);
    int index = 0;
    while (stream.tellg() != end && !stream.eof()) {
        switch (readValue<int>(stream)) {
        case AttributeChunk_Frame:
            enterChunk(stream);
            if (index == attribute->size())
                attribute->duplicateElement();
            stream.read(reinterpret_cast<char *>(attribute->m_values[index]->data()), attribute->length() * sizeof(T));
            index++;
            break;
        case AttributeChunk_ArrayLength:
            enterChunk(stream);
            attribute->resize(readValue<int>(stream));
            break;
        case AttributeChunk_Sequence:
            readAttributeSequence(stream, attribute, names);
            break;
        default:
            skipChunk(stream);
            break;
        }
    }
}

// The frames (1) and sequences (3) of an attribute with plain elements.
template<typename A> void readAttribute(std::istream &stream, A *attribute, std::map<int, std::string> &names)
{
    std::streampos end = enterChunk(stream);
    int index = 0;
    while (stream.tellg() != end && !stream.eof()) {
        switch (readValue<int>(stream)) {
        case AttributeChunk_Frame:
            enterChunk(stream);
            if (index == attribute->size())
                attribute->duplicateElement();
            stream.read(reinterpret_cast<char *>(&attribute->m_values[index]), sizeof(attribute->m_values[index]));
            ++index;
            break;
        case AttributeChunk_Sequence:
            readAttributeSequence(stream, attribute, names);
            break;
        default:
            skipChunk(stream);
            break;
        }
    }
}

Engine::Engine()
    : m_page(DefaultPageWidth, DefaultPageHeight, DefaultPageResolution),
      m_blurSurface(0),
      m_surfaceDepth(-1),
      m_filterSurface(0),
      m_overlayRoot(0),
      m_pointerNode(0),
      m_pointerCapture(0),
      m_focusWidget(0),
      m_popUpWidget(0),
      m_pointer(0.0f, 0.0f),
      m_previousPointer(0.0f, 0.0f),
      m_frameTime(0),
      m_caretClock(0),
      m_buttons(0),
      m_pressedPopUpItem(0),
      m_viewportX(0),
      m_viewportY(0),
      m_viewportWidth(DefaultViewportWidth),
      m_viewportHeight(DefaultViewportHeight),
      m_windowWidth(0),
      m_windowHeight(0),
      m_fillColor(1.0f, 1.0f, 1.0f, 1.0f),
      m_outlineColor(1.0f, 1.0f, 1.0f, 1.0f),
      m_extrusionColor(1.0f, 1.0f, 1.0f, 1.0f),
      m_renderFlags((1 << RENDER_ANTIALIAS) | (1 << RENDER_CLIP_WIDGET_SURFACES) | (1 << RENDER_BLUR)),
      m_unused190(true)
{
    // Preallocate the critical-section event and spin 1024 times.
    InitializeCriticalSectionAndSpinCount(&m_lock, 0x80000400);
    m_fontEngine = new FontEngine(this);
    FT_Init_FreeType(&m_freeType);
    m_root = createNode(0, 0, 0, 0, L"root");
    m_sources = createNode(0, 0, 0, 0, L"sources");
    m_sources->m_flags |= (1 << NODE_INTERNAL);
    m_fonts = createNode(0, 0, 0, 0, L"fonts");
    m_fonts->m_flags |= (1 << NODE_INTERNAL);
}

Engine::~Engine()
{
    delete m_fontEngine;
    FT_Done_FreeType(m_freeType);
    for (int i = 0; i < (int)m_renderSurfaces.size(); i++)
        delete m_renderSurfaces[i];
    delete m_blurSurface;
    DeleteCriticalSection(&m_lock);
}

ObjectManager::~ObjectManager()
{
    m_destroying = true;
    for (std::list<Object *>::iterator it = m_objects.begin(); it != m_objects.end(); ++it)
        delete *it;
}

std::list<Display *>::iterator Engine::addDisplay(Display *display)
{
    lock();
    m_displays.push_back(display);
    std::list<Display *>::iterator entry = --m_displays.end();
    unlock();
    return entry;
}

std::list<Node *>::iterator Engine::addNode(Node *node)
{
    lock();
    m_nodes.push_back(node);
    std::list<Node *>::iterator entry = --m_nodes.end();
    unlock();
    return entry;
}

std::list<Object *>::iterator Engine::addObject(Object *object)
{
    lock();
    m_objects.push_back(object);
    std::list<Object *>::iterator entry = --m_objects.end();
    unlock();
    return entry;
}

std::list<Shape *>::iterator Engine::addShape(Shape *shape)
{
    lock();
    m_shapes.push_back(shape);
    std::list<Shape *>::iterator entry = --m_shapes.end();
    unlock();
    return entry;
}

std::list<Texture *>::iterator Engine::addTexture(Texture *texture)
{
    lock();
    m_textures.push_back(texture);
    std::list<Texture *>::iterator entry = --m_textures.end();
    unlock();
    return entry;
}

std::list<Transformation *>::iterator Engine::addTransformation(Transformation *transformation)
{
    lock();
    m_transformations.push_back(transformation);
    std::list<Transformation *>::iterator entry = --m_transformations.end();
    unlock();
    return entry;
}

std::list<Widget *>::iterator Engine::addWidget(Widget *widget)
{
    lock();
    m_widgets.push_back(widget);
    std::list<Widget *>::iterator entry = --m_widgets.end();
    unlock();
    return entry;
}

void Engine::beginOffscreenDrawing(float scale)
{
    ++m_surfaceDepth;
    while (m_surfaceDepth >= static_cast<int>(m_renderSurfaces.size())) {
        m_renderSurfaces.push_back(createRenderSurface(false));
        m_renderSurfaces.back()->resize(m_viewportWidth, m_viewportHeight);
    }
    beginSurface(m_renderSurfaces[m_surfaceDepth], Vector<2, float>(0.0f, 0.0f),
                 Vector<2, float>(Vector<2, int>(m_viewportWidth, m_viewportHeight)), scale, true);
}

void Engine::dispatchWidgets(Node *node, void (Widget::*method)())
{
    if (node->m_display->m_visibility.value() && node->isShown() && node->isHitTestable()) {
        if (node->m_widget)
            (node->m_widget->*method)();
        for (std::list<Node *>::iterator it = node->m_children.begin(); it != node->m_children.end(); ++it)
            dispatchWidgets(*it, method);
    }
}

void Engine::setPointerCapture(Node *node)
{
    m_pointerCapture = node;
}

// The scale is unused (has it too).
void Engine::clearSurfaceBounds(Node *node, float scale)
{
    Matrix<float> matrix;
    getModelViewMatrix(matrix);
    Vector<2, float> minimum;
    Vector<2, float> maximum;
    node->getBounds(minimum, maximum, matrix);
    m_renderSurfaces[m_surfaceDepth % m_renderSurfaces.size()]->clearRect(minimum, maximum - minimum, false);
}

Button *Engine::createButton(Node *node, int type, const std::wstring &name)
{
    return new Button(this, node, type, name);
}

CurveShape *Engine::createCurveShape(const std::wstring &name)
{
    return new CurveShape(this, name);
}

Display *Engine::createDisplay(const std::wstring &name)
{
    return new Display(this, name);
}

Edit *Engine::createEdit(Node *node, const std::wstring &name)
{
    return new Edit(this, node, name);
}

GenericShape *Engine::createGenericShape(const std::wstring &source, const std::wstring &name)
{
    return new GenericShape(this, source, name);
}

ListWidget *Engine::createListWidget(Node *node, const std::wstring &name)
{
    return new ListWidget(this, node, name);
}

MeshShape *Engine::createMeshShape(const std::wstring &name)
{
    return new MeshShape(this, name);
}

Movie *Engine::createMovie(const std::wstring &name)
{
    delete findMovie(name);
    Movie *movie = new Movie(this, name);
    m_movies[name] = movie;
    return movie;
}

Node *Engine::createNode(Transformation *transformation, Shape *shape, Display *display, Node *parent,
                         const std::wstring &name)
{
    if (!transformation)
        transformation = createTransformation();
    if (!display)
        display = createDisplay();
    Node *node = new Node(this, transformation, shape, display, name);
    if (parent)
        parent->addChild(node);
    return node;
}

// Creates a pixel font from a texture grid. With no column count, a sixteen-by-sixteen grid
// represents characters zero to 255.
PixelFont *Engine::createPixelFont(Texture *texture, int columns, int rows, const std::wstring &characters)
{
    std::wstring glyphs = characters;
    if (columns < 1) {
        columns = 16;
        rows = 16;
        std::wstringstream stream;
        for (unsigned short character = 0; character < 256; ++character)
            stream << static_cast<wchar_t>(character);
        glyphs = stream.str();
    }
    int glyphCount = (int)glyphs.size();
    PixelFont *font = new PixelFont(this, texture->getName());
    font->setTexture(texture);
    // A pixel font's size is two thirds of a cell's width, its spacing a sixteenth of a cell's width
    // less one; an empty glyph a third of a cell.
    font->m_size = static_cast<float>(2 * (texture->m_width / columns) / 3);
    Vector<2, int> cellSize(texture->m_width / columns, texture->m_height / rows);
    int spacing = texture->m_width / columns / 16 - 1;
    Vector<2, float> offset(0.0f, 0.0f);
    for (int i = 0; i < glyphCount; ++i) {
        Vector<2, int> origin(i % columns * cellSize[0], i / columns * cellSize[1]);
        Vector<2, int> minimum = cellSize - Vector<2, int>(1, 1);
        Vector<2, int> maximum(0, 0);
        for (int x = 0; x < cellSize[0]; ++x) {
            for (int y = 0; y < cellSize[1]; ++y) {
                if (origin[0] + x < texture->m_width && origin[1] + y < texture->m_height &&
                    texture->m_pixels[4 * ((origin[1] + y) * texture->m_width + origin[0] + x) + 3] > 0) {
                    if (x > maximum[0])
                        maximum[0] = x;
                    if (y > maximum[1])
                        maximum[1] = y;
                    if (x < minimum[0])
                        minimum[0] = x;
                    if (y < minimum[1])
                        minimum[1] = y;
                }
            }
        }
        if (maximum[0] < minimum[0] || maximum[1] < minimum[1]) {
            minimum = Vector<2, int>(0, 0);
            maximum = Vector<2, int>(texture->m_width / columns / 3, texture->m_height / rows / 3);
        }
        minimum = minimum - Vector<2, int>(1, 1);
        maximum = maximum + Vector<2, int>(1, 1);
        Vector<2, int> dimensions = maximum - minimum + Vector<2, int>(1, 1);
        Vector<2, float> inverseSize(1.0f / texture->m_width, 1.0f / texture->m_height);
        Vector<2, float> size((float)dimensions[0], (float)dimensions[1]);
        offset[1] = static_cast<float>(minimum[1]);
        Vector<2, float> textureSize(size[0] * inverseSize[0], size[1] * inverseSize[1]);
        Vector<2, float> texturePosition = Vector<2, float>(minimum + origin) * inverseSize;
        font->setGlyph(glyphs[i], offset, size, texturePosition, textureSize,
                       static_cast<float>(spacing + dimensions[0]));
    }
    return font;
}

PlasmaFont *Engine::createPlasmaFont(const std::wstring &name)
{
    return new PlasmaFont(this, name);
}

PlasmaFont *Engine::createPlasmaFont(Node *root, const std::wstring &name)
{
    PlasmaFont *font = createPlasmaFont(name);
    for (std::list<Node *>::iterator it = root->m_children.begin(); it != root->m_children.end(); ++it) {
        if ((*it)->getName().size() == 1 && (*it)->m_shape) {
            std::wstring character = (*it)->getName();
            Transformation *transformation = (*it)->m_transformation;
            Vector<2, float> minimum = transformation->m_matrix * (*it)->m_shape->getMinimum();
            Vector<2, float> maximum = transformation->m_matrix * (*it)->m_shape->getMaximum();
            if (!(*it)->isHitTestable())
                (*it)->setShape(0);
            Vector<2, float> glyphSize = maximum - minimum;
            Vector<2, float> offset = minimum - (*it)->m_transformation->m_translation.value();
            font->setGlyph(character[0], offset, glyphSize, *it, maximum.m_elements[0] - minimum.m_elements[0]);
        }
    }
    return font;
}

PopUpButton *Engine::createPopUpButton(Node *node, const std::wstring &name)
{
    return new PopUpButton(this, node, name);
}

ScrollButton *Engine::createScrollButton(Node *node, const std::wstring &name)
{
    return new ScrollButton(this, node, name);
}

ScrollSlider *Engine::createScrollSlider(Node *node, const std::wstring &name)
{
    return new ScrollSlider(this, node, name);
}

SmoothMeshShape *Engine::createSmoothMeshShape(const std::wstring &name)
{
    return new SmoothMeshShape(this, name);
}

TextShape *Engine::createTextShape(const std::wstring &text, const std::wstring &name)
{
    return new TextShape(this, text, name);
}

Transformation *Engine::createTransformation(const std::wstring &name)
{
    return new Transformation(this, name);
}

Widget *Engine::createWidget(Node *node, const std::wstring &name)
{
    return new Widget(this, node, name);
}

void Engine::deleteDisplay(Display *display)
{
    lock();
    m_displays.erase(display->m_engineEntry);
    unlock();
    delete display;
}

void Engine::deleteDrawing(Drawing *drawing)
{
    delete drawing;
}

void Engine::deleteRenderSurface(RenderSurface *surface)
{
    delete surface;
}

void Engine::deleteNode(Node *node)
{
    if (node->getEngine() != this)
        return;
    if (node == m_pointerCapture)
        releasePointerCapture();
    if (node == m_pointerNode)
        m_pointerNode = 0;
    std::list<Node *> children = node->m_children;
    for (std::list<Node *>::iterator it = children.begin(); it != children.end(); ++it)
        deleteNode(*it);
    if (node->m_parent)
        node->m_parent->removeChild(node);
    Transformation *transformation = node->m_transformation;
    if (transformation) {
        if (transformation->countNodes() == 1)
            deleteTransformation(transformation);
        else
            transformation->m_nodes.erase(node);
    }
    Shape *shape = node->m_shape;
    if (shape) {
        if (shape->countNodes() == 1)
            deleteShape(shape);
        else
            shape->m_nodes.erase(node);
    }
    Display *display = node->m_display;
    if (display) {
        if (display->countNodes() == 1)
            deleteDisplay(display);
        else
            display->m_nodes.erase(node);
    }
    if (node->m_widget)
        deleteWidget(node->m_widget);
    lock();
    m_nodes.erase(node->m_engineEntry);
    unlock();
    delete node;
}

void Engine::deleteShape(Shape *shape)
{
    lock();
    m_shapes.erase(shape->m_engineEntry);
    unlock();
    delete shape;
}

void Engine::deleteTexture(Texture *texture)
{
    lock();
    m_textures.erase(texture->m_engineEntry);
    m_texturesById.erase(texture->m_id);
    unlock();
    delete texture;
}

void Engine::deleteTransformation(Transformation *transformation)
{
    lock();
    m_transformations.erase(transformation->m_engineEntry);
    unlock();
    delete transformation;
}

void Engine::deleteWidget(Widget *widget)
{
    if (widget == m_focusWidget)
        m_focusWidget = 0;
    if (widget == m_popUpWidget)
        m_popUpWidget = 0;
    if (widget == m_pressedPopUpItem)
        m_pressedPopUpItem = 0;
    lock();
    m_widgets.erase(widget->m_engineEntry);
    unlock();
    delete widget;
}

int Engine::drawWindow()
{
    beginWindowDrawing();
    if (m_blurSurface)
        m_blurSurface->resize(m_viewportWidth, m_viewportHeight);
    m_renderTargetSwitchCount = 0;
    m_surfaceDepth = 0;
    m_root->draw((1 << NODE_RENDER_CULL_SCREEN));
    endWindowDrawing();
    return m_renderTargetSwitchCount;
}

void Engine::endOffscreenDrawing()
{
    endSurface();
    if (m_surfaceDepth < static_cast<int>(m_renderSurfaces.size()))
        m_filterSurface = m_renderSurfaces[m_surfaceDepth];
    --m_surfaceDepth;
}

// Reads a chunk's length and returns where its payload ends.
std::streampos enterChunk(std::istream &stream)
{
    int length;
    stream.read(reinterpret_cast<char *>(&length), 4);
    return stream.tellg() + std::streampos(length);
}

void Engine::lock()
{
    EnterCriticalSection(&m_lock);
}

Node *Engine::getPointerNode()
{
    if (m_pointerCapture)
        return m_pointerCapture;
    return m_root->hitTest(m_pointer, HIT_FLAGS_NONE);
}

// A cached font of that file, size, stroke and kind, or a new one: TrueType files become pixel or
// outline fonts, others are read as plasma fonts.
Font *Engine::loadLegacyFont(const std::wstring &path, float size, bool transient, float stroke, bool pixel)
{
    int slash = (int)path.find_last_of(L'/');
    int backslash = (int)path.find_last_of(L'\\');
    int separator = (std::max)(slash, backslash);
    std::wstring basename = path.substr(separator + 1, path.size() - separator - 1);
    for (std::list<Font *>::iterator it = m_fontList.begin(); it != m_fontList.end(); ++it) {
        if ((*it)->m_path == basename && (*it)->m_size == size && (*it)->m_transient == transient &&
            (*it)->m_spacing == stroke &&
            (pixel ? static_cast<Font *>(dynamic_cast<PixelFont *>(*it))
                   : static_cast<Font *>(dynamic_cast<PlasmaFont *>(*it)))) {
            return *it;
        }
    }
    try {
        int dot = (int)path.find_last_of(L'.');
        std::wstring extension = path.substr(dot + 1, path.size() - dot - 1);
        // Narrow toupper applied to UTF-16 code units,.
        std::transform(extension.begin(), extension.end(), extension.begin(), toupper);
        if (extension == L"TTF" && pixel)
            return loadPixelFont(path, size, transient, stroke);
        if (extension == L"TTF")
            return loadOutlineFont(path, size);
        return loadPlasmaFont(path, size);
    } catch (IOException &) {
        return 0;
    }
}

// A plasma font of outline glyphs from a TrueType file: a node per character with a backdrop quad
// and the glyph's contours as a smooth mesh.
PlasmaFont *Engine::loadOutlineFont(const std::wstring &path, float size, const std::wstring &characters)
{
    int slash = (int)path.find_last_of(L'/');
    int backslash = (int)path.find_last_of(L'\\');
    int separator = (std::max)(slash, backslash);
    std::wstring basename = path.substr(separator + 1, path.size() - separator - 1);
    for (std::list<Font *>::iterator it = m_fontList.begin(); it != m_fontList.end(); ++it) {
        PlasmaFont *font = dynamic_cast<PlasmaFont *>(*it);
        if (font && font->m_path == basename) {
            PlasmaFont *copy = font->clone();
            copy->m_path = basename;
            copy->m_size = size;
            copy->m_spacing = 0.0f;
            return copy;
        }
    }
    std::ifstream stream;
    stream.open(std::filesystem::path(path), std::ios::in | std::ios::binary);
    std::wstring directory;
    if (!stream.is_open()) {
        for (std::list<std::wstring>::iterator it = m_fontPaths.begin(); it != m_fontPaths.end(); ++it) {
            stream.open(std::filesystem::path(*it + L"/" + basename), std::ios::in | std::ios::binary);
            if (stream.is_open()) {
                directory = *it;
                break;
            }
        }
    }
    if (!stream.is_open()) {
        PlasmaFont *font = new PlasmaFont(this, basename);
        font->m_path = basename;
        font->m_size = size;
        return font;
    }
    stream.close();
    if (directory.empty())
        directory = path;
    else
        directory += L"/" + basename;
    FT_Face face;
    if (FT_New_Face(m_freeType, narrowString(directory).c_str(), 0, &face))
        return 0;
    std::string source = std::string(face->family_name) + " " + face->style_name;
    FT_Set_Pixel_Sizes(face, ImportedGlyphPixelSize, ImportedGlyphPixelSize);
    std::wstring glyphs = characters;
    if (glyphs.empty())
        glyphs =
            L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ`1234567890 .,-_:!?<>/\\|[]()=+~@#$%^&*()'\"|\u00b0";
    int count = (int)glyphs.size();
    Node *root = createNode(0, 0, 0, m_fonts, basename);
    root->m_flags |= (1 << NODE_INTERNAL);
    for (int character = 0; character < count; ++character) {
        wchar_t name[2] = {glyphs[character], 0};
        FT_Load_Char(face, glyphs[character], FT_LOAD_NO_BITMAP | FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING);
        SmoothMeshShape *background = createSmoothMeshShape(name);
        Node *glyphNode = createNode(0, background, 0, root, name);
        glyphNode->m_flags |= (1 << NODE_INTERNAL);
        background->setSubdivisions(0, true);
        background->m_vertexPositions.resize(4);
        background->m_vertexColors.resize(4);
        background->m_vertexPositions.value()[0] = Vector<2, float>(face->glyph->bitmap_left * 0.015625f, -100.0f);
        background->m_vertexPositions.value()[1] = Vector<2, float>(face->glyph->advance.x * 0.015625f, -100.0f);
        background->m_vertexPositions.value()[2] = Vector<2, float>(face->glyph->advance.x * 0.015625f, 0.0f);
        background->m_vertexPositions.value()[3] = Vector<2, float>(face->glyph->bitmap_left * 0.015625f, 0.0f);
        // The glyph editor's background, grey (RGBA).
        background->m_vertexColors.value()[0] = Vector<4, float>(0.6f, 0.6f, 0.6f, 0.5f);
        background->m_vertexColors.value()[1] = Vector<4, float>(0.8f, 0.8f, 0.8f, 0.5f);
        background->m_vertexColors.value()[2] = Vector<4, float>(0.7f, 0.7f, 0.7f, 0.5f);
        background->m_vertexColors.value()[3] = Vector<4, float>(0.5f, 0.5f, 0.5f, 0.5f);
        std::vector<std::vector<int>> backgroundFaces(1);
        backgroundFaces.back().resize(4);
        backgroundFaces.back()[0] = 0;
        backgroundFaces.back()[1] = 1;
        backgroundFaces.back()[2] = 2;
        backgroundFaces.back()[3] = 3;
        background->setFaces(backgroundFaces, true);
        SmoothMeshShape *shape = createSmoothMeshShape(name);
        Node *outlineNode = createNode(0, shape, 0, glyphNode, name);
        outlineNode->m_flags |= (1 << NODE_INTERNAL);
        int start = 0;
        std::vector<std::vector<int>> faces;
        float width = ImportedStrokeWidth;
        for (int contour = 0; contour < face->glyph->outline.n_contours; ++contour) {
            Vector<2, float> previousDelta(0.0f, 0.0f);
            int last = face->glyph->outline.contours[contour];
            // 26.6 fixed point to pixels, y up.
            Vector<2, float> previous(static_cast<float>(face->glyph->outline.points[last].x),
                                      -static_cast<float>(face->glyph->outline.points[last].y));
            previous *= 0.015625f;
            bool previousOffCurve = !(face->glyph->outline.tags[last] & FT_CURVE_TAG_ON);
            std::vector<int> vertices;
            for (int point = start; point <= face->glyph->outline.contours[contour]; ++point) {
                Vector<2, float> position(static_cast<float>(face->glyph->outline.points[point].x),
                                          -static_cast<float>(face->glyph->outline.points[point].y));
                position *= 0.015625f;
                Vector<2, float> delta = position - previous;
                if (!(delta[0] * delta[0] + delta[1] * delta[1] < 0.001f)) {
                    if (face->glyph->outline.tags[point] & FT_CURVE_TAG_CUBIC)
                        std::cout << "FT_CURVE_TAG_CUBIC" << std::endl;
                    if (delta[0] * previousDelta[1] - delta[1] * previousDelta[0] == 0.0f &&
                        delta.dot(previousDelta) < 0.0f) {
                        shape->m_vertexPositions.erase(shape->m_vertexPositions.length() - 1);
                        shape->m_vertexTexCoords.erase(shape->m_vertexPositions.length());
                        shape->m_vertexColors.erase(shape->m_vertexPositions.length());
                        shape->m_strokeColors.erase(shape->m_vertexPositions.length());
                        shape->m_strokeWidths.erase(shape->m_vertexPositions.length());
                        shape->m_vertexFlags.pop_back();
                        vertices.pop_back();
                    }
                    previousDelta = delta;
                    if (previousOffCurve && !(face->glyph->outline.tags[point] & FT_CURVE_TAG_ON)) {
                        vertices.push_back(shape->m_vertexPositions.length());
                        shape->m_vertexPositions.append((previous + position) * 0.5f);
                        shape->m_vertexColors.append(Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
                        shape->m_vertexTexCoords.append(Vector<2, float>(0.0f, 0.0f));
                        shape->m_strokeColors.append(Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
                        shape->m_strokeWidths.append(width);
                        shape->m_vertexFlags.push_back(VERTEX_SHARP);
                    }
                    vertices.push_back(shape->m_vertexPositions.length());
                    shape->m_vertexPositions.append(position);
                    shape->m_vertexColors.append(Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
                    shape->m_vertexTexCoords.append(Vector<2, float>(0.0f, 0.0f));
                    shape->m_strokeColors.append(Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f));
                    shape->m_strokeWidths.append(width);
                    shape->m_vertexFlags.push_back(
                        (face->glyph->outline.tags[point] & FT_CURVE_TAG_ON) ? VERTEX_SHARP : VERTEX_BEZIER);
                    previous = position;
                    previousOffCurve = !(face->glyph->outline.tags[point] & FT_CURVE_TAG_ON);
                }
            }
            faces.push_back(vertices);
            start = face->glyph->outline.contours[contour] + 1;
        }
        shape->m_flags = (shape->m_flags & ~((1 << MESH_NO_FILL) | (1 << MESH_OPEN))) | (1 << MESH_CONTOUR);
        shape->setFaces(faces, true);
        glyphNode->m_flags |= (1 << NODE_LOCKED) | (1 << NODE_INACTIVE);
        glyphNode->getMaximum(BOUNDS_FLAGS_NONE);
    }
    FT_Done_Face(face);
    PlasmaFont *font = createPlasmaFont(root, widenString(source));
    font->m_path = basename;
    font->m_size = size;
    font->m_spacing = 0.0f;
    return font;
}

void Engine::initializeWidgets()
{
    m_root->initializeWidgets();
    for (std::list<Widget *>::iterator it = m_widgets.begin(); it != m_widgets.end(); ++it) {
        (*it)->initialize();
        Widget::Connection *connection = (*it)->m_connections[WIDGET_EVENT_INITIALIZED];
        if (connection)
            connection->invoke();
    }
}

void Engine::characterEntered(int character)
{
    if (m_focusWidget && m_focusWidget->m_enabled)
        m_focusWidget->characterEntered(character);
}

void Engine::keyPressed(unsigned short key)
{
    m_lastKey = key;
    m_pressedKeys.insert(key);
    if (m_focusWidget && m_focusWidget->m_enabled)
        m_focusWidget->keyPressed(key);
}

void Engine::keyReleased(unsigned short key)
{
    m_lastKey = key;
    m_pressedKeys.erase(key);
    if (m_focusWidget && m_focusWidget->m_enabled)
        m_focusWidget->keyReleased(key);
}

void Engine::pointerPressed()
{
    m_buttons |= (1 << MOUSE_BUTTON_LEFT);
    Node *node = getPointerNode();
    if (node && node->m_enclosingWidget && node->m_enclosingWidget->isPopUpItem() &&
        node->m_enclosingWidget->m_enabled) {
        m_pressedPopUpItem = node->m_enclosingWidget;
        dispatchWidgets(m_root, &Widget::popUpItemPressedAnywhere);
    } else {
        m_pressedPopUpItem = 0;
    }
    if (m_popUpWidget && !m_popUpWidget->m_node->isAncestorOf(node))
        setPopUpWidget(0, 0);
    if (!node || node->m_widget != m_focusWidget)
        setFocusWidget(0);
    if (node) {
        Widget *widget = node->m_enclosingWidget;
        if (widget && widget->m_enabled)
            widget->pointerPressed();
        sendEvent(node, WIDGET_EVENT_POINTER_PRESSED);
    }
    dispatchWidgets(m_root, &Widget::pointerPressedAnywhere);
}

void Engine::pointerReleased()
{
    m_buttons &= ~(unsigned)(1 << MOUSE_BUTTON_LEFT);
    Node *node = getPointerNode();
    if (m_popUpWidget && m_pressedPopUpItem)
        setPopUpWidget(0, 0);
    if (node) {
        Widget *widget = node->m_enclosingWidget;
        if (widget && widget->m_enabled)
            widget->pointerReleased();
        sendEvent(node, WIDGET_EVENT_POINTER_RELEASED);
    }
    node = getPointerNode();
    if (node) {
        Node *hit = m_root->hitTest(m_pointer, HIT_FLAGS_NONE);
        Widget *underPointer = hit ? hit->m_enclosingWidget : 0;
        Widget *widget = node->m_enclosingWidget;
        if (widget != underPointer) {
            if (widget && widget->m_enabled)
                widget->pointerLeft();
            sendEvent(node, WIDGET_EVENT_POINTER_LEFT);
        }
    }
    dispatchWidgets(m_root, &Widget::pointerReleasedAnywhere);
}

void Engine::rightButtonPressed()
{
    m_buttons |= (1 << MOUSE_BUTTON_RIGHT);
    Node *node = getPointerNode();
    if (!node || node->m_widget != m_focusWidget)
        setFocusWidget(0);
    if (node) {
        Widget *widget = node->m_enclosingWidget;
        if (widget && widget->m_enabled)
            widget->rightButtonPressed();
        sendEvent(node, WIDGET_EVENT_RIGHT_BUTTON_PRESSED);
    }
    dispatchWidgets(m_root, &Widget::rightButtonPressedAnywhere);
}

void Engine::rightButtonReleased()
{
    m_buttons &= ~(unsigned)(1 << MOUSE_BUTTON_RIGHT);
    Node *node = getPointerNode();
    if (node) {
        Widget *widget = node->m_enclosingWidget;
        if (widget && widget->m_enabled)
            widget->rightButtonReleased();
        sendEvent(node, WIDGET_EVENT_RIGHT_BUTTON_RELEASED);
    }
    dispatchWidgets(m_root, &Widget::rightButtonReleasedAnywhere);
}

void Engine::movePointer(float x, float y)
{
    m_previousPointer = m_pointer;
    m_pointer.m_elements[0] = x;
    m_pointer.m_elements[1] = y;
    Node *node = getPointerNode();
    Widget *previous = m_pointerNode ? m_pointerNode->m_enclosingWidget : 0;
    Widget *widget = node ? node->m_enclosingWidget : 0;
    if (previous != widget) {
        if (previous && previous->m_enabled)
            previous->pointerLeft();
        sendEvent(m_pointerNode, WIDGET_EVENT_POINTER_LEFT);
        if (widget && widget->m_enabled)
            widget->pointerEntered();
        sendEvent(node, WIDGET_EVENT_POINTER_ENTERED);
    }
    if (node) {
        if (widget && widget->m_enabled)
            widget->pointerMoved();
        sendEvent(node, WIDGET_EVENT_POINTER_MOVED);
    }
    m_pointerNode = node;
    dispatchWidgets(m_root, &Widget::pointerMovedAnywhere);
}

Widget *Engine::getFocusWidget()
{
    return m_focusWidget;
}

void Engine::unlock()
{
    LeaveCriticalSection(&m_lock);
}

RenderSurface *Engine::popSurface()
{
    if (m_surfaceStack.empty())
        return 0;
    RenderSurface *surface = m_surfaceStack.back();
    m_surfaceStack.pop_back();
    return surface;
}

void Engine::setPopUpWidget(Widget *widget, Widget *anchor)
{
    if (m_popUpWidget)
        m_popUpWidget->m_node->m_display->m_visibility.value() = DISPLAY_INVISIBLE;
    m_popUpWidget = widget;
    if (widget) {
        Node *node = widget->getNode();
        if (m_overlayRoot)
            node->setParent(m_overlayRoot);
        else
            node->setParent(m_root);
        if (anchor) {
            m_popUpWidget->setViewPosition(
                anchor->getContentPosition() + Vector<2, float>(0.0f, anchor->getViewHeight()), true);
        }
        if (m_popUpWidget->viewX() < 0.0f)
            m_popUpWidget->moveTo(0.0f, m_popUpWidget->viewY(), true);
        if (m_popUpWidget->viewY() < 0.0f)
            m_popUpWidget->moveTo(m_popUpWidget->viewX(), 0.0f, true);
        if (m_popUpWidget->viewX() + m_popUpWidget->viewWidth() > viewportWidth())
            m_popUpWidget->moveTo(static_cast<float>(viewportWidth()) - m_popUpWidget->viewWidth(),
                                  m_popUpWidget->viewY(), true);
        if (m_popUpWidget->viewY() + m_popUpWidget->getViewHeight() > viewportHeight()) {
            if (anchor) {
                m_popUpWidget->setViewPosition(
                    anchor->getContentPosition() - Vector<2, float>(0.0f, widget->getViewHeight()), true);
            } else {
                m_popUpWidget->moveTo(m_popUpWidget->viewX(),
                                      static_cast<float>(viewportHeight()) - m_popUpWidget->getViewHeight(), true);
            }
        }
        if (m_popUpWidget)
            m_popUpWidget->m_node->m_display->m_visibility.value() = DISPLAY_VISIBLE;
    }
}

void Engine::sendEvent(Node *node, int event)
{
    if (node && node->isHitTestable()) {
        Widget *widget = node->m_enclosingWidget;
        if (widget) {
            int lookupEvent = event;
            Widget::Connection *connection = widget->m_connections[lookupEvent];
            if (connection && (widget->m_enabled || !connection->m_enabledOnly))
                connection->invoke();
        }
    }
}

void Engine::pushSurface(RenderSurface *surface)
{
    m_surfaceStack.push_back(surface);
}

// Reads a PLX file into a node.
inline void Engine::readPLXFile(std::istream &stream, Node *target, int flags, Page *page,
                                const std::vector<unsigned __int64> &keys)
{
    if (!page)
        page = &m_page;
    PLXStreamReader(this, page, target, flags, stream, keys).read();
}

// Reads a.pla (legacy) or.plg,.pld,.plx file into target. The file's directory joins the font
// search paths. Logs the path, then dispatches to readPLAFile or readPLXFile.
bool Engine::readPlasmaFileIntoNode(const std::wstring &path, Node *target, int flags, Page *page,
                                    const std::vector<unsigned __int64> &keys)
{
    std::wcout << "read " << path << std::endl;
    int slash = (int)path.find_last_of(L'/');
    int backslash = (int)path.find_last_of(L'\\');
    int separator = (std::max)(slash, backslash);
    std::ifstream stream(streamPath(path), std::ios::binary);
    if (!stream.is_open())
        return false;
    std::wstring directory = path.substr(0, separator);
    m_fontPaths.push_front(directory);
    int dot = (int)path.find_last_of(L'.');
    std::wstring extension = path.substr(dot + 1, path.size() - dot - 1);
    // Narrow toupper applied to UTF-16 code units, as in plasma (undefined for values outside unsigned
    // char).
    std::transform(extension.begin(), extension.end(), extension.begin(), toupper);
    stream.seekg(0);
    if (extension == L"PLA") {
        readPLAFile(stream, target, flags, page);
        stream.close();
        return true;
    }
    if (extension == L"PLG" || extension == L"PLD" || extension == L"PLX") {
        readPLXFile(stream, target, flags, page, keys);
        stream.close();
        return true;
    }
    stream.close();
    return false;
}

// An attribute's sequence: its name by id (1) or as a string (3), then its keys (2).
void readAttributeSequence(std::istream &stream, Attribute *attribute, std::map<int, std::string> &names)
{
    std::streampos end = enterChunk(stream);
    Sequence *sequence = 0;
    while (stream.tellg() != end && !stream.eof()) {
        int identifier = readValue<int>(stream);
        switch (identifier) {
        case SequenceChunk_NameId:
            enterChunk(stream);
            sequence = attribute->newSequence(widenString(names[readValue<int>(stream)]));
            break;
        case SequenceChunk_Name:
            enterChunk(stream);
            sequence = attribute->newSequence(widenString(readString(stream)));
            break;
        case SequenceChunk_Key: {
            std::streampos keyEnd = enterChunk(stream);
            int frame = 0;
            int element = 0;
            float smoothness = 0.0f;
            while (stream.tellg() != keyEnd && !stream.eof()) {
                switch (readValue<int>(stream)) {
                case SequenceKeyChunk_Element:
                    enterChunk(stream);
                    element = readValue<int>(stream);
                    break;
                case SequenceKeyChunk_Frame:
                    enterChunk(stream);
                    frame = readValue<int>(stream);
                    break;
                case SequenceKeyChunk_Smoothness:
                    enterChunk(stream);
                    smoothness = readValue<float>(stream);
                    break;
                default:
                    skipChunk(stream);
                    break;
                }
            }
            // A key before a sequence name dereferences null, as in plasma.
            sequence->addKey(element, frame, smoothness);
            break;
        }
        default:
            skipChunk(stream);
            break;
        }
    }
}

Button *Engine::readButton(std::istream &stream)
{
    std::streampos end = enterChunk(stream);
    Button *button = createButton(0, Button::Push);
    while (stream.tellg() != end && !stream.eof()) {
        int identifier = readValue<int>(stream);
        if (!readButtonProperty(stream, button, identifier))
            skipChunk(stream);
    }
    return button;
}

// Reads a button.
inline bool Engine::readButtonProperty(std::istream &stream, Button *button, int identifier)
{
    switch (identifier) {
    case WidgetChunk_ButtonType:
        enterChunk(stream);
        button->m_type = readValue<int>(stream);
        return true;
    default:
        return readWidgetProperty(stream, button, identifier);
    }
}

CurveShape *Engine::readCurveShape(std::istream &stream, std::map<int, std::string> &names)
{
    const std::streampos end = enterChunk(stream);
    CurveShape *shape = createCurveShape();
    std::vector<int> vertexFlags;
    int subdivisions = shape->m_subdivisions;
    bool closed = shape->m_closed;
    int strokeCapType = shape->m_strokeCapType;
    int strokeSegments = shape->m_strokeSegments;
    while (stream.tellg() != end && !stream.eof()) {
        switch (readValue<int>(stream)) {
        case CurveChunk_Name:
            enterChunk(stream);
            shape->setName(widenString(readString(stream)));
            break;
        case CurveChunk_VertexFlags: {
            enterChunk(stream);
            int count = readValue<int>(stream);
            if (count > 0) {
                vertexFlags.resize(count);
                stream.read(reinterpret_cast<char *>(&vertexFlags[0]), count * sizeof(int));
            }
            break;
        }
        case CurveChunk_Positions:
            readArrayAttribute(stream, &shape->m_vertexPositions, names);
            break;
        case CurveChunk_TexCoords:
            readArrayAttribute(stream, &shape->m_vertexTexCoords, names);
            break;
        case CurveChunk_Colors:
            readArrayAttribute(stream, &shape->m_vertexColors, names);
            break;
        case CurveChunk_Widths:
            readArrayAttribute(stream, &shape->m_vertexWidths, names);
            break;
        case CurveChunk_Subdivisions:
            enterChunk(stream);
            subdivisions = readValue<int>(stream);
            break;
        case CurveChunk_Closed:
            enterChunk(stream);
            closed = readValue<int>(stream) != 0;
            break;
        case CurveChunk_StrokeCap:
            enterChunk(stream);
            strokeCapType = readValue<int>(stream);
            break;
        case CurveChunk_StrokeSegments:
            enterChunk(stream);
            strokeSegments = readValue<int>(stream);
            break;
        default:
            skipChunk(stream);
            break;
        }
    }
    shape->setVertexFlags(vertexFlags);
    shape->setSubdivisions(subdivisions);
    shape->setClosed(closed);
    shape->m_strokeCapType = strokeCapType;
    shape->setStrokeSegments(strokeSegments);
    shape->attributesChanged(ATTRIBUTES_CHANGED_ALL);
    return shape;
}

Widget *Engine::readEdit(std::istream &stream)
{
    std::streampos end = enterChunk(stream);
    Edit *widget = createEdit(0);
    while (stream.tellg() != end && !stream.eof()) {
        int identifier = readValue<int>(stream);
        if (!readWidgetProperty(stream, widget, identifier))
            skipChunk(stream);
    }
    return widget;
}

GenericShape *Engine::readGenericShape(std::istream &stream, std::map<int, std::string> &names)
{
    // ReadPLAFile passes the names map; this reader does not use it.
    const std::streampos end = enterChunk(stream);
    GenericShape *shape = createGenericShape();
    while (stream.tellg() != end && !stream.eof()) {
        switch (readValue<int>(stream)) {
        case GenericShapeChunk_Name:
            enterChunk(stream);
            shape->setName(widenString(readString(stream)));
            break;
        case GenericShapeChunk_Source:
            enterChunk(stream);
            shape->setSource(widenString(readString(stream)));
            break;
        case GenericShapeChunk_Position:
            enterChunk(stream);
            shape->m_position = readValue<Vector<2, float>>(stream);
            break;
        case GenericShapeChunk_Size:
            enterChunk(stream);
            shape->m_size = readValue<Vector<2, float>>(stream);
            break;
        default:
            skipChunk(stream);
            break;
        }
    }
    shape->attributesChanged(ATTRIBUTES_CHANGED_ALL);
    return shape;
}

// Reads a list widget.
inline bool Engine::readListWidgetProperty(std::istream &stream, ListWidget *widget, int identifier)
{
    return readWidgetProperty(stream, widget, identifier);
}

Widget *Engine::readListWidget(std::istream &stream)
{
    std::streampos end = enterChunk(stream);
    ListWidget *widget = createListWidget(0);
    while (stream.tellg() != end && !stream.eof()) {
        int identifier = readValue<int>(stream);
        if (!readListWidgetProperty(stream, widget, identifier))
            skipChunk(stream);
    }
    return widget;
}

// A movie: its name (1), then entries (2) of a sequence name, by id (1) or as a string (3), and a
// value (2).
Movie *Engine::readMovie(std::istream &stream, std::map<int, std::string> &names)
{
    std::streampos end = enterChunk(stream);
    Movie *movie = 0;
    while (stream.tellg() != end && !stream.eof()) {
        switch (readValue<int>(stream)) {
        case MovieChunk_Name:
            enterChunk(stream);
            movie = createMovie(widenString(readString(stream)));
            break;
        case MovieChunk_Entry: {
            const std::streampos entryEnd = enterChunk(stream);
            std::string name;
            // Stays uninitialised without a value chunk, and an entry before the name dereferences null, as in
            // plasma.
            int value;
            while (stream.tellg() != entryEnd && !stream.eof()) {
                switch (readValue<int>(stream)) {
                case MovieEntryChunk_NameId:
                    enterChunk(stream);
                    name = names[readValue<int>(stream)];
                    break;
                case MovieEntryChunk_Value:
                    enterChunk(stream);
                    value = readValue<int>(stream);
                    break;
                case MovieEntryChunk_Name:
                    enterChunk(stream);
                    name = readString(stream);
                    break;
                default:
                    skipChunk(stream);
                    break;
                }
            }
            movie->addEntry(widenString(name), value);
            break;
        }
        default:
            skipChunk(stream);
            break;
        }
    }
    return movie;
}

// Reads a legacy.pla file into target: transformations (1), mesh (3), text (4), curve (10) and
// generic (24) shapes, textures (5), fonts (6), nodes (7; the first one is target itself), movies
// (8), sequence names (9), widgets (11..19) and the page (20..23), then links the nodes by the
// child ids they listed.
void Engine::readPLAFile(std::ifstream &stream, Node *target, int flags, Page *page)
{
    if (!page)
        page = &m_page;
    std::map<int, Node *> nodes;
    std::map<int, Shape *> shapes;
    std::map<int, Transformation *> transformations;
    std::map<int, Widget *> widgets;
    std::map<int, std::string> names;
    std::map<int, int> textureIds;
    std::map<int, int> fontIds;
    nodes[NO_OBJECT_ID] = 0;
    shapes[NO_OBJECT_ID] = 0;
    fontIds[NO_OBJECT_ID] = NO_OBJECT_ID;
    textureIds[NO_OBJECT_ID] = NO_OBJECT_ID;
    transformations[NO_OBJECT_ID] = 0;
    widgets[NO_OBJECT_ID] = 0;
    int shapeIndex = 0;
    int transformationIndex = 0;
    int widgetIndex = 0;
    int nodeIndex = 0;
    page->size.m_elements[2] = 100.0f;
    page->unit = 0;
    std::list<PLAFontReference> fonts;
    std::list<TextShape *> textShapes;
    while (!stream.eof()) {
        int identifier = readValue<int>(stream);
        if (stream.eof())
            break;
        switch (identifier) {
        case PlaChunk_Name: {
            const std::streampos end = enterChunk(stream);
            std::string name;
            int id = NO_OBJECT_ID;
            while (stream.tellg() != end && !stream.eof()) {
                switch (readValue<int>(stream)) {
                case NameChunk_Id:
                    enterChunk(stream);
                    id = readValue<int>(stream);
                    break;
                case NameChunk_Name:
                    enterChunk(stream);
                    name = readString(stream);
                    break;
                default:
                    skipChunk(stream);
                    break;
                }
            }
            if (id != NO_OBJECT_ID)
                names[id] = name;
            break;
        }
        case PlaChunk_Texture: {
            const std::streampos end = enterChunk(stream);
            std::string name;
            int id = NO_OBJECT_ID;
            TextureFormat format;
            int width = 0;
            int height = 0;
            std::vector<unsigned char> pixels;
            while (stream.tellg() != end && !stream.eof()) {
                switch (readValue<int>(stream)) {
                case TextureChunk_Name:
                    enterChunk(stream);
                    name = readString(stream);
                    break;
                case TextureChunk_Id:
                    enterChunk(stream);
                    id = readValue<int>(stream);
                    break;
                case TextureChunk_PixelFormat:
                    enterChunk(stream);
                    format.pixelFormat = readValue<int>(stream);
                    break;
                case TextureChunk_MinFilter:
                    enterChunk(stream);
                    format.minFilter = readValue<int>(stream);
                    break;
                case TextureChunk_MaxFilter:
                    enterChunk(stream);
                    format.maxFilter = readValue<int>(stream);
                    break;
                case TextureChunk_HorizontalWrap:
                    enterChunk(stream);
                    format.horizontalWrap = readValue<int>(stream);
                    break;
                case TextureChunk_VerticalWrap:
                    enterChunk(stream);
                    format.verticalWrap = readValue<int>(stream);
                    break;
                case TextureChunk_Width:
                    enterChunk(stream);
                    width = readValue<int>(stream);
                    break;
                case TextureChunk_Height:
                    enterChunk(stream);
                    height = readValue<int>(stream);
                    break;
                case TextureChunk_Pixels: {
                    enterChunk(stream);
                    int count = readValue<int>(stream);
                    if (count > 0) {
                        pixels.resize(count);
                        stream.read(reinterpret_cast<char *>(pixels.data()), count);
                    }
                    break;
                }
                default:
                    skipChunk(stream);
                    break;
                }
            }
            if (id != NO_OBJECT_ID) {
                Texture *texture = createTexture(width, height, pixels.data(), format, widenString(name), false);
                textureIds[id] = texture->m_id;
            }
            break;
        }
        case PlaChunk_Font: {
            const std::streampos end = enterChunk(stream);
            PLAFontReference font;
            font.name = "";
            font.source = "";
            font.size = 0;
            font.stroke = 0.0f;
            font.id = NO_OBJECT_ID;
            while (stream.tellg() != end && !stream.eof()) {
                switch (readValue<int>(stream)) {
                case FontChunk_Name:
                    enterChunk(stream);
                    font.name = readString(stream);
                    break;
                case FontChunk_Source:
                    enterChunk(stream);
                    font.source = readString(stream);
                    break;
                case FontChunk_Size:
                    enterChunk(stream);
                    font.size = readValue<int>(stream);
                    break;
                case FontChunk_Id:
                    enterChunk(stream);
                    font.id = readValue<int>(stream);
                    break;
                default:
                    skipChunk(stream);
                    break;
                }
            }
            fonts.push_back(font);
            break;
        }
        case PlaChunk_Text: {
            textShapes.push_back(readTextShape(stream, names, fontIds));
            shapes[shapeIndex++] = textShapes.back();
            break;
        }
        case PlaChunk_SmoothMesh:
            shapes[shapeIndex++] = readSmoothMeshShape(stream, names, textureIds);
            break;
        case PlaChunk_GenericShape:
            shapes[shapeIndex++] = readGenericShape(stream, names);
            break;
        case PlaChunk_Curve:
            shapes[shapeIndex++] = readCurveShape(stream, names);
            break;
        case PlaChunk_Transformation:
            transformations[transformationIndex++] = readTransformation(stream, names);
            break;
        case PlaChunk_Widget:
            widgets[widgetIndex++] = readWidget(stream);
            break;
        case PlaChunk_WidgetAlias:
            widgets[widgetIndex++] = readWidget(stream);
            break;
        case PlaChunk_Button:
            widgets[widgetIndex++] = readButton(stream);
            break;
        case PlaChunk_Edit:
            widgets[widgetIndex++] = readEdit(stream);
            break;
        case PlaChunk_ListWidget:
            widgets[widgetIndex++] = readListWidget(stream);
            break;
        case PlaChunk_ScrollButton:
            widgets[widgetIndex++] = readScrollButton(stream);
            break;
        case PlaChunk_ScrollSlider:
            widgets[widgetIndex++] = readScrollSlider(stream);
            break;
        case PlaChunk_Node: {
            const std::streampos end = enterChunk(stream);
            std::string name;
            Shape *shape = 0;
            Transformation *transformation = 0;
            Widget *widget = 0;
            std::list<int> childIds;
            // The legacy display attributes are read and discarded.
            DiscreteAttribute<int> visibility("", 0);
            DiscreteAttribute<int> clipping("", 0);
            // The initial value is an uninitialised vector, as in plasma.
            ContinuousAttribute<Vector<4, float>> fill("", Vector<4, float>());
            fill.value() = Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
            visibility.value() = 1;
            clipping.value() = 0;
            std::list<std::pair<std::string, std::string>> variables;
            unsigned nodeFlags; // stays uninitialised without chunk 7, as in plasma
            while (stream.tellg() != end && !stream.eof()) {
                switch (readValue<int>(stream)) {
                case LegacyNodeChunk_Name:
                    enterChunk(stream);
                    name = readString(stream);
                    break;
                case LegacyNodeChunk_ShapeId:
                    enterChunk(stream);
                    shape = shapes[readValue<int>(stream)];
                    break;
                case LegacyNodeChunk_TransformationId:
                    enterChunk(stream);
                    transformation = transformations[readValue<int>(stream)];
                    break;
                case LegacyNodeChunk_WidgetId:
                    enterChunk(stream);
                    widget = widgets[readValue<int>(stream)];
                    break;
                case LegacyNodeChunk_ParentId:
                    enterChunk(stream);
                    readValue<int>(stream); // the parent id: superseded by the child lists
                    break;
                case LegacyNodeChunk_ChildId:
                    enterChunk(stream);
                    childIds.push_back(readValue<int>(stream));
                    break;
                case LegacyNodeChunk_Flags:
                    enterChunk(stream);
                    nodeFlags = readValue<int>(stream);
                    break;
                case LegacyNodeChunk_Visibility:
                    readAttribute(stream, &visibility, names);
                    break;
                case LegacyNodeChunk_Clipping:
                    readAttribute(stream, &clipping, names);
                    break;
                case LegacyNodeChunk_Fill:
                    readAttribute(stream, &fill, names);
                    break;
                case LegacyNodeChunk_Variable:
                    enterChunk(stream);
                    // The second argument is read first: the pair is (value, key).
                    variables.push_back(std::make_pair(readString(stream), readString(stream)));
                    break;
                default:
                    skipChunk(stream);
                    break;
                }
            }
            Node *node;
            // The first node is the target itself.
            if (nodeIndex == 0) {
                node = target;
            } else {
                node = createNode(transformation, shape, 0, 0, widenString(name));
                node->setWidget(widget);
                node->m_flags = nodeFlags;
                for (std::list<std::pair<std::string, std::string>>::iterator it = variables.begin();
                     it != variables.end(); ++it)
                    node->setVariable(it->second, it->first);
                if (flags & (1 << LOAD_INTERNAL))
                    node->m_flags |= (1 << NODE_INTERNAL);
            }
            node->m_pendingChildIds = childIds;
            nodes[nodeIndex++] = node;
            break;
        }
        case PlaChunk_Movie:
            readMovie(stream, names);
            break;
        case PlaChunk_PageWidth:
            enterChunk(stream);
            page->size.m_elements[0] = readValue<float>(stream);
            break;
        case PlaChunk_PageHeight:
            enterChunk(stream);
            page->size.m_elements[1] = readValue<float>(stream);
            break;
        case PlaChunk_PageDepth:
            enterChunk(stream);
            page->size.m_elements[2] = readValue<float>(stream);
            break;
        case PlaChunk_PageUnit:
            enterChunk(stream);
            page->unit = readValue<int>(stream);
            break;
        default:
            skipChunk(stream);
            break;
        }
    }
    stream.close();
    for (std::map<int, Node *>::iterator it = nodes.begin(); it != nodes.end(); ++it) {
        if (it->second) {
            for (std::list<int>::iterator child = it->second->m_pendingChildIds.begin();
                 child != it->second->m_pendingChildIds.end(); ++child)
                it->second->addChild(nodes[*child]);
            it->second->m_pendingChildIds.clear();
        }
    }
    if (!(flags & (1 << LOAD_SHARED))) {
        for (std::map<int, Node *>::iterator it = nodes.begin(); it != nodes.end(); ++it) {
            Node *node = it->second;
            if (node && (!node->m_parent || node->m_parent == target))
                node->makeUnique();
        }
    }
    for (std::list<PLAFontReference>::iterator it = fonts.begin(); it != fonts.end(); ++it) {
        Font *font = loadLegacyFont(widenString(it->source), static_cast<float>(it->size), false, it->stroke, true);
        if (font) {
            fontIds[it->id] = font->id();
            m_fontsById[font->id()] = font;
        }
    }
    // The text shapes are walked without touching them, as in plasma (the font remapping it once did
    // is gone).
    for (std::list<TextShape *>::iterator it = textShapes.begin(); it != textShapes.end(); ++it) {
    }
    for (std::map<int, Widget *>::iterator it = widgets.begin(); it != widgets.end(); ++it) {
        if (ScrollButton *button = dynamic_cast<ScrollButton *>(it->second))
            button->setTarget(widgets[button->m_targetId]);
        if (ScrollSlider *slider = dynamic_cast<ScrollSlider *>(it->second))
            slider->setTarget(widgets[slider->m_targetId]);
    }
    if (!(flags & (1 << LOAD_NO_WIDGET_INIT))) {
        int x = m_viewportX;
        int y = m_viewportY;
        int width = m_viewportWidth;
        int height = m_viewportHeight;
        setViewport(0, 0, static_cast<int>(page->size.m_elements[0]), static_cast<int>(page->size.m_elements[1]),
                    false);
        initializeWidgets();
        setViewport(x, y, width, height, true);
    }
}

PixelFont *Engine::loadPixelFont(const std::wstring &path, float size, bool transient, float stroke,
                                 const std::wstring &characters)
{
    int dot = (int)path.find_last_of(L'.');
    std::wstring extension = path.substr(dot + 1, path.size() - dot - 1);
    int slash = (int)path.find_last_of(L'/');
    int backslash = (int)path.find_last_of(L'\\');
    int separator = (std::max)(slash, backslash);
    // Narrow toupper applied to UTF-16 code units,.
    std::transform(extension.begin(), extension.end(), extension.begin(), toupper);
    std::wstring basename = path.substr(separator + 1, path.size() - separator - 1);
    if (extension == L"TTF") {
        PixelFont *font = new PixelFont(this, basename);
        font->m_path = basename;
        font->m_size = size;
        font->m_spacing = stroke;
        font->m_transient = transient;
        return font;
    }
    Texture *texture = loadImageTexture(path);
    if (!texture) {
        for (std::list<std::wstring>::iterator it = m_fontPaths.begin(); it != m_fontPaths.end(); ++it) {
            texture = loadImageTexture(*it + L"/" + basename);
            if (texture)
                break;
        }
    }
    if (!texture) {
        PixelFont *font = new PixelFont(this, path);
        font->m_path = path;
        return font;
    }
    {
        TextureFormat format;
        format.pixelFormat = TEXTURE_RGBA;
        format.minFilter = TEXTURE_FILTER_NEAREST;
        format.maxFilter = TEXTURE_FILTER_NEAREST;
        format.horizontalWrap = TEXTURE_WRAP_REPEAT;
        format.verticalWrap = TEXTURE_WRAP_REPEAT;
        texture->setFormat(format);
    }
    texture->m_transient = true;
    PixelFont *font = createPixelFont(texture);
    if (font)
        font->m_path = basename;
    return font;
}

PlasmaFont *Engine::loadPlasmaFont(const std::wstring &path, float size)
{
    int slash = (int)path.find_last_of(L'/');
    int backslash = (int)path.find_last_of(L'\\');
    int separator = (std::max)(slash, backslash);
    std::wstring basename = path.substr(separator + 1, path.size() - separator - 1);
    std::wstring directory;
    std::ifstream stream;
    stream.open(streamPath(path), std::ios::in | std::ios::binary);
    if (!stream.is_open()) {
        for (std::list<std::wstring>::iterator it = m_fontPaths.begin(); it != m_fontPaths.end(); ++it) {
            stream.open(streamPath(*it + L"/" + basename), std::ios::in | std::ios::binary);
            if (stream.is_open()) {
                directory = *it;
                break;
            }
        }
    }
    if (!stream.is_open()) {
        PlasmaFont *font = new PlasmaFont(this, basename);
        font->m_path = basename;
        font->m_size = size;
        return font;
    }
    stream.close();
    Node *fontNode = 0;
    for (std::list<Node *>::iterator it = m_fonts->m_children.begin(); it != m_fonts->m_children.end(); ++it) {
        if ((*it)->getName() == basename) {
            fontNode = *it;
            break;
        }
    }
    if (directory.empty())
        directory = path;
    else
        directory += L"/" + basename;
    if (!fontNode) {
        fontNode = createNode(0, 0, 0, m_fonts, basename);
        readPlasmaFileIntoNode(directory, fontNode,
                               (1 << LOAD_SHARED) | (1 << LOAD_NO_WIDGET_INIT) | (1 << LOAD_INTERNAL),
                               static_cast<Page *>(0), std::vector<unsigned __int64>(1, 0));
        fontNode->m_flags |= (1 << NODE_INTERNAL);
    }
    PlasmaFont *font = createPlasmaFont(fontNode, basename);
    font->m_path = basename;
    font->m_size = size;
    font->m_spacing = 0.0f;
    return font;
}

Widget *Engine::readScrollButton(std::istream &stream)
{
    std::streampos end = enterChunk(stream);
    ScrollButton *widget = createScrollButton(0);
    while (stream.tellg() != end && !stream.eof()) {
        int identifier = readValue<int>(stream);
        if (!readScrollButtonProperty(stream, widget, identifier))
            skipChunk(stream);
    }
    return widget;
}

bool Engine::readScrollButtonProperty(std::istream &stream, ScrollButton *button, int identifier)
{
    switch (identifier) {
    case WidgetChunk_ScrollDirection:
        enterChunk(stream);
        button->m_direction = readValue<int>(stream);
        return true;
    case WidgetChunk_ScrollStep:
        enterChunk(stream);
        button->m_step = readValue<float>(stream);
        return true;
    case WidgetChunk_ScrollTargetId:
        enterChunk(stream);
        button->m_targetId = readValue<int>(stream);
        return true;
    default:
        return readWidgetProperty(stream, button, identifier);
    }
}

Widget *Engine::readScrollSlider(std::istream &stream)
{
    std::streampos end = enterChunk(stream);
    ScrollSlider *widget = createScrollSlider(0);
    while (stream.tellg() != end && !stream.eof()) {
        int identifier = readValue<int>(stream);
        if (!readScrollSliderProperty(stream, widget, identifier))
            skipChunk(stream);
    }
    return widget;
}

bool Engine::readScrollSliderProperty(std::istream &stream, ScrollSlider *slider, int identifier)
{
    switch (identifier) {
    case WidgetChunk_SliderDirection:
        enterChunk(stream);
        slider->m_direction = readValue<int>(stream) != SLIDER_VERTICAL;
        return true;
    case WidgetChunk_SliderTargetId:
        enterChunk(stream);
        slider->m_targetId = readValue<int>(stream);
        return true;
    default:
        return readWidgetProperty(stream, slider, identifier);
    }
}

SmoothMeshShape *Engine::readSmoothMeshShape(std::istream &stream, std::map<int, std::string> &names,
                                             std::map<int, int> &textureIds)
{
    std::streampos end = enterChunk(stream);
    SmoothMeshShape *shape = createSmoothMeshShape();
    std::vector<std::vector<int>> faces;
    int subdivisions = shape->m_subdivisions;
    float smoothWeight = shape->m_smoothWeight;
    while (stream.tellg() != end && !stream.eof()) {
        switch (readValue<int>(stream)) {
        case MeshChunk_Name:
            enterChunk(stream);
            shape->setName(widenString(readString(stream)));
            break;
        case MeshChunk_Face: {
            enterChunk(stream);
            int count = readValue<int>(stream);
            std::vector<int> face(count);
            // The first element's address is taken even when count is zero.
            stream.read(reinterpret_cast<char *>(&face[0]), count * sizeof(int));
            faces.push_back(face);
            break;
        }
        case MeshChunk_VertexFlags: {
            enterChunk(stream);
            int count = readValue<int>(stream);
            if (count > 0) {
                shape->m_vertexFlags.resize(count);
                stream.read(reinterpret_cast<char *>(&shape->m_vertexFlags[0]), count * sizeof(int));
            }
            break;
        }
        case MeshChunk_Positions:
            readArrayAttribute(stream, &shape->m_vertexPositions, names);
            break;
        case MeshChunk_TexCoords:
            readArrayAttribute(stream, &shape->m_vertexTexCoords, names);
            break;
        case MeshChunk_Colors:
            readArrayAttribute(stream, &shape->m_vertexColors, names);
            break;
        case MeshChunk_StrokeColors:
            readArrayAttribute(stream, &shape->m_strokeColors, names);
            break;
        case MeshChunk_StrokeWidths:
            readArrayAttribute(stream, &shape->m_strokeWidths, names);
            break;
        case MeshChunk_Texture: {
            readAttribute(stream, &shape->m_texture, names);
            for (int i = 0; i < shape->m_texture.size(); ++i)
                shape->m_texture.m_values[i] = textureIds[shape->m_texture.m_values[i]];
            break;
        }
        case MeshChunk_TextureTranslation:
            readAttribute(stream, &shape->m_textureTranslation, names);
            break;
        case MeshChunk_TextureRotation:
            readAttribute(stream, &shape->m_textureRotation, names);
            break;
        case MeshChunk_TextureDeformation:
            readAttribute(stream, &shape->m_textureDeformation, names);
            break;
        case MeshChunk_TexturePivot:
            readAttribute(stream, &shape->m_texturePivot, names);
            break;
        case MeshChunk_Subdivisions:
            enterChunk(stream);
            subdivisions = readValue<int>(stream);
            break;
        case MeshChunk_SmoothWeight:
            enterChunk(stream);
            smoothWeight = readValue<float>(stream);
            break;
        case MeshChunk_Flags:
            enterChunk(stream);
            shape->m_flags = readValue<int>(stream);
            break;
        default:
            skipChunk(stream);
            break;
        }
    }
    shape->m_smoothWeight = smoothWeight;
    shape->setFaces(faces, false);
    shape->setSubdivisions(subdivisions, true);
    return shape;
}

// A length-prefixed string. The count is trusted, as in plasma.
std::string readString(std::istream &stream)
{
    int length = readValue<int>(stream);
    if (!length)
        return std::string("");
    char *buffer = new char[length + 1];
    buffer[length] = 0;
    stream.read(buffer, length);
    std::string value(buffer);
    delete[] buffer;
    return value;
}

TextShape *Engine::readTextShape(std::istream &stream, std::map<int, std::string> &names, std::map<int, int> &fontIds)
{
    // ReadPLAFile passes its font ids; this reader does not use them.
    const std::streampos end = enterChunk(stream);
    // The text is a std::wstring made from a null pointer, whose length is read at address zero, as in
    // plasma.
    TextShape *shape = createTextShape(0);
    while (stream.tellg() != end && !stream.eof()) {
        switch (readValue<int>(stream)) {
        case TextChunk_Name:
            enterChunk(stream);
            shape->setName(widenString(readString(stream)));
            break;
        case TextChunk_Strings: {
            const std::streampos stringsEnd = enterChunk(stream);
            int index = 0;
            while (stream.tellg() != stringsEnd && !stream.eof()) {
                switch (readValue<int>(stream)) {
                case AttributeChunk_Frame:
                    enterChunk(stream);
                    if (index == shape->m_string.size())
                        shape->m_string.duplicateElement();
                    shape->m_string.m_values[index] = readWString(stream);
                    ++index;
                    break;
                case AttributeChunk_Sequence:
                    readAttributeSequence(stream, &shape->m_string, names);
                    break;
                default:
                    skipChunk(stream);
                    break;
                }
            }
            break;
        }
        case TextChunk_Color:
            readAttribute(stream, &shape->m_color, names);
            break;
        default:
            skipChunk(stream);
            break;
        }
    }
    shape->attributesChanged(ATTRIBUTES_CHANGED_ALL);
    return shape;
}

// An image file (JPEG, BMP, PNG, PPM, TIFF or TGA, by its extension) as an RGBA texture, through
// FreeImage.
Texture *Engine::loadImageTexture(const std::wstring &path)
{
    int dot = (int)path.find_last_of(L'.');
    std::wstring extension = path.substr(dot + 1, path.size() - dot - 1);
    // Narrow toupper applied to UTF-16 code units,.
    std::transform(extension.begin(), extension.end(), extension.begin(), toupper);
    FREE_IMAGE_FORMAT imageFormat = FIF_UNKNOWN;
    if (extension == L"JPG")
        imageFormat = FIF_JPEG;
    if (extension == L"BMP")
        imageFormat = FIF_BMP;
    if (extension == L"PNG")
        imageFormat = FIF_PNG;
    if (extension == L"PPM")
        imageFormat = FIF_PPM;
    if (extension == L"TIF")
        imageFormat = FIF_TIFF;
    if (extension == L"TGA")
        imageFormat = FIF_TARGA;
    else if (imageFormat == FIF_UNKNOWN)
        return 0;
#ifdef _WIN32
    FIBITMAP *bitmap = FreeImage_LoadU(imageFormat, path.c_str(), 0);
#else
    // FreeImage_LoadU only exists on Windows.
    FIBITMAP *bitmap = FreeImage_Load(imageFormat, streamPath(path).c_str(), 0);
#endif
    if (!bitmap)
        return 0;
    int width = FreeImage_GetWidth(bitmap);
    int height = FreeImage_GetHeight(bitmap);
    std::vector<char> pixels(4 * width * height);
    // Divides by the width without testing it for zero and assumes RGB or RGBA scanlines,.
    unsigned bytesPerPixel = FreeImage_GetLine(bitmap) / FreeImage_GetWidth(bitmap);
    int destination = 0;
    for (int y = 0; y < static_cast<int>(FreeImage_GetHeight(bitmap)); ++y) {
        const unsigned char *source = FreeImage_GetScanLine(bitmap, FreeImage_GetHeight(bitmap) - y - 1);
        for (int x = 0; x < static_cast<int>(FreeImage_GetWidth(bitmap)); ++x) {
            pixels[destination] = source[2];
            pixels[destination + 1] = source[1];
            pixels[destination + 2] = source[0];
            if (bytesPerPixel == 3)
                pixels[destination + 3] = static_cast<char>(255);
            else
                pixels[destination + 3] = source[3];
            destination += 4;
            source += bytesPerPixel;
        }
    }
    FreeImage_Unload(bitmap);
    return createTexture(width, height, reinterpret_cast<const unsigned char *>(&pixels[0]), TextureFormat(), L"",
                         false);
}

// The same for an image file's bytes already in memory; path only gives the format.
Texture *Engine::loadImageTexture(const std::wstring &path, unsigned char *data, int size)
{
    int dot = (int)path.find_last_of(L'.');
    std::wstring extension = path.substr(dot + 1, path.size() - dot - 1);
    // Narrow toupper applied to UTF-16 code units,.
    std::transform(extension.begin(), extension.end(), extension.begin(), toupper);
    FREE_IMAGE_FORMAT imageFormat = FIF_UNKNOWN;
    if (extension == L"JPG")
        imageFormat = FIF_JPEG;
    if (extension == L"BMP")
        imageFormat = FIF_BMP;
    if (extension == L"PNG")
        imageFormat = FIF_PNG;
    if (extension == L"PPM")
        imageFormat = FIF_PPM;
    if (extension == L"TIF")
        imageFormat = FIF_TIFF;
    if (extension == L"TGA")
        imageFormat = FIF_TARGA;
    else if (imageFormat == FIF_UNKNOWN)
        return 0;
    FIMEMORY *memory = FreeImage_OpenMemory(data, size);
    FIBITMAP *bitmap = FreeImage_LoadFromMemory(imageFormat, memory, 0);
    FreeImage_CloseMemory(memory);
    if (!bitmap)
        return 0;
    int width = FreeImage_GetWidth(bitmap);
    int height = FreeImage_GetHeight(bitmap);
    std::vector<char> pixels(4 * width * height);
    // Divides by the width without testing it for zero and assumes RGB or RGBA scanlines,.
    unsigned bytesPerPixel = FreeImage_GetLine(bitmap) / FreeImage_GetWidth(bitmap);
    int destination = 0;
    for (int y = 0; y < static_cast<int>(FreeImage_GetHeight(bitmap)); ++y) {
        const unsigned char *source = FreeImage_GetScanLine(bitmap, FreeImage_GetHeight(bitmap) - y - 1);
        for (int x = 0; x < static_cast<int>(FreeImage_GetWidth(bitmap)); ++x) {
            pixels[destination] = source[2];
            pixels[destination + 1] = source[1];
            pixels[destination + 2] = source[0];
            if (bytesPerPixel == 3)
                pixels[destination + 3] = static_cast<char>(255);
            else
                pixels[destination + 3] = source[3];
            destination += 4;
            source += bytesPerPixel;
        }
    }
    FreeImage_Unload(bitmap);
    return createTexture(width, height, reinterpret_cast<const unsigned char *>(&pixels[0]), TextureFormat(), L"",
                         false);
}

Transformation *Engine::readTransformation(std::istream &stream, std::map<int, std::string> &names)
{
    const std::streampos end = enterChunk(stream);
    Transformation *transformation = createTransformation();
    while (stream.tellg() != end && !stream.eof()) {
        switch (readValue<int>(stream)) {
        case TransformationChunk_Name:
            enterChunk(stream);
            transformation->setName(widenString(readString(stream)));
            break;
        case TransformationChunk_Translation:
            readAttribute(stream, &transformation->m_translation, names);
            break;
        case TransformationChunk_Rotation:
            readAttribute(stream, &transformation->m_rotation, names);
            break;
        case TransformationChunk_Deformation:
            readAttribute(stream, &transformation->m_deformation, names);
            break;
        case TransformationChunk_Pivot:
            readAttribute(stream, &transformation->m_pivot, names);
            break;
        default:
            skipChunk(stream);
            break;
        }
    }
    transformation->attributesChanged(ATTRIBUTES_CHANGED_ALL);
    return transformation;
}

// A length-prefixed UTF-16 string. The count is trusted, as in plasma.
std::wstring readWString(std::istream &stream)
{
    int length = readValue<int>(stream);
    if (!length)
        return std::wstring(L"");
    // Two bytes per character in the file, whatever the size of wchar_t.
    std::vector<unsigned short> buffer(length + 1, 0);
    stream.read(reinterpret_cast<char *>(&buffer[0]), length * 2);
    return std::wstring(buffer.begin(), std::find(buffer.begin(), buffer.end(), 0));
}

Widget *Engine::readWidget(std::istream &stream)
{
    std::streampos end = enterChunk(stream);
    Widget *widget = createWidget(0);
    while (stream.tellg() != end && !stream.eof()) {
        int identifier = readValue<int>(stream);
        if (!readWidgetProperty(stream, widget, identifier))
            skipChunk(stream);
    }
    return widget;
}

bool Engine::readWidgetProperty(std::istream &stream, Widget *widget, int identifier)
{
    switch (identifier) {
    case WidgetChunk_Name:
        enterChunk(stream);
        widget->setName(widenString(readString(stream)));
        return true;
    case WidgetChunk_Caption:
        enterChunk(stream);
        widget->setCaption(readWString(stream), false);
        return true;
    case WidgetChunk_InnerBindPosition:
        enterChunk(stream);
        widget->m_innerBindPos = readValue<Vector<2, float>>(stream);
        return true;
    case WidgetChunk_InnerBindSize:
        enterChunk(stream);
        widget->m_innerBindSize = readValue<Vector<2, float>>(stream);
        return true;
    case WidgetChunk_BindPosition:
        enterChunk(stream);
        widget->m_bindPos = readValue<Vector<2, float>>(stream);
        return true;
    case WidgetChunk_BindSize:
        enterChunk(stream);
        widget->m_bindSize = readValue<Vector<2, float>>(stream);
        return true;
    case WidgetChunk_FramePosition:
        enterChunk(stream);
        widget->m_framePos = readValue<Vector<2, float>>(stream);
        return true;
    case WidgetChunk_FrameSize:
        enterChunk(stream);
        widget->m_frameSize = readValue<Vector<2, float>>(stream);
        return true;
    case WidgetChunk_BindMatrix:
        enterChunk(stream);
        widget->m_bindMatrix = readValue<Matrix<float>>(stream);
        widget->m_inverseBindMatrix = widget->m_bindMatrix;
        widget->m_inverseBindMatrix.invert();
        return true;
    case WidgetChunk_StretchX:
        enterChunk(stream);
        widget->setStretchesX(readValue<int>(stream) == 1);
        return true;
    case WidgetChunk_StretchY:
        enterChunk(stream);
        widget->setStretchesY(readValue<int>(stream) == 1);
        return true;
    case WidgetChunk_HorizontalAlignment:
        enterChunk(stream);
        widget->m_horizontalAlignment = readValue<int>(stream);
        return true;
    case WidgetChunk_VerticalAlignment:
        enterChunk(stream);
        widget->m_verticalAlignment = readValue<int>(stream);
        return true;
    default:
        return false;
    }
}

void Engine::releasePointerCapture()
{
    m_pointerCapture = 0;
    movePointer(m_pointer[0], m_pointer[1]);
}

void Engine::setViewport(int x, int y, int width, int height, bool notify)
{
    m_viewportX = x;
    m_viewportY = y;
    m_viewportWidth = width;
    m_viewportHeight = height;
    if (notify) {
        for (std::list<Widget *>::iterator it = m_widgets.begin(); it != m_widgets.end(); ++it) {
            (*it)->viewportChanged();
            Widget::Connection *connection = (*it)->m_connections[WIDGET_EVENT_VIEWPORT_CHANGED];
            if (connection)
                connection->invoke();
        }
    }
}

void Engine::setFocusWidget(Widget *widget)
{
    if (m_focusWidget) {
        if (widget == m_focusWidget)
            return;
        m_focusWidget->m_changed = true;
        sendEvent(m_focusWidget->m_node, WIDGET_EVENT_FOCUS_LOST);
    }
    m_focusWidget = widget;
    if (widget)
        sendEvent(widget->m_node, WIDGET_EVENT_FOCUS_GAINED);
}

void skipChunk(std::istream &stream)
{
    int length;
    stream.read(reinterpret_cast<char *>(&length), 4);
    stream.seekg(stream.tellg() + std::streampos(length));
}

Texture *Engine::findTexture(int id)
{
    std::map<int, Texture *>::iterator found = m_texturesById.find(id);
    if (found == m_texturesById.end())
        return 0;
    return found->second;
}

}

std::string narrowString(const std::wstring &text)
{
    return std::string(text.begin(), text.end());
}

namespace plasma {

bool Engine::advance(int milliseconds)
{
    m_caretClock += milliseconds;
    m_frameTime = milliseconds;
    m_renderTargetSwitchCount = 0;
    return m_root->animate(milliseconds);
}

}

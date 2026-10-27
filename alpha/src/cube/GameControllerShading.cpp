// Shades block-face corners from neighboring block light and occlusion.
#include "cube/GameController.h"
#include "cube/WorldBlocks.h"
#include <math.h>
namespace cube {

plasma::Vector<4, float> GameController::shade(int x, int y, int z, const plasma::Vector<4, float> &color,
                                               const plasma::Vector<3, int> &normal, bool plain)
{
    if (plain)
        return color;

    float brightness = 0.0f;
    float openness = 0.0f;

    // The blocks around the corner: a cube of 6 x 6 x 6, or along an axis the face points to, the one
    // block on the face's side.
    plasma::Vector<3, int> from(-3, -3, -3);
    plasma::Vector<3, int> to(3, 3, 3);
    if (normal[0] > 0) {
        from[0] = 0;
        to[0] = 1;
    }
    if (normal[0] < 0) {
        from[0] = -1;
        to[0] = 0;
    }
    if (normal[1] > 0) {
        from[1] = 0;
        to[1] = 1;
    }
    if (normal[1] < 0) {
        from[1] = -1;
        to[1] = 0;
    }
    if (normal[2] > 0) {
        from[2] = 0;
        to[2] = 1;
    }
    if (normal[2] < 0) {
        from[2] = -1;
        to[2] = 0;
    }

    // Each block counts less with its distance from the corner.
    float weight = 0.0f;
    float light = 0.0f;
    float open = 0.0f;
    for (int dx = from[0]; dx < to[0]; dx++) {
        for (int dy = from[1]; dy < to[1]; dy++) {
            for (int dz = from[2]; dz < to[2]; dz++) {
                const Block &block = m_world.getBlock(x + dx, y + dy, z + dz, 0);
                int type = block.type & ~BlockFlagMask;
                float empty = 1.0f;
                int level;
                if (type == 13) {
                    level = 255;
                } else if (type != BlockAir && type != BlockWater) {
                    level = MinimumBlockLight;
                    empty = 0.0f;
                } else {
                    if (block.color[0] < MinimumBlockLight)
                        level = MinimumBlockLight;
                    else
                        level = block.color[0];
                    if (level < 0)
                        continue;
                }
                float distance = (float)fabs(dx + 0.5f);
                float distanceY = (float)fabs(dy + 0.5f);
                if (distanceY > distance)
                    distance = distanceY;
                float distanceZ = (float)fabs(dz + 0.5f);
                if (distanceZ > distance)
                    distance = distanceZ;
                float share = (3.0f - distance) / 3.0f;
                light += level * share;
                open += share * empty;
                weight += share;
            }
        }
    }
    if (weight > 0.0f) {
        brightness = light / (weight * 255.0f);
        openness = open / weight;
    }

    // The colour greyed: a tenth of it, pushed away from its luminance ten times over (the luminance
    // with the weights 0.3, 0.59 and 0.11).
    float red = color[0];
    float green = color[1];
    float blue = color[2];
    if (red < 0.0f)
        red = 0.0f;
    else if (red > 1.0f)
        red = 1.0f;
    if (green < 0.0f)
        green = 0.0f;
    else if (green > 1.0f)
        green = 1.0f;
    if (blue < 0.0f)
        blue = 0.0f;
    else if (blue > 1.0f)
        blue = 1.0f;
    red *= 0.1f;
    green *= 0.1f;
    blue *= 0.1f;
    float luminance = red * 0.3f + green * 0.59f + blue * 0.11f;
    float shift = luminance * -9.0f; // one product, added to each
    red = red * 10.0f + shift;
    green = green * 10.0f + shift;
    blue = blue * 10.0f + shift;
    if (red < 0.0f)
        red = 0.0f;
    else if (red > 1.0f)
        red = 1.0f;
    if (green < 0.0f)
        green = 0.0f;
    else if (green > 1.0f)
        green = 1.0f;
    if (blue < 0.0f)
        blue = 0.0f;
    else if (blue > 1.0f)
        blue = 1.0f;
    // The lit colour and the grey one mixed by the openness (the grey term's products come first:
    // operator+'s argument is evaluated before the object).
    plasma::Vector<3, float> lit(color[0], color[1], color[2]);
    plasma::Vector<3, float> grey(red, green, blue);
    plasma::Vector<3, float> mixed = lit * openness + grey * (1.0f - openness);
    return plasma::Vector<4, float>(mixed[0], mixed[1], mixed[2], brightness);
}

}

#ifndef ENGINE_SOURCE_GXU_GXUFONTSLUG_H
#define ENGINE_SOURCE_GXU_GXUFONTSLUG_H

#include <freetype/freetype.h>
#include <cstdint>
#include <vector>

// Slug-style glyph data: the outline is kept as quadratic Bezier segments in em
// space, so the pixel shader can solve for coverage analytically instead of
// reconstructing it from a rasterised field.
//
// One em is 1.0; y points up; the origin is the glyph origin on the baseline.

struct SLUGCURVE {
    float m_p0[2];  // start point
    float m_p1[2];  // control point
    float m_p2[2];  // end point
};

struct SLUGGLYPH {
    std::vector<SLUGCURVE> m_curves;
    float m_minX;
    float m_minY;
    float m_maxX;
    float m_maxY;
};

// Extracts one glyph as quadratic segments. Straight edges are encoded as
// quadratics whose control point is the midpoint, which is what the shader
// expects (it only ever solves quadratics).
//
// pixelSize is the size the face was loaded at, used to normalise the 26.6 pixel
// coordinates FreeType hands back into em space.
bool GxuFontSlugExtractGlyph(FT_Face face, uint32_t code, uint32_t pixelSize, SLUGGLYPH* out);

// Packs the curves into the texture layout the shader reads: two texels per curve,
// the first holding (p0.x, p0.y, p1.x, p1.y) and the second (p2.x, p2.y, 0, 0).
// Values are written as 16-bit floats. Returns the number of texels written, or 0
// if the buffer is too small.
uint32_t GxuFontSlugPackCurves(const SLUGGLYPH& glyph, uint16_t* dest, uint32_t texelCapacity);

// Number of texels a glyph needs in the curve texture.
uint32_t GxuFontSlugCurveTexels(const SLUGGLYPH& glyph);

#endif

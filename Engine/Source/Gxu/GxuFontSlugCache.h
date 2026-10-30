#ifndef ENGINE_SOURCE_GXU_GXUFONTSLUG_CACHE_H
#define ENGINE_SOURCE_GXU_GXUFONTSLUG_CACHE_H

#include <freetype/freetype.h>
#include <cstdint>

// Switches for the Slug text path, kept as compile time flags so the shipping SDF
// client is never affected by half finished work.
//
//   GXU_FONT_SLUG_PROBE  draws the standalone debug quads
//   GXU_FONT_SLUG_TEXT   routes real text through Slug (not implemented yet; the
//                        vertex format change is atomic, see MEMORY.md)
#ifndef GXU_FONT_SLUG_PROBE
#define GXU_FONT_SLUG_PROBE 1
#endif

#ifndef GXU_FONT_SLUG_TEXT
#define GXU_FONT_SLUG_TEXT 0
#endif

struct CTexture;
struct CGxTex;

// Glyph storage for the Slug path.
//
// Curves live in em space, so one curve set serves every raster size of the same
// font: entries are keyed by face and character code, never by pixel size. Each
// glyph owns a contiguous run of texels in a Gr1616F page (three texels per
// quadratic). When the page fills up it is flushed and refilled, which is enough
// while the text path is still being built.

struct SLUGGLYPHINFO {
    uint32_t m_baseTexel;
    uint32_t m_curveCount;
    float m_minX;
    float m_minY;
    float m_maxX;
    float m_maxY;
};

// Packs the glyph if needed and returns where its curves live.
bool GxuFontSlugGetGlyph(FT_Face face, uint32_t code, uint32_t pixelSize, SLUGGLYPHINFO* out);

// Texture holding the curves (wrapper and the device texture the state wants).
CTexture* GxuFontSlugTexture();
CGxTex* GxuFontSlugDeviceTexture();

// Shader constants for one glyph: (firstCurveTexel, curveCount, 1/texWidth, 1/texHeight).
void GxuFontSlugShaderParams(const SLUGGLYPHINFO& glyph, float* out);

// Debug probe: draws a couple of glyphs, once per frame. Remove once text goes
// through Slug.
void GxuFontSlugDebugDraw(FT_Face face, uint32_t pixelSize);

#endif

#ifndef ENGINE_SOURCE_GXU_GXUFONTSLUGCACHE_H
#define ENGINE_SOURCE_GXU_GXUFONTSLUGCACHE_H

#include <freetype/freetype.h>
#include <cstdint>

struct CTexture;

// Curve texture for the Slug path. One 256x256 RGBA16F page holds about 1900
// quadratic segments, which is roughly sixty Latin glyphs, enough for the debug
// path and for sizing the real atlas later.
//
// The texture is CPU filled through a latch callback, same as the font atlas.

// Packs one glyph's curves into the curve texture and makes the page available.
// Returns false when the glyph has no outline or the page is full.
bool GxuFontSlugDebugBuild(FT_Face face, uint32_t code, uint32_t pixelSize);

// Texture to bind, or null when nothing has been built yet.
CTexture* GxuFontSlugTexture();

// Shader params: (firstCurveTexel, curveCount, 1/texWidth, 0).
void GxuFontSlugShaderParams(float* out);

// Glyph bounds in em space, for mapping the quad onto the outline.
bool GxuFontSlugGlyphBounds(float* minX, float* minY, float* maxX, float* maxY);

// True once a glyph is packed and the texture exists.
bool GxuFontSlugReady();

// Draws the packed glyph once as a fixed quad. Debug only: proves the curve
// texture, the shader and the em mapping line up before the real text path
// exists.
void GxuFontSlugDebugDraw(FT_Face face, uint32_t pixelSize);

#endif

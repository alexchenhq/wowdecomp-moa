#ifndef ENGINE_SOURCE_GXU_GXUFONTSLUG_CACHE_H
#define ENGINE_SOURCE_GXU_GXUFONTSLUG_CACHE_H

#include <freetype/freetype.h>
#include <cstdint>
#include "Engine/Source/Gxu/GxuFontSlugConfig.h"

struct CTexture;
struct CGxTex;
struct CGxFont;

// Glyph storage for the Slug path.
//
// Curves live in em space, so one curve set serves every raster size of the same
// font: entries are keyed by face and character code, never by pixel size. Each
// glyph owns a contiguous run of texels in a Gr1616F page (four texels per
// quadratic, the fourth holding the segment's vertical extent). When the page
// fills up it is flushed and refilled, which is enough while the text path is
// still being built.

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

// Called when a face that uses Slug could not get a glyph out of its outline
// (spaces have none). Logged once per face, because the glyph then falls back to
// an atlas quad and is invisible on a Slug face - a character going missing with
// no trace is the one failure mode that cannot be diagnosed from outside.
void GxuFontSlugNoteGlyphFailure(const CGxFont* face, uint32_t code);

// Texture holding the curves (wrapper and the device texture the state wants).
CTexture* GxuFontSlugTexture();
CGxTex* GxuFontSlugDeviceTexture();

// (1 / texWidth, 1 / texHeight, 0, 0): the only per batch constant left now that
// the curve base and count travel in the vertex.
void GxuFontSlugPageConstants(float* out);

// True for the one face that is drawn through Slug. Everything else keeps using
// the distance field in the same build.
bool GxuFontSlugFaceEnabled(const CGxFont* face);

// Binds the Slug vertex shader, pixel shader and curve page. Call inside an
// RsPush, before drawing. False when the path is not available, in which case the
// caller must fall back to the distance field.
bool GxuFontSlugBeginBatch();

// Debug probe: draws a couple of glyphs, once per frame. Remove once text goes
// through Slug.
void GxuFontSlugDebugDraw(FT_Face face, uint32_t pixelSize);

#endif

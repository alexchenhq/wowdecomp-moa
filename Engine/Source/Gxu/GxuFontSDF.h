#ifndef ENGINE_SOURCE_GXU_GXUFONTSDF_H
#define ENGINE_SOURCE_GXU_GXUFONTSDF_H

#include <cstdint>

// ---------------------------------------------------------------------------
// Font SDF (signed distance field) generation.
//
// This is an addition to the original 3.3.5a glyph pipeline, which pasted raw
// coverage into a 4-bit alpha atlas. Storing a distance field instead lets the
// pixel shader rebuild a crisp edge at any scale.
//
// The distance is computed with an exact Euclidean distance transform
// (Felzenszwalb & Huttenlocher, two separable 1D passes) on a binary mask that
// is thresholded from the FreeType 8-bit grayscale bitmap.
//
// Encoding written into the atlas:  value = 0.5 + signedDistance / (2 * spread)
// so 128 means "on the edge", 255 is deep inside, 0 is far outside.
// ---------------------------------------------------------------------------

namespace GxuFontSdf {

// Pixels of distance range stored on each side of the edge.
constexpr float kSpread = 4.0f;

// Builds the SDF for one glyph cell.
//
// gray      : FreeType 8bpp coverage bitmap (may be null for empty glyphs)
// gw, gh    : glyph bitmap size in pixels
// pitch     : row stride of the source bitmap in bytes
// cellW     : full cell width in the atlas for this face
// cellH     : full cell height in the atlas for this face
// yStart    : rows of empty space above the glyph bitmap inside the cell
// dest      : pointer to the first texel of the cell in the atlas
// destStride: atlas row stride in texels (256)
//
// Writes 0xAARRGGBB texels. The distance lives in the alpha byte; RGB is set to
// the same value so the texture still displays correctly if sampled by a
// fixed-function path.
void BuildCell(const uint8_t* gray, uint32_t gw, uint32_t gh, uint32_t pitch,
               uint32_t cellW, uint32_t cellH, uint32_t yStart,
               uint32_t* dest, uint32_t destStride);

// Clears one cell (used for empty glyphs and for glyphs we skip).
void ClearCell(uint32_t cellW, uint32_t cellH, uint32_t* dest, uint32_t destStride);

} // namespace GxuFontSdf

#endif

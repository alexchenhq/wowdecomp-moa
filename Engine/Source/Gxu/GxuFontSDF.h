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
//
// NOTE: this variant derives the distance from a rasterized bitmap, which
// quantizes curves. BuildFromPolygons is the accurate path; this one is only
// kept as a fallback for glyphs whose outline cannot be decomposed.
void BuildCell(const uint8_t* gray, uint32_t gw, uint32_t gh, uint32_t pitch,
               uint32_t cellW, uint32_t cellH, uint32_t yStart,
               uint32_t* dest, uint32_t destStride);

// Builds the SDF directly from the glyph outline, which is what the vector
// shape actually is. No rasterization, no thresholding, so curves stay curved.
//
// pts          : flattened outline points, interleaved (x, y), in glyph pixel
//                space with y pointing UP
// pointCount   : number of points in pts
// contourStart : index into pts where each contour begins
// contourCount : number of contours
// originX      : glyph-space x of the bitmap's left edge
// originY      : glyph-space y of the bitmap's TOP edge
// gw, gh       : bitmap size the caller will display (grid to sample on)
// out          : gw * gh bytes, one distance per pixel, row stride = gw
//
// Returns false if the outline is empty or degenerate.
bool BuildFromPolygons(const float* pts, int pointCount,
                       const int* contourStart, int contourCount,
                       float originX, float originY,
                       uint32_t gw, uint32_t gh, uint8_t* out);

// Multi-channel variant (MSDF). Each contour edge is assigned one of three
// colours, and the field stores the distance to the red, green and blue edges in
// separate channels. The shader takes the median of the three, which keeps
// corners sharp instead of rounding them off the way a single channel does.
//
// out: gw * gh * 3 bytes, interleaved R, G, B.
bool BuildMSDFFromPolygons(const float* pts, int pointCount,
                           const int* contourStart, int contourCount,
                           float originX, float originY,
                           uint32_t gw, uint32_t gh, uint8_t* out);

// Clears one cell (used for empty glyphs and for glyphs we skip).
void ClearCell(uint32_t cellW, uint32_t cellH, uint32_t* dest, uint32_t destStride);

// Copies a prepared one-byte-per-pixel distance field into an atlas cell,
// converting it to the 0xAARRGGBB form the texture expects. The cell is cleared
// first, so callers do not have to blank the area themselves.
// channels: 1 for a grayscale field, 3 for an MSDF. With three channels the
// R/G/B texel channels carry the per-colour distances and alpha receives their
// median, so a fixed-function fallback still shows readable text.
void BlitDistance(const uint8_t* sdf, uint32_t sdfWidth, uint32_t sdfHeight,
                  uint32_t channels, uint32_t cellW, uint32_t cellH, uint32_t yStart,
                  uint32_t* dest, uint32_t destStride);

} // namespace GxuFontSdf

#endif

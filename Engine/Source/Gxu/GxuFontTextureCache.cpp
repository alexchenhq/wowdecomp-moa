#include <cstddef>
#include <cstdint>
#include <cstring>
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Gx/CGxTexFlags.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gxu/GxuFontFont.h"
#include "Engine/Source/Gxu/GxuFontMiscClasses.h"
#include "Engine/Source/Gxu/GxuFontSDF.h"
#include "Engine/Source/Gxu/GxuFontUtil.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Storm/H/storm.h"

// MODIFIED: atlas widened from ARGB4444 to ARGB8888 so the distance field has
// 8 bits of precision instead of 4.
// STATIC: 0x00C7D328
static uint32_t s_textureData[256 * 256];

// Scratch cell image used to build a distance field from the outlined/mono
// coverage paths, which do not hand us a grayscale bitmap.
static uint8_t s_cellImage[256 * 36];

// VERIFY: 7a9fdc75 manual
// OFFSET: 0x006C8E70
void TEXTURECACHE::PasteGlyphOutlinedMonochrome(const GLYPHBITMAPDATA& data, uint32_t* dest) {
    const uint8_t* source = static_cast<const uint8_t*>(data.m_data);
    uint32_t thick = this->m_theFace->m_fontFlags & 8;

    uint16_t buffer[256 * 36];
    memset(buffer, 0, sizeof(buffer));

    uint32_t yStart = data.m_yStart;
    uint16_t* line = &buffer[256 * yStart + 257];

    if (thick) {
        line++;
    }

    for (uint32_t y = data.m_glyphHeight; y; y--) {
        for (uint32_t x = 0; x < data.m_glyphWidth; x++) {
            line[x] = (source[x >> 3] >> (7 - (x & 7))) & 1;
        }

        source += data.m_glyphPitch;
        line += 256;
    }

    uint32_t cellHeight = this->m_theFace->m_cellHeight;
    uint32_t cellWidth = data.m_glyphCellWidth;
    uint32_t passes = (thick != 0) + 1;

    for (uint32_t pass = 0; pass < passes; pass++) {
        uint32_t y = yStart;

        if (pass == 1 && yStart) {
            y = yStart - 1;
        }

        for (; y < cellHeight; y++) {
            uint32_t row = y << 8;

            for (uint32_t x = 0; x < cellWidth; x++) {
                uint16_t mask = 0;
                uint16_t value = 0;

                if (pass == 1) {
                    if (buffer[row + x]) {
                        continue;
                    }

                    mask = 4;
                    value = 2;
                } else if (pass == 0) {
                    mask = 1;
                    value = 4;
                }

                uint16_t* cell = &buffer[row + x];
                int32_t found;

                if (y == 0) {
                    if (x == 0) {
                        found = (cell[1] & mask) || (cell[257] & mask) || (cell[256] & mask);
                    } else if (x == cellWidth - 1) {
                        found = (cell[-1] & mask) || (cell[255] & mask) || (cell[256] & mask);
                    } else {
                        found = (cell[-1] & mask) || (cell[1] & mask) || (cell[255] & mask) || (cell[256] & mask) || (cell[257] & mask);
                    }
                } else if (y == cellHeight - 1) {
                    if (x == 0) {
                        found = (cell[-256] & mask) || (cell[-255] & mask) || (cell[1] & mask);
                    } else if (x == cellWidth - 1) {
                        found = (cell[-1] & mask) || (cell[-257] & mask) || (cell[-256] & mask);
                    } else {
                        found = (cell[-257] & mask) || (cell[-256] & mask) || (cell[-255] & mask) || (cell[-1] & mask) || (cell[1] & mask);
                    }
                } else if (x == 0) {
                    found = (cell[-256] & mask) || (cell[-255] & mask) || (cell[1] & mask) || (cell[257] & mask) || (cell[256] & mask);
                } else if (x == cellWidth - 1) {
                    found = (cell[-256] & mask) || (cell[-257] & mask) || (cell[-1] & mask) || (cell[255] & mask) || (cell[256] & mask);
                } else {
                    found = (cell[-257] & mask) || (cell[-256] & mask) || (cell[-255] & mask) || (cell[-1] & mask) || (cell[1] & mask) || (cell[255] & mask) || (cell[256] & mask) || (cell[257] & mask);
                }

                if (found && *cell != 1) {
                    *cell = value;
                }
            }
        }
    }

    // MODIFIED: body and outline rim both become ink in the cell image, then the
    // cell goes through the distance field path.
    uint16_t* bufferRow = buffer;

    if (cellWidth > 256 || cellHeight > 36) {
        GxuFontSdf::ClearCell(cellWidth, cellHeight, dest, 256);
        return;
    }

    memset(s_cellImage, 0, sizeof(s_cellImage));

    for (uint32_t y = 0; y < cellHeight; y++) {
        uint8_t* line = s_cellImage + y * 256;

        for (uint32_t x = 0; x < cellWidth; x++) {
            uint16_t value = bufferRow[x];

            // MODIFIED: only the glyph body is ink. The outline rim used to be
            // baked in here, which merged it into the body and made outlined
            // text look bold and clumped. The shader expands the rim instead.
            if (value == 1 || value == 4) {
                line[x] = 255;
            }
        }

        bufferRow += 256;
    }

    GxuFontSdf::BuildCell(s_cellImage, cellWidth, cellHeight, 256, cellWidth, cellHeight, 0, dest, 256);
}

// VERIFY: 891087d8 manual
// OFFSET: 0x006C9330
void TEXTURECACHE::PasteGlyphNonOutlinedMonochrome(const GLYPHBITMAPDATA& data, uint32_t* dest) {
    // MODIFIED: monochrome glyphs are expanded into the cell image and go
    // through the same distance field path as antialiased ones, so the shader
    // sees a uniform encoding for every glyph.
    const uint8_t* source = static_cast<const uint8_t*>(data.m_data);
    uint32_t pitch = data.m_glyphPitch;
    uint32_t cellW = data.m_glyphCellWidth;
    uint32_t cellH = this->m_theFace->m_cellHeight;

    if (cellW > 256 || cellH > 36) {
        GxuFontSdf::ClearCell(cellW, cellH, dest, 256);
        return;
    }

    memset(s_cellImage, 0, sizeof(s_cellImage));

    for (uint32_t y = 0; y < data.m_glyphHeight; y++) {
        uint32_t row = data.m_yStart + y;

        if (row >= cellH) {
            break;
        }

        uint8_t* line = s_cellImage + row * 256;

        for (uint32_t x = 0; x < data.m_glyphWidth && x < cellW; x++) {
            if ((source[x >> 3] >> (7 - (x & 7))) & 1) {
                line[x] = 255;
            }
        }

        source += pitch;
    }

    GxuFontSdf::BuildCell(s_cellImage, cellW, cellH, 256, cellW, cellH, 0, dest, 256);
}

// VERIFY: c5efdcbb manual
// OFFSET: 0x006C9420
void TEXTURECACHE::PasteGlyphOutlinedAA(const GLYPHBITMAPDATA& data, uint32_t* dest) {
    // MODIFIED: when the outline-derived field exists, use it directly. It holds
    // the body only, which is exactly what the shader needs; the rim is expanded
    // there. This also skips the whole wall-following pass below.
    if (data.m_sdfData) {
        GxuFontSdf::BlitDistance(data.m_sdfData, data.m_glyphWidth, data.m_glyphHeight,
            data.m_glyphCellWidth, this->m_theFace->m_cellHeight, data.m_yStart, dest, 256);
        return;
    }
    static const uint8_t s_outlineAlpha[] = { 0, 1, 1, 3, 5, 7, 9, 11, 13, 15 };

    const uint8_t* source = static_cast<const uint8_t*>(data.m_data);
    uint32_t thick = this->m_theFace->m_fontFlags & 8;

    uint16_t alpha[256 * 36];
    uint16_t coverage[256 * 36];
    uint16_t state[256 * 36];

    memset(state, 0, sizeof(state));
    memset(coverage, 0, sizeof(coverage));

    uint16_t* stateLine = &state[256 * data.m_yStart + 257];

    if (thick) {
        stateLine++;
    }

    uint16_t* coverageLine = &coverage[stateLine - state];

    for (uint32_t y = data.m_glyphHeight; y; y--) {
        for (uint32_t x = 0; x < data.m_glyphWidth; x++) {
            uint8_t value = source[x];

            if (value) {
                stateLine[x] = 1;
                coverageLine[x] = value;
            }
        }

        coverageLine += 256;
        source += data.m_glyphPitch;
        stateLine += 256;
    }

    uint32_t cellHeight = this->m_theFace->m_cellHeight;
    uint32_t passes = (thick != 0) + 1;

    for (uint32_t pass = 0; pass < passes; pass++) {
        uint16_t value = 2 * (pass != 0) + 2;
        uint16_t mask = 2 * (pass != 0) + 1;
        int32_t firstRow = static_cast<int32_t>(data.m_yStart) - 1;

        for (uint32_t y = firstRow < 0 ? 0 : firstRow; y < cellHeight; y++) {
            uint32_t cellWidth = data.m_glyphCellWidth;
            uint32_t row = y << 8;

            for (uint32_t x = 0; x < cellWidth; x++) {
                uint16_t* cell = &state[row + x];

                if (*cell & mask) {
                    continue;
                }

                int32_t found;

                if (y == 0) {
                    if (x == 0) {
                        found = (cell[1] & mask) || (cell[257] & mask) || (cell[256] & mask);
                    } else if (x == cellWidth - 1) {
                        found = (cell[-1] & mask) || (cell[255] & mask) || (cell[256] & mask);
                    } else {
                        found = (cell[-1] & mask) || (cell[1] & mask) || (cell[255] & mask) || (cell[256] & mask) || (cell[257] & mask);
                    }
                } else if (y == cellHeight - 1) {
                    if (x == 0) {
                        found = (cell[-256] & mask) || (cell[-255] & mask) || (cell[1] & mask);
                    } else if (x == cellWidth - 1) {
                        found = (cell[-1] & mask) || (cell[-257] & mask) || (cell[-256] & mask);
                    } else {
                        found = (cell[-257] & mask) || (cell[-256] & mask) || (cell[-255] & mask) || (cell[-1] & mask) || (cell[1] & mask);
                    }
                } else if (x == 0) {
                    found = (cell[-256] & mask) || (cell[-255] & mask) || (cell[1] & mask) || (cell[257] & mask) || (cell[256] & mask);
                } else if (x == cellWidth - 1) {
                    found = (cell[-256] & mask) || (cell[-257] & mask) || (cell[-1] & mask) || (cell[255] & mask) || (cell[256] & mask);
                } else {
                    found = (cell[-257] & mask) || (cell[-256] & mask) || (cell[-255] & mask) || (cell[-1] & mask) || (cell[1] & mask) || (cell[255] & mask) || (cell[256] & mask) || (cell[257] & mask);
                }

                if (found) {
                    *cell = value;
                }
            }
        }
    }

    memset(alpha, 0, sizeof(alpha));

    for (uint32_t y = 0; y < cellHeight; y++) {
        uint32_t row = y << 8;

        for (uint32_t x = 0; x < data.m_glyphCellWidth; x++) {
            uint16_t* cell = &state[row + x];
            uint32_t cellWidth = data.m_glyphCellWidth;
            int32_t count;

            if (y == 0) {
                if (x == 0) {
                    count = (cell[0] != 0) + (cell[1] != 0) + (cell[257] != 0) + (cell[256] != 0);
                } else if (x == cellWidth - 1) {
                    count = (cell[0] != 0) + (cell[256] != 0) + (cell[255] != 0) + (cell[-1] != 0);
                } else {
                    count = (cell[0] != 0) + (cell[1] != 0) + (cell[257] != 0) + (cell[256] != 0) + (cell[255] != 0) + (cell[-1] != 0);
                }
            } else if (y == cellHeight - 1) {
                if (x == 0) {
                    count = (cell[0] != 0) + (cell[1] != 0) + (cell[-255] != 0) + (cell[-256] != 0);
                } else if (x == cellWidth - 1) {
                    count = (cell[-256] != 0) + (cell[-257] != 0) + (cell[0] != 0) + (cell[-1] != 0);
                } else {
                    count = (cell[-256] != 0) + (cell[-257] != 0) + (cell[-255] != 0) + (cell[1] != 0) + (cell[0] != 0) + (cell[-1] != 0);
                }
            } else if (x == 0) {
                count = (cell[0] != 0) + (cell[256] != 0) + (cell[1] != 0) + (cell[257] != 0) + (cell[-255] != 0) + (cell[-256] != 0);
            } else if (x == cellWidth - 1) {
                count = (cell[256] != 0) + (cell[255] != 0) + (cell[-256] != 0) + (cell[-257] != 0) + (cell[0] != 0) + (cell[-1] != 0);
            } else {
                count = (cell[256] != 0) + (cell[255] != 0) + (cell[-255] != 0) + (cell[-256] != 0) + (cell[-257] != 0) + (cell[257] != 0) + (cell[1] != 0) + (cell[0] != 0) + (cell[-1] != 0);
            }

            alpha[row + x] = s_outlineAlpha[count];
        }
    }

    // MODIFIED: the outline rim and the glyph body are unioned into one cell
    // image and pushed through the distance field builder.
    if (data.m_glyphCellWidth > 256 || this->m_theFace->m_cellHeight > 36) {
        GxuFontSdf::ClearCell(data.m_glyphCellWidth, this->m_theFace->m_cellHeight, dest, 256);
        return;
    }

    memset(s_cellImage, 0, sizeof(s_cellImage));

    uint32_t offset = 0;

    for (uint32_t y = 0; y < this->m_theFace->m_cellHeight; y++) {
        uint8_t* line = s_cellImage + y * 256;

        for (uint32_t x = 0; x < data.m_glyphCellWidth; x++) {
            // MODIFIED: use the body coverage only. The rim lives in state[] but
            // not in coverage[]; baking it into the distance field made outlined
            // glyphs visibly heavier, so the shader draws the rim analytically.
            const uint16_t cov = coverage[offset + x];

            if (cov) {
                line[x] = static_cast<uint8_t>(cov > 255 ? 255 : cov);
            }
        }

        offset += 256;
    }

    GxuFontSdf::BuildCell(s_cellImage, data.m_glyphCellWidth, this->m_theFace->m_cellHeight,
        256, data.m_glyphCellWidth, this->m_theFace->m_cellHeight, 0, dest, 256);
}

// VERIFY: 45fd06c3 manual
// OFFSET: 0x006C9C70
void TEXTURECACHE::PasteGlyphNonOutlinedAA(const GLYPHBITMAPDATA& data, uint32_t* dest) {
    // MODIFIED: prefer the field built from the outline; fall back to deriving it
    // from the raster when the outline was not available.
    if (data.m_sdfData) {
        GxuFontSdf::BlitDistance(data.m_sdfData, data.m_glyphWidth, data.m_glyphHeight,
            data.m_glyphCellWidth, this->m_theFace->m_cellHeight, data.m_yStart, dest, 256);
        return;
    }

    GxuFontSdf::BuildCell(static_cast<const uint8_t*>(data.m_data),
        data.m_glyphWidth, data.m_glyphHeight, data.m_glyphPitch,
        data.m_glyphCellWidth, this->m_theFace->m_cellHeight, data.m_yStart,
        dest, 256);
}

// VERIFY: d9c18e4c auto
// OFFSET: 0x006C9D50
void TEXTURECACHE::UpdateDirty() {
    if (this->m_anyDirtyGlyphs && this->m_texture) {
        CGxTex* gxTex = TextureGetGxTex(this->m_texture, 1, nullptr);
        GxTexUpdate(gxTex, 0, 0, 256, 256, 1);
        this->m_anyDirtyGlyphs = 0;
    }
}

// VERIFY: 0d8fc085 manual
// OFFSET: 0x006C9D90
CHARCODEDESC* TEXTURECACHE::AllocateNewGlyph(GLYPHBITMAPDATA* data) {
    uint32_t count = this->m_textureRows.m_count;

    for (uint32_t i = 0; i < count; i++) {
        CHARCODEDESC* desc = this->m_textureRows.m_data[i].CreateNewDesc(data, i, this->m_theFace->m_cellHeight);

        if (desc) {
            return desc;
        }
    }

    return nullptr;
}

// VERIFY: 248ca280 manual
// OFFSET: 0x006C9F50
void TEXTURECACHE::TextureCallback(EGxTexCommand command, uint32_t width, uint32_t height, uint32_t face, uint32_t level, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    if (command != GxTex_Latch) {
        return;
    }

    TEXTURECACHE* cache = static_cast<TEXTURECACHE*>(userArg);

    for (uint32_t row = cache->m_textureRows.m_count; row > 0; row--) {
        TEXTURECACHEROW& cacheRow = cache->m_textureRows.m_data[row - 1];

        for (CHARCODEDESC* glyph = cacheRow.m_glyphlist.Head(); glyph; glyph = cacheRow.m_glyphlist.Next(glyph)) {
            if (!cache->m_texture || !cache->m_theFace || !cache->m_theFace->m_cellHeight) {
                continue;
            }

            CGxFont* font = cache->m_theFace;
            uint32_t* dest = &s_textureData[glyph->m_glyphStartPixel + ((glyph->m_rowNumber * font->m_cellHeight) << 8)];

            if (font->m_fontFlags & 1) {
                if (font->m_fontFlags & 2) {
                    cache->PasteGlyphOutlinedMonochrome(glyph->m_bitmapData, dest);
                } else {
                    cache->PasteGlyphOutlinedAA(glyph->m_bitmapData, dest);
                }
            } else if (font->m_fontFlags & 2) {
                cache->PasteGlyphNonOutlinedMonochrome(glyph->m_bitmapData, dest);
            } else {
                cache->PasteGlyphNonOutlinedAA(glyph->m_bitmapData, dest);
            }
        }
    }

    // MODIFIED: 256 texels of ARGB8888 per row.
    texelStrideInBytes = 1024;
    texels = s_textureData;
}

// VERIFY: 0f656a97 auto
// OFFSET: 0x006CA050
void TEXTURECACHE::CreateTexture(int32_t filter) {
    // MODIFIED: ARGB8888 instead of ARGB4444. 4 bits of alpha cannot carry a
    // distance field (16 levels), 8 bits is the minimum usable precision.
    //
    // MODIFIED: the filter is forced to linear. The original client used
    // nearest filtering (fontFlags & 4 was usually 0) because a coverage bitmap
    // sampled 1:1 does not care. A distance field does: nearest sampling of it
    // aliases badly the moment the UI is scaled, which is the normal case here.
    CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1, 0, 0, 0);
    this->m_texture = TextureCreate(256, 256, GxTex_Argb8888, GxTex_Argb8888, flags, this, TEXTURECACHE::TextureCallback, "GxuFont", 0);
}

// VERIFY: ba005b39 manual
// OFFSET: 0x006CA130
void TEXTURECACHE::Initialize(CGxFont* face, uint32_t cellHeight) {
    this->m_theFace = face;
    uint32_t rows = 256 / cellHeight;
    this->m_textureRows.SetCount(rows);

    for (uint32_t i = 0; i < rows; i++) {
        this->m_textureRows.m_data[i].m_widestFreeSlot = 256;
    }
}

// INLINED
TEXTURECACHE::TEXTURECACHE() {
    this->m_texture = nullptr;
    this->m_theFace = nullptr;
    this->m_anyDirtyGlyphs = 0;
}

// INLINED
TEXTURECACHE::~TEXTURECACHE() {
    this->Destroy();
}

// INLINED
void TEXTURECACHE::Destroy() {
    if (this->m_texture) {
        HandleClose(reinterpret_cast<HOBJECT>(this->m_texture));
    }

    this->m_texture = nullptr;
    this->m_textureRows.Clear();
}

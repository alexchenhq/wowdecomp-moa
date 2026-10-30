#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <freetype/ftoutln.h>
#include "Engine/Source/Gxu/IGxuFontGlyph.h"
#include "Engine/Source/Gxu/GxuFontMiscClasses.h"
#include "Engine/Source/Gxu/GxuFontSDF.h"
#include "Engine/Source/Storm/H/storm.h"

namespace {

// MODIFIED: the outline is flattened into polylines before FT_Render_Glyph,
// because rendering replaces the glyph slot with a bitmap and the vector shape
// is lost. The distance field is then built from those polylines, which keeps
// curves round instead of quantizing them to the rasterizer's pixel grid.
// Values arrive in 26.6 fixed point and are converted to pixels on the way in.
struct OutlinePoly {
    std::vector<float> m_points;     // interleaved x, y in pixels, y pointing up
    std::vector<int> m_contourStart;

    void PushScaled(int32_t x, int32_t y) {
        m_points.push_back(static_cast<float>(x) * (1.0f / 64.0f));
        m_points.push_back(static_cast<float>(y) * (1.0f / 64.0f));
    }

    void Push(float x, float y) {
        m_points.push_back(x);
        m_points.push_back(y);
    }

    float LastX() const { return m_points[m_points.size() - 2]; }
    float LastY() const { return m_points[m_points.size() - 1]; }
    bool Usable() const { return m_points.size() >= 6 && !m_contourStart.empty(); }
};

int OutlineMoveTo(FT_Vector* to, void* user) {
    OutlinePoly* poly = static_cast<OutlinePoly*>(user);

    if (!poly) {
        return 0;
    }

    poly->m_contourStart.push_back(static_cast<int>(poly->m_points.size() / 2));
    poly->PushScaled(to->x, to->y);

    return 0;
}

int OutlineLineTo(FT_Vector* to, void* user) {
    OutlinePoly* poly = static_cast<OutlinePoly*>(user);

    if (poly) {
        poly->PushScaled(to->x, to->y);
    }

    return 0;
}

int OutlineConicTo(FT_Vector* control, FT_Vector* to, void* user) {
    OutlinePoly* poly = static_cast<OutlinePoly*>(user);

    if (!poly || poly->m_points.size() < 2) {
        return 0;
    }

    const float x0 = poly->LastX();
    const float y0 = poly->LastY();
    const float x1 = static_cast<float>(control->x) * (1.0f / 64.0f);
    const float y1 = static_cast<float>(control->y) * (1.0f / 64.0f);
    const float x2 = static_cast<float>(to->x) * (1.0f / 64.0f);
    const float y2 = static_cast<float>(to->y) * (1.0f / 64.0f);

    const int steps = 8;

    for (int i = 1; i <= steps; i++) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float mt = 1.0f - t;

        poly->Push(mt * mt * x0 + 2.0f * mt * t * x1 + t * t * x2,
                   mt * mt * y0 + 2.0f * mt * t * y1 + t * t * y2);
    }

    return 0;
}

int OutlineCubicTo(FT_Vector* control1, FT_Vector* control2, FT_Vector* to, void* user) {
    OutlinePoly* poly = static_cast<OutlinePoly*>(user);

    if (!poly || poly->m_points.size() < 2) {
        return 0;
    }

    const float x0 = poly->LastX();
    const float y0 = poly->LastY();
    const float x1 = static_cast<float>(control1->x) * (1.0f / 64.0f);
    const float y1 = static_cast<float>(control1->y) * (1.0f / 64.0f);
    const float x2 = static_cast<float>(control2->x) * (1.0f / 64.0f);
    const float y2 = static_cast<float>(control2->y) * (1.0f / 64.0f);
    const float x3 = static_cast<float>(to->x) * (1.0f / 64.0f);
    const float y3 = static_cast<float>(to->y) * (1.0f / 64.0f);

    const int steps = 12;

    for (int i = 1; i <= steps; i++) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float mt = 1.0f - t;

        const float a = mt * mt * mt;
        const float b = 3.0f * mt * mt * t;
        const float c = 3.0f * mt * t * t;
        const float d = t * t * t;

        poly->Push(a * x0 + b * x1 + c * x2 + d * x3,
                   a * y0 + b * y1 + c * y2 + d * y3);
    }

    return 0;
}

// MODIFIED: split from the original single FREETYPE_RenderGlyph so the caller
// can inspect the outline between loading and rasterizing.
int32_t FREETYPE_LoadGlyph(FT_Face face, uint32_t code, bool monochrome, int32_t allowHinting) {
    FT_UInt index = FT_Get_Char_Index(face, code);

    if (!index) {
        return 0;
    }

    // MODIFIED: grid fitting is decided by the caller. It helps when the glyph is
    // rasterised at the size it is shown at, but hurts when the raster was shrunk
    // to match the display: hinting then folds diagonals onto whole pixels and the
    // distance field faithfully encodes those steps, which reads as jaggies.
    FT_Int32 flags = FT_LOAD_LINEAR_DESIGN | FT_LOAD_PEDANTIC;

    if (!allowHinting) {
        flags |= FT_LOAD_NO_HINTING;
    }

    if (monochrome) {
        flags |= FT_LOAD_CROP_BITMAP;
    } else {
        flags |= FT_LOAD_NO_BITMAP;
    }

    return FT_Load_Glyph(face, index, flags) == 0;
}

int32_t FREETYPE_RenderLoadedGlyph(FT_Face face, bool monochrome) {
    return FT_Render_Glyph(face->glyph, static_cast<FT_Render_Mode>(monochrome)) == FT_Err_Ok;
}

bool FlattenOutline(FT_Face face, OutlinePoly* poly) {
    if (!face || !poly || !face->glyph || face->glyph->format != ft_glyph_format_outline) {
        return false;
    }

    if (!face->glyph->outline.n_contours || face->glyph->outline.n_points <= 0) {
        return false;
    }

    FT_Outline_Funcs functions;
    memset(&functions, 0, sizeof(functions));

    functions.move_to = OutlineMoveTo;
    functions.line_to = OutlineLineTo;
    functions.conic_to = OutlineConicTo;
    functions.cubic_to = OutlineCubicTo;
    functions.shift = 0;
    functions.delta = 0;

    if (FT_Outline_Decompose(&face->glyph->outline, &functions, poly) != 0) {
        return false;
    }

    return poly->Usable();
}

} // namespace

static void CalculateYOffset(uint32_t pixelSize, int32_t baseline, FT_Face face, uint32_t glyphHeight, int32_t* yOffset, uint32_t* yStart) {
    int32_t offset = 0;
    int32_t start;

    if (glyphHeight <= pixelSize) {
        int32_t top = face->glyph->bitmap_top;

        if (top <= baseline) {
            start = baseline - top;
        } else {
            start = 0;
            offset = top - baseline;
        }
    } else {
        start = 0;
    }

    start = start <= 0 ? 0 : start;

    int32_t available = pixelSize - glyphHeight;

    if (available >= start) {
        *yOffset = offset;
        *yStart = start;
    } else {
        *yOffset = pixelSize - start - glyphHeight;
        *yStart = pixelSize - glyphHeight;
    }
}

bool IGxuFontGlyphRenderGlyph(FT_Face face, uint32_t pixelSize, uint32_t code, uint32_t baseline, GLYPHBITMAPDATA* dataPtr, int32_t monochrome, uint32_t padding, int32_t allowHinting) {
    if (!face || !pixelSize || !dataPtr) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return false;
    }

    if (!FT_Get_Char_Index(face, code)) {
        return false;
    }

    if (!FREETYPE_LoadGlyph(face, code, monochrome != 0, allowHinting)) {
        return false;
    }

    // MODIFIED: sample the vector outline while it is still present.
    OutlinePoly poly;
    bool haveOutline = false;

    if (!monochrome) {
        haveOutline = FlattenOutline(face, &poly);
    }

    if (!FREETYPE_RenderLoadedGlyph(face, monochrome != 0)) {
        return false;
    }

    uint32_t width = face->glyph->bitmap.width;
    uint32_t height = pixelSize >= static_cast<uint32_t>(face->glyph->bitmap.rows) ? face->glyph->bitmap.rows : pixelSize;
    int32_t pitch = face->glyph->bitmap.pitch;
    uint32_t size = pitch * face->glyph->bitmap.rows;
    const uint8_t* source = face->glyph->bitmap.buffer;
    uint32_t dataPitch = pitch;
    int32_t placeholder = 0;

    if (!width || !height || !source || !pitch || !size) {
        width = (pixelSize + 3) >> 2;
        height = pixelSize;

        if (!width) {
            width = pixelSize;
        }

        if (monochrome) {
            dataPitch = (width + 7) & ~7;
        } else {
            dataPitch = width;
        }

        size = pixelSize * dataPitch;
        placeholder = 1;
    }

    void* data = SMemAlloc(size, __FILE__, __LINE__, 0);

    if (data) {
        memset(data, 0, size);
    }

    if (source) {
        memcpy(data, source, size);
    }

    dataPtr->m_glyphCellWidth = width + padding;
    dataPtr->m_dataSize = size;
    dataPtr->m_glyphPitch = dataPitch;
    dataPtr->m_data = data;
    dataPtr->m_glyphWidth = width;
    dataPtr->m_glyphHeight = height;
    dataPtr->m_glyphAdvance = (face->glyph->metrics.horiAdvance / 64) + 1.0;
    dataPtr->m_yOffset = 0;
    dataPtr->m_yStart = 0;
    dataPtr->m_glyphBearing = face->glyph->metrics.horiBearingX * (1.0 / 64.0);

    if (width && data && !placeholder) {
        CalculateYOffset(pixelSize, baseline, face, height, &dataPtr->m_yOffset, &dataPtr->m_yStart);
    }

    // MODIFIED: sample the whole atlas cell, not just the ink rectangle. The
    // outline is analytic, so texels outside the glyph still get a real distance
    // and the field keeps decaying past the edge instead of stopping dead at the
    // last ink column. That dead stop is what looked like a cut line on the right
    // of 8 and 6, and like a chipped tip on the apex of M.
    if (haveOutline && width && height && !placeholder && dataPtr->m_glyphCellWidth && pixelSize) {
        const uint32_t fieldW = dataPtr->m_glyphCellWidth;
        const uint32_t fieldH = pixelSize;

        if (fieldW <= 256 && fieldH <= 64) {
            const bool useMsdf = false;
            const uint32_t channels = useMsdf ? 3 : 1;
            const size_t sdfSize = static_cast<size_t>(fieldW) * fieldH * channels;
            uint8_t* sdf = static_cast<uint8_t*>(SMemAlloc(sdfSize, __FILE__, __LINE__, 0));

            if (sdf) {
                // Cell row 0 sits yStart rows above the bitmap's top edge.
                const float originX = static_cast<float>(face->glyph->bitmap_left);
                const float originY = static_cast<float>(face->glyph->bitmap_top)
                                    + static_cast<float>(dataPtr->m_yStart);

                const int pointCount = static_cast<int>(poly.m_points.size() / 2);
                const int contourCount = static_cast<int>(poly.m_contourStart.size());

                const bool built = useMsdf
                    ? GxuFontSdf::BuildMSDFFromPolygons(poly.m_points.data(), pointCount,
                        poly.m_contourStart.data(), contourCount, originX, originY, fieldW, fieldH, sdf)
                    : GxuFontSdf::BuildFromPolygons(poly.m_points.data(), pointCount,
                        poly.m_contourStart.data(), contourCount, originX, originY, fieldW, fieldH, sdf);

                if (built) {
                    dataPtr->m_sdfData = sdf;
                    dataPtr->m_sdfChannels = channels;
                    dataPtr->m_sdfWidth = fieldW;
                    dataPtr->m_sdfHeight = fieldH;
                } else {
                    SMemFree(sdf, __FILE__, __LINE__, 0);
                }
            }
        }
    }

    return true;
}

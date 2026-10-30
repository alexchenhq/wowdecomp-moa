#include <cmath>
#include <cstring>
#include <freetype/ftoutln.h>
#include "Engine/Source/Gxu/GxuFontSlug.h"

namespace {

// FreeType hands back 26.6 fixed point and draws counter-clockwise contours.
// We keep the raw segments and convert to em at the end.
struct OutlineCollector {
    std::vector<SLUGCURVE> m_curves;
    float m_current[2];
    bool m_haveCurrent;
    float m_scale;   // 26.6 units -> em

    OutlineCollector() : m_haveCurrent(false), m_scale(0.0f) {
        m_current[0] = 0.0f;
        m_current[1] = 0.0f;
    }

    static float ToEm(int32_t fixed, float scale) {
        return static_cast<float>(fixed) * (1.0f / 64.0f) * scale;
    }

    void Push(float p1x, float p1y, float p2x, float p2y) {
        SLUGCURVE curve;
        curve.m_p0[0] = m_current[0];
        curve.m_p0[1] = m_current[1];
        curve.m_p1[0] = p1x;
        curve.m_p1[1] = p1y;
        curve.m_p2[0] = p2x;
        curve.m_p2[1] = p2y;
        m_curves.push_back(curve);

        m_current[0] = p2x;
        m_current[1] = p2y;
    }
};

int OutlineMoveTo(FT_Vector* to, void* user) {
    OutlineCollector* collector = static_cast<OutlineCollector*>(user);

    if (!collector) {
        return 0;
    }

    collector->m_current[0] = OutlineCollector::ToEm(to->x, collector->m_scale);
    collector->m_current[1] = OutlineCollector::ToEm(to->y, collector->m_scale);
    collector->m_haveCurrent = true;

    return 0;
}

int OutlineLineTo(FT_Vector* to, void* user) {
    OutlineCollector* collector = static_cast<OutlineCollector*>(user);

    if (!collector || !collector->m_haveCurrent) {
        return 0;
    }

    // A straight edge is a quadratic whose control point is the midpoint.
    const float ex = OutlineCollector::ToEm(to->x, collector->m_scale);
    const float ey = OutlineCollector::ToEm(to->y, collector->m_scale);
    const float cx = (collector->m_current[0] + ex) * 0.5f;
    const float cy = (collector->m_current[1] + ey) * 0.5f;

    collector->Push(cx, cy, ex, ey);

    return 0;
}

int OutlineConicTo(FT_Vector* control, FT_Vector* to, void* user) {
    OutlineCollector* collector = static_cast<OutlineCollector*>(user);

    if (!collector || !collector->m_haveCurrent) {
        return 0;
    }

    const float cx = OutlineCollector::ToEm(control->x, collector->m_scale);
    const float cy = OutlineCollector::ToEm(control->y, collector->m_scale);
    const float ex = OutlineCollector::ToEm(to->x, collector->m_scale);
    const float ey = OutlineCollector::ToEm(to->y, collector->m_scale);

    collector->Push(cx, cy, ex, ey);

    return 0;
}

int OutlineCubicTo(FT_Vector* control1, FT_Vector* control2, FT_Vector* to, void* user) {
    OutlineCollector* collector = static_cast<OutlineCollector*>(user);

    if (!collector || !collector->m_haveCurrent) {
        return 0;
    }

    // Slug works on quadratics. Split the cubic into two quadratics with the
    // standard midpoint approximation; the error is well under a quarter texel at
    // the sizes we rasterise.
    const float x0 = collector->m_current[0];
    const float y0 = collector->m_current[1];
    const float x1 = OutlineCollector::ToEm(control1->x, collector->m_scale);
    const float y1 = OutlineCollector::ToEm(control1->y, collector->m_scale);
    const float x2 = OutlineCollector::ToEm(control2->x, collector->m_scale);
    const float y2 = OutlineCollector::ToEm(control2->y, collector->m_scale);
    const float x3 = OutlineCollector::ToEm(to->x, collector->m_scale);
    const float y3 = OutlineCollector::ToEm(to->y, collector->m_scale);

    const float midX = (x1 + x2) * 0.5f;
    const float midY = (y1 + y2) * 0.5f;

    // first half: p0 -> control (p0+p1)/2 -> midpoint of the cubic at t=0.5
    const float halfX = (x0 + 3.0f * x1 + 3.0f * x2 + x3) * 0.125f;
    const float halfY = (y0 + 3.0f * y1 + 3.0f * y2 + y3) * 0.125f;

    collector->Push((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, halfX, halfY);
    collector->Push((x2 + x3) * 0.5f, (y2 + y3) * 0.5f, x3, y3);

    // Push already advanced the current point to the second half's end.
    (void)midX;
    (void)midY;

    return 0;
}

// Minimal IEEE-754 single -> half conversion (round to nearest even is not
// required here; the values are control points in a normal range).
uint16_t FloatToHalf(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));

    const uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFF) - 127;
    uint32_t mantissa = bits & 0x7FFFFFu;

    if (exponent > 15) {
        return static_cast<uint16_t>(sign | 0x7C00u); // clamp to infinity
    }

    if (exponent < -14) {
        return static_cast<uint16_t>(sign);           // flush tiny values to zero
    }

    return static_cast<uint16_t>(sign | ((exponent + 15) << 10) | (mantissa >> 13));
}

} // namespace

bool GxuFontSlugExtractGlyph(FT_Face face, uint32_t code, uint32_t pixelSize, SLUGGLYPH* out) {
    if (!face || !out || !pixelSize) {
        return false;
    }

    const FT_UInt index = FT_Get_Char_Index(face, code);

    if (!index) {
        return false;
    }

    if (FT_Load_Glyph(face, index, FT_LOAD_LINEAR_DESIGN | FT_LOAD_PEDANTIC | FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0) {
        return false;
    }

    if (face->glyph->format != ft_glyph_format_outline || !face->glyph->outline.n_contours) {
        return false;
    }

    OutlineCollector collector;
    collector.m_scale = 1.0f / static_cast<float>(pixelSize);

    FT_Outline_Funcs functions;
    memset(&functions, 0, sizeof(functions));
    functions.move_to = OutlineMoveTo;
    functions.line_to = OutlineLineTo;
    functions.conic_to = OutlineConicTo;
    functions.cubic_to = OutlineCubicTo;

    if (FT_Outline_Decompose(&face->glyph->outline, &functions, &collector) != 0) {
        return false;
    }

    if (collector.m_curves.empty()) {
        return false;
    }

    out->m_curves.swap(collector.m_curves);

    out->m_minX = out->m_maxX = out->m_curves[0].m_p0[0];
    out->m_minY = out->m_maxY = out->m_curves[0].m_p0[1];

    for (const SLUGCURVE& curve : out->m_curves) {
        // Control points are included on purpose: the curve stays inside their hull.
        const float xs[3] = { curve.m_p0[0], curve.m_p1[0], curve.m_p2[0] };
        const float ys[3] = { curve.m_p0[1], curve.m_p1[1], curve.m_p2[1] };

        for (int k = 0; k < 3; k++) {
            if (xs[k] < out->m_minX) out->m_minX = xs[k];
            if (xs[k] > out->m_maxX) out->m_maxX = xs[k];
            if (ys[k] < out->m_minY) out->m_minY = ys[k];
            if (ys[k] > out->m_maxY) out->m_maxY = ys[k];
        }
    }

    return true;
}

uint32_t GxuFontSlugCurveTexels(const SLUGGLYPH& glyph) {
    // Gr1616F stores two halves per texel, so one curve takes three texels:
    // (p0.x, p0.y), (p1.x, p1.y), (p2.x, p2.y).
    return static_cast<uint32_t>(glyph.m_curves.size()) * 3;
}

uint32_t GxuFontSlugPackCurves(const SLUGGLYPH& glyph, uint16_t* dest, uint32_t texelCapacity) {
    if (!dest) {
        return 0;
    }

    const uint32_t needed = GxuFontSlugCurveTexels(glyph);

    if (!needed || needed > texelCapacity) {
        return 0;
    }

    for (size_t i = 0; i < glyph.m_curves.size(); i++) {
        const SLUGCURVE& curve = glyph.m_curves[i];
        uint16_t* texel = dest + i * 6;

        texel[0] = FloatToHalf(curve.m_p0[0]);
        texel[1] = FloatToHalf(curve.m_p0[1]);

        texel[2] = FloatToHalf(curve.m_p1[0]);
        texel[3] = FloatToHalf(curve.m_p1[1]);

        texel[4] = FloatToHalf(curve.m_p2[0]);
        texel[5] = FloatToHalf(curve.m_p2[1]);
    }

    return needed;
}

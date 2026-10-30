// Analytic glyph coverage from quadratic Bezier outlines (Slug idea, reference
// shaders by Eric Lengyel: MIT licence, patent dedicated to the public domain).
//
// One curve per three texels of a Gr1616F page: (p0.xy) (p1.xy) (p2.xy), em
// space with y up. For every pixel we count how often the outline crosses the
// horizontal ray going right from the pixel, accumulating a winding number. Inside
// is "winding is not zero", which is what TrueType fills by.
//
// ps_3_0 notes:
//   tex2Dlod is mandatory, tex2D needs gradients and then the loop cannot stay
//   dynamic (the compiler unrolls it and the shader stops fitting).
//   No integer or bitwise maths either, so everything is float.

sampler2D CurveTex : register(s0);

// (firstCurveTexel, curveCount, 1/texWidth, 1/texHeight)
float4 SlugParams : register(c0);

float CurveX(float2 p0, float2 p1, float2 p2, float t) {
    float mt = 1.0 - t;
    return mt * mt * p0.x + 2.0 * mt * t * p1.x + t * t * p2.x;
}

// +1 for an upward crossing, -1 for a downward one.
float CurveWinding(float2 p0, float2 p1, float2 p2, float t) {
    float dy = 2.0 * (1.0 - t) * (p1.y - p0.y) + 2.0 * t * (p2.y - p1.y);
    return dy >= 0.0 ? 1.0 : -1.0;
}

float4 main(float2 em : TEXCOORD0, float4 color : COLOR0) : COLOR0 {
    float base = SlugParams.x;
    float count = SlugParams.y;
    float invWidth = SlugParams.z;
    float rowV = 0.5 * SlugParams.w;

    // Pixel size in em units, taken once outside the loop: gradients cannot live
    // inside a loop with break, and this is only needed for the edge softening.
    float pxSize = max(length(ddx(em)), length(ddy(em)));
    pxSize = max(pxSize, 1e-6);
    float invHalfPixel = 2.0 / pxSize;

    // Signed, clamped distance accumulation. For an edge crossing the pixel this
    // reproduces the exact covered fraction (verified offline against analytic
    // coverage), and it fades instead of hard switching at the boundary.
    float acc = 0.0;

    for (int i = 0; i < 256; i++) {
        if (float(i) >= count) {
            break;
        }

        float texel = base + float(i) * 3.0;
        float2 p0 = tex2Dlod(CurveTex, float4((texel + 0.5) * invWidth, rowV, 0.0, 0.0)).xy;
        float2 p1 = tex2Dlod(CurveTex, float4((texel + 1.5) * invWidth, rowV, 0.0, 0.0)).xy;
        float2 p2 = tex2Dlod(CurveTex, float4((texel + 2.5) * invWidth, rowV, 0.0, 0.0)).xy;

#if SLUG_SHOW_CURVE
        if (i == 0) {
            // R = p0.x, G = p0.y, B = p1.x. CPU reports p0(0.285, 0.724) p1.x 0.379.
            return float4(p0.x, p0.y, p1.x, 1.0);
        }
#endif

        float a = p0.y - 2.0 * p1.y + p2.y;
        float b = 2.0 * (p1.y - p0.y);
        float c = p0.y - em.y;

        if (abs(a) < 1e-6) {
            // Degenerate quadratic: a straight edge.
            if (abs(b) > 1e-6) {
                float t = -c / b;

                if (t >= 0.0 && t < 1.0) {
                    float x = CurveX(p0, p1, p2, t);
                    acc += CurveWinding(p0, p1, p2, t) * clamp((x - em.x) * invHalfPixel, -1.0, 1.0);
                }
            }
        } else {
            float disc = b * b - 4.0 * a * c;

            // Root eligibility, the part of Slug that is easy to get wrong: a
            // tangent touch (disc near zero) must contribute nothing, and shared
            // endpoints between neighbouring curves must be counted exactly once,
            // hence the half open interval [0, 1).
            if (disc > 1e-6) {
                float sq = sqrt(disc);
                float inv2a = 0.5 / a;

                float r0 = (-b + sq) * inv2a;
                float r1 = (-b - sq) * inv2a;

                if (r0 >= 0.0 && r0 < 1.0) {
                    float x0 = CurveX(p0, p1, p2, r0);
                    acc += CurveWinding(p0, p1, p2, r0) * clamp((x0 - em.x) * invHalfPixel, -1.0, 1.0);
                }

                if (r1 >= 0.0 && r1 < 1.0) {
                    float x1 = CurveX(p0, p1, p2, r1);
                    acc += CurveWinding(p0, p1, p2, r1) * clamp((x1 - em.x) * invHalfPixel, -1.0, 1.0);
                }
            }
        }
    }

    float inside = saturate(abs(acc) * 0.5);

    return float4(color.rgb, color.a * inside);
}

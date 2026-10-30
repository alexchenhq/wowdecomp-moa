// Analytic glyph coverage from quadratic Bezier outlines (Slug idea; reference
// shaders by Eric Lengyel, MIT licence, patent dedicated to the public domain).
//
// Layout: one curve per three texels of a Gr1616F page, (p0.xy) (p1.xy) (p2.xy),
// em space with y up. Coverage comes from casting a horizontal ray per pixel,
// accumulating a signed winding number, and turning those crossings into partial
// coverage so edges fade instead of stepping.
//
// ps_3_0 constraints, learned the hard way:
//   - tex2Dlod is mandatory. tex2D needs gradients, gradients cannot live in a
//     loop with break, the compiler then unrolls everything and the shader no
//     longer fits in 512 slots.
//   - No integer or bitwise maths.
//   - ddx/ddy may only be taken outside the loop.

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

// One scanline slice: how much of the pixel this curve's crossing covers, as a
// signed distance clamped to the pixel width. Exact for edges perpendicular to the
// ray, a close approximation otherwise.
float SliceAcc(float2 p0, float2 p1, float2 p2, float px, float py, float invHalfPixel) {
    float a = p0.y - 2.0 * p1.y + p2.y;
    float b = 2.0 * (p1.y - p0.y);
    float c = p0.y - py;
    float acc = 0.0;

    if (abs(a) < 1e-6) {
        if (abs(b) > 1e-6) {
            float t = -c / b;

            if (t >= 0.0 && t < 1.0) {
                acc += CurveWinding(p0, p1, p2, t) * clamp((CurveX(p0, p1, p2, t) - px) * invHalfPixel, -1.0, 1.0);
            }
        }
    } else {
        float disc = b * b - 4.0 * a * c;

        // Root eligibility: a tangent touch must contribute nothing, and an endpoint
        // shared by two neighbouring curves must count once, hence the half open
        // interval.
        if (disc > 1e-6) {
            float sq = sqrt(disc);
            float inv2a = 0.5 / a;
            float r0 = (-b + sq) * inv2a;
            float r1 = (-b - sq) * inv2a;

            if (r0 >= 0.0 && r0 < 1.0) {
                acc += CurveWinding(p0, p1, p2, r0) * clamp((CurveX(p0, p1, p2, r0) - px) * invHalfPixel, -1.0, 1.0);
            }

            if (r1 >= 0.0 && r1 < 1.0) {
                acc += CurveWinding(p0, p1, p2, r1) * clamp((CurveX(p0, p1, p2, r1) - px) * invHalfPixel, -1.0, 1.0);
            }
        }
    }

    return acc;
}

float4 main(float2 em : TEXCOORD0, float4 color : COLOR0) : COLOR0 {
    float base = SlugParams.x;
    float count = SlugParams.y;
    float invWidth = SlugParams.z;
    float rowV = 0.5 * SlugParams.w;

    // Pixel footprint in em units, taken once outside the loop.
    float pxSize = max(length(ddx(em)), length(ddy(em)));
    pxSize = max(pxSize, 1e-6);
    float invHalfPixel = 2.0 / pxSize;

    // Four slices inside the pixel. A single centre slice leaves a staircase on
    // slanted strokes; averaging rows is what removes it.
    float py0 = em.y - 0.375 * pxSize;
    float py1 = em.y - 0.125 * pxSize;
    float py2 = em.y + 0.125 * pxSize;
    float py3 = em.y + 0.375 * pxSize;

    float acc = 0.0;

    for (int i = 0; i < 256; i++) {
        if (float(i) >= count) {
            break;
        }

        float texel = base + float(i) * 3.0;
        float2 p0 = tex2Dlod(CurveTex, float4((texel + 0.5) * invWidth, rowV, 0.0, 0.0)).xy;
        float2 p1 = tex2Dlod(CurveTex, float4((texel + 1.5) * invWidth, rowV, 0.0, 0.0)).xy;
        float2 p2 = tex2Dlod(CurveTex, float4((texel + 2.5) * invWidth, rowV, 0.0, 0.0)).xy;

        acc += SliceAcc(p0, p1, p2, em.x, py0, invHalfPixel);
        acc += SliceAcc(p0, p1, p2, em.x, py1, invHalfPixel);
        acc += SliceAcc(p0, p1, p2, em.x, py2, invHalfPixel);
        acc += SliceAcc(p0, p1, p2, em.x, py3, invHalfPixel);
    }

    // Four slices averaged, then the signed sum turned into coverage.
    float inside = saturate(abs(acc * 0.25) * 0.5);

    return float4(color.rgb, color.a * inside);
}

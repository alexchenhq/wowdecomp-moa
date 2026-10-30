// Analytic glyph coverage from quadratic Bezier outlines (Slug idea; reference
// shaders by Eric Lengyel, MIT licence, patent dedicated to the public domain).
//
// Layout: one curve per four texels of a Gr1616F page, (p0.xy) (p1.xy) (p2.xy)
// (minY, maxY), em space with y up. Coverage comes from casting a horizontal ray
// per pixel, accumulating a signed winding number, and turning those crossings
// into partial coverage so edges fade instead of stepping.
//
// Where the glyph lives used to arrive in c0, which meant one draw per glyph.
// The base texel and the curve count now come from the vertex (TEXCOORD1), so a
// whole string can be drawn in one batch; only the page size stays constant.
//
// ps_3_0 constraints, learned the hard way:
//   - tex2Dlod is mandatory. tex2D needs gradients, gradients cannot live in a
//     loop with break, the compiler then unrolls everything and the shader no
//     longer fits in 512 slots.
//   - No integer or bitwise maths.
//   - ddx/ddy may only be taken outside the loop.

sampler2D CurveTex : register(s0);

// (1/texWidth, 1/texHeight)
float4 SlugPage : register(c0);

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

// Curve texel index -> texcoord on the page.
//
// The curves are packed as one linear run of texels, so the index has to be
// folded back into (column, row) here: past one row the raw index is longer than
// a row and the fetch would land on whatever wraps around. A curve whose four
// texels straddle a row boundary still addresses correctly because every texel
// goes through this.
float2 CurveUV(float texel, float invWidth, float invHeight) {
    float t = texel * invWidth;
    float row = floor(t);

    return float2(t - row + 0.5 * invWidth, row * invHeight + 0.5 * invHeight);
}

float4 main(float2 em : TEXCOORD0, float2 slug : TEXCOORD1, float4 color : COLOR0) : COLOR0 {
    // Per glyph, straight from the vertex: where its curves start and how many.
    float base = slug.x;
    float count = slug.y;

    float invWidth = SlugPage.x;
    float invHeight = SlugPage.y;

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

        // Four texels per curve; the fourth is the segment's vertical extent.
        float texel = base + float(i) * 4.0;
        float2 span = tex2Dlod(CurveTex, float4(CurveUV(texel + 3.0, invWidth, invHeight), 0.0, 0.0)).xy;

        // Cull by y range first: one fetch instead of three plus the root solve.
        // Most segments are nowhere near any given pixel.
        if (py0 <= span.y && py3 >= span.x) {
            float2 p0 = tex2Dlod(CurveTex, float4(CurveUV(texel + 0.0, invWidth, invHeight), 0.0, 0.0)).xy;
            float2 p1 = tex2Dlod(CurveTex, float4(CurveUV(texel + 1.0, invWidth, invHeight), 0.0, 0.0)).xy;
            float2 p2 = tex2Dlod(CurveTex, float4(CurveUV(texel + 2.0, invWidth, invHeight), 0.0, 0.0)).xy;

            acc += SliceAcc(p0, p1, p2, em.x, py0, invHalfPixel);
            acc += SliceAcc(p0, p1, p2, em.x, py1, invHalfPixel);
            acc += SliceAcc(p0, p1, p2, em.x, py2, invHalfPixel);
            acc += SliceAcc(p0, p1, p2, em.x, py3, invHalfPixel);
        }
    }

    // Four slices averaged, then the signed sum turned into coverage.
    float inside = saturate(abs(acc * 0.25) * 0.5);

    // No outline here. Growing one from this coverage looked possible - it is a
    // one pixel ramp, so its gradient is a distance - but the ramp only carries
    // distance inside half a pixel, and a rim is one to two pixels out. Measured
    // at every size, the outer edge came out as a hard threshold, so outlined
    // faces are kept on the distance field instead (see GxuFontSlugFaceEnabled).
    return float4(color.rgb, color.a * inside);
}

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

#if RIM

// (width in pixels, 0, 0, 0) and the outline colour, same meaning as the
// distance field path uses.
float4 RimParams : register(c1);
float4 RimColor : register(c2);

float SegmentDistance(float2 p, float2 a, float2 b) {
    float2 ab = b - a;
    float denom = max(dot(ab, ab), 1e-12);
    float t = saturate(dot(p - a, ab) / denom);

    return length(p - (a + ab * t));
}

// Distance from the point to one quadratic.
//
// Two Newton steps on |B(t) - p|^2, started from the projection onto the chord.
// A polyline was the obvious alternative but it is the expensive one here: eight
// segments cost about three times what this costs and still under-report the
// distance by up to 0.03 of a pixel, because a chord sits inside a convex curve.
// The two endpoint distances are taken as well, so a closest point that falls
// outside the segment still gives the right answer.
float CurveDistance(float2 p, float2 p0, float2 p1, float2 p2) {
    float2 a = p0 - 2.0 * p1 + p2;   // t^2 coefficient
    float2 b = 2.0 * (p1 - p0);      // t coefficient
    float2 chord = p2 - p0;

    float t = saturate(dot(p - p0, chord) / max(dot(chord, chord), 1e-12));

    [unroll]
    for (int k = 0; k < 2; k++) {
        float2 q = a * (t * t) + b * t + p0;
        float2 dq = a * (2.0 * t) + b;
        float2 e = q - p;

        t = saturate(t - dot(e, dq) / max(dot(dq, dq) + dot(e, a * 2.0), 1e-9));
    }

    float2 closest = a * (t * t) + b * t + p0;

    float dist = length(closest - p);
    dist = min(dist, length(p - p0));
    dist = min(dist, length(p - p2));

    return dist;
}

#endif

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

    // Three scanlines inside the pixel. A single centre slice leaves a staircase
    // on slanted strokes; averaging rows is what removes it. Four was the number
    // here for a long time; three at a third of a pixel either side measures the
    // same against a supersampled reference - 0.003 to 0.014 RMS, against 0.003
    // to 0.017 for four - and costs a quarter less.
    float py0 = em.y - 0.3333333 * pxSize;
    float py1 = em.y;
    float py2 = em.y + 0.3333333 * pxSize;
    const float sliceScale = 1.0 / 3.0;

    float acc = 0.0;
    float sliceHigh;

#if RIM
    // The rim reaches further than the slices, so the loop has to consider a
    // wider band of curves than the coverage does. Anything farther than the
    // rim plus a pixel cannot change the result.
    float reach = (RimParams.x + 1.0) * pxSize;
    float cullLo = min(py0, em.y - reach);
    float cullHi = max(py2, em.y + reach);
    float nearest = reach;
    sliceHigh = py2;
#else
    float cullLo = py0;
    float cullHi = py2;
    sliceHigh = py2;
#endif

    for (int i = 0; i < 256; i++) {
        if (float(i) >= count) {
            break;
        }

        // Four texels per curve; the fourth is the segment's vertical extent.
        float texel = base + float(i) * 4.0;
        float2 span = tex2Dlod(CurveTex, float4(CurveUV(texel + 3.0, invWidth, invHeight), 0.0, 0.0)).xy;

        // Cull by y range first: one fetch instead of three plus the root solve.
        // Most segments are nowhere near any given pixel.
        if (cullLo <= span.y && cullHi >= span.x) {
            float2 p0 = tex2Dlod(CurveTex, float4(CurveUV(texel + 0.0, invWidth, invHeight), 0.0, 0.0)).xy;
            float2 p1 = tex2Dlod(CurveTex, float4(CurveUV(texel + 1.0, invWidth, invHeight), 0.0, 0.0)).xy;
            float2 p2 = tex2Dlod(CurveTex, float4(CurveUV(texel + 2.0, invWidth, invHeight), 0.0, 0.0)).xy;

            // Coverage only cares about curves that reach the pixel's own rows.
            if (py0 <= span.y && sliceHigh >= span.x) {
                acc += SliceAcc(p0, p1, p2, em.x, py0, invHalfPixel);
                acc += SliceAcc(p0, p1, p2, em.x, py1, invHalfPixel);
                acc += SliceAcc(p0, p1, p2, em.x, py2, invHalfPixel);
            }

#if RIM
            float2 lo = min(p0, min(p1, p2));
            float2 hi = max(p0, max(p1, p2));

            if (em.x >= lo.x - reach && em.x <= hi.x + reach) {
                nearest = min(nearest, CurveDistance(em, p0, p1, p2));
            }
#endif
        }
    }

    // Slices averaged, then the signed sum turned into coverage.
    float inside = saturate(abs(acc * sliceScale) * 0.5);

#if RIM
    // One pass, one surface, composed the way the distance field path does it:
    // the coverage says which side of the edge this pixel is on, the distance
    // says how far out, and the two are combined with a max rather than by
    // stacking layers. Stacking was what made the stroke look eaten: the rim sat
    // under the body and showed through its antialiased edge.
    float side = inside >= 0.5 ? 1.0 : -1.0;
    float dist = side * nearest;

    float rim = saturate(dist / pxSize + RimParams.x + 0.5);
    float alpha = max(inside * color.a, rim * RimColor.a);
    float3 rgb = lerp(RimColor.rgb, color.rgb, inside);

    return float4(rgb, alpha);
#else
    return float4(color.rgb, color.a * inside);
#endif
}

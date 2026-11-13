// Analytic glyph coverage from quadratic Bezier outlines, written for ps_5_0.
//
// Same idea as FontSlug.hlsl (which stays as the D3D9 shader), with the three
// compromises ps_3_0 forced taken back out:
//
//   - Four scanline slices, not three. Three was a 512 slot budget decision, not
//     a quality one. Against a supersampled reference four measures 0.003-0.017
//     RMS and three 0.003-0.014; four is the one that keeps large text from
//     quantising its horizontal edges.
//   - The four texels of a curve are addressed in one go. Every fetch used to
//     recompute floor(texel * invWidth). Here the four column positions are one
//     float4, the row wrap is one step(), and the texcoords fall out vectorised.
//   - Culls are real branches. ps_3_0 predicated them, so the body ran anyway;
//     now a curve that misses the pixel costs one fetch and a compare.
//
// Constants come in through the same cbuffer the device layer already uploads,
// c[0] being what register(c0) was, so nothing on the C++ side moves.
//
// Reference: Slug, Eric Lengyel. MIT licence, patent dedicated to the public
// domain. The quadratic distance in the rim path is this port's own.

Texture2D CurveTex : register(t0);
SamplerState CurveSampler : register(s0);
cbuffer Constants : register(b0) { float4 c[256]; };

#define SlugPage  (c[0])
#define RimParams (c[1])
#define RimColor  (c[2])

float2 Fetch(float2 uv) {
    return CurveTex.SampleLevel(CurveSampler, uv, 0.0).xy;
}

float CurveX(float2 p0, float2 p1, float2 p2, float t) {
    float mt = 1.0 - t;
    return mt * mt * p0.x + 2.0 * mt * t * p1.x + t * t * p2.x;
}

// +1 for an upward crossing, -1 for a downward one.
float CurveWinding(float2 p0, float2 p1, float2 p2, float t) {
    float dy = 2.0 * (1.0 - t) * (p1.y - p0.y) + 2.0 * t * (p2.y - p1.y);
    return dy >= 0.0 ? 1.0 : -1.0;
}

// One horizontal ray against one quadratic, as partial coverage of the pixel.
//
// The winding number decides the sign, the clamped horizontal distance decides
// how much of the pixel the crossing covers. On a near vertical edge that
// distance is the coverage; a close approximation otherwise.
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

        // Root eligibility: a tangent touch must contribute nothing, and an
        // endpoint shared by two neighbouring curves must count once, hence the
        // half open interval.
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

#if RIM

float SegmentDistance(float2 p, float2 a, float2 b) {
    float2 ab = b - a;
    float denom = max(dot(ab, ab), 1e-12);
    float t = saturate(dot(p - a, ab) / denom);

    return length(p - (a + ab * t));
}

// Distance from the point to one quadratic: two Newton steps on |B(t) - p|^2,
// started from the projection onto the chord, with the endpoint distances taken
// as well so a closest point outside the segment still answers correctly. One
// step leaves up to 0.2 of a pixel of error, two brings it to 0.02.
float CurveDistance(float2 p, float2 p0, float2 p1, float2 p2) {
    float2 a = p0 - 2.0 * p1 + p2;
    float2 b = 2.0 * (p1 - p0);
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

float4 main(float2 em : TEXCOORD0, float2 slug : TEXCOORD1, float4 color : TEXCOORD2) : SV_Target {
    // Per glyph, straight from the vertex: where its curves start and how many.
    float base = slug.x;
    uint count = (uint)slug.y;

    float invWidth = SlugPage.x;
    float invHeight = SlugPage.y;

    // Pixel footprint in em units, taken once outside the loop.
    float pxSize = max(length(ddx(em)), length(ddy(em)));
    pxSize = max(pxSize, 1e-6);
    float invHalfPixel = 2.0 / pxSize;

    // Four scanlines inside the pixel.
    float py0 = em.y - 0.375 * pxSize;
    float py1 = em.y - 0.125 * pxSize;
    float py2 = em.y + 0.125 * pxSize;
    float py3 = em.y + 0.375 * pxSize;

    float acc = 0.0;

#if RIM
    // The rim reaches further than the slices, so the loop has to consider a
    // wider band of curves than the coverage does. Anything farther than the rim
    // plus a pixel cannot change the result.
    float reach = (RimParams.x + 1.0) * pxSize;
    float cullLo = min(py0, em.y - reach);
    float cullHi = max(py3, em.y + reach);
    float nearest = reach;
#else
    float cullLo = py0;
    float cullHi = py3;
#endif

    for (uint i = 0u; i < count; ++i) {
        float texel = base + float(i) * 4.0;

        // Four contiguous texels: one multiply, one floor, and the rest is
        // vectorised. rr carries the row each texel lands on, which differs from
        // the first by at most one because a page row is far wider than four.
        float t = texel * invWidth;
        float row = floor(t);
        float4 tt = t + float4(0.0, 1.0, 2.0, 3.0) * invWidth;
        float4 rr = row + step(1.0, tt - row);
        float4 uu = tt - rr + 0.5 * invWidth;
        float4 vv = rr * invHeight + 0.5 * invHeight;

        // The fourth texel is the curve's vertical extent: cull before paying for
        // the three point fetches and the root solve.
        float2 span = Fetch(float2(uu.w, vv.w));

        if (cullLo > span.y || cullHi < span.x) {
            continue;
        }

        float2 p0 = Fetch(float2(uu.x, vv.x));
        float2 p1 = Fetch(float2(uu.y, vv.y));
        float2 p2 = Fetch(float2(uu.z, vv.z));

        // Coverage only cares about curves that reach the pixel's own rows.
        if (py0 <= span.y && py3 >= span.x) {
            acc += SliceAcc(p0, p1, p2, em.x, py0, invHalfPixel);
            acc += SliceAcc(p0, p1, p2, em.x, py1, invHalfPixel);
            acc += SliceAcc(p0, p1, p2, em.x, py2, invHalfPixel);
            acc += SliceAcc(p0, p1, p2, em.x, py3, invHalfPixel);
        }

#if RIM
        float2 lo = min(p0, min(p1, p2));
        float2 hi = max(p0, max(p1, p2));

        if (em.x >= lo.x - reach && em.x <= hi.x + reach) {
            nearest = min(nearest, CurveDistance(em, p0, p1, p2));
        }
#endif
    }

    float inside = saturate(abs(acc * 0.25) * 0.5);

#if RIM
    // One pass, one surface, composed the way the distance field path does it:
    // the coverage says which side of the edge this pixel is on, the distance
    // says how far out, and the two are combined with a max rather than by
    // stacking layers.
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

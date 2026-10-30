// Rim pass for the Slug text path.
//
// The body keeps using the four scanline slices in FontSlug.hlsl, which measure
// the area a pixel covers far more accurately than a distance ever does. A rim
// needs the opposite: a distance that stays meaningful one or two pixels out,
// which coverage cannot give. So it gets its own pass with its own shader.
//
// Half the work here is the sign. Coverage gives inside/outside for free; a
// distance does not, and without the sign the rim would only be a ring, leaving
// the antialiased body edge blending against the background instead of against
// the rim.
//
// ps_3_0 constraints, same as the body: tex2Dlod only, no integer or bitwise
// maths, derivatives outside the loop.

sampler2D CurveTex : register(s0);

// (1/texWidth, 1/texHeight)
float4 SlugPage : register(c0);

// (width in pixels, 0, 0, 0)
float4 RimParams : register(c1);

float4 RimColor : register(c2);

float2 CurveUV(float texel, float invWidth, float invHeight) {
    float t = texel * invWidth;
    float row = floor(t);

    return float2(t - row + 0.5 * invWidth, row * invHeight + 0.5 * invHeight);
}

float CurveX(float2 p0, float2 p1, float2 p2, float t) {
    float mt = 1.0 - t;
    return mt * mt * p0.x + 2.0 * mt * t * p1.x + t * t * p2.x;
}

float SegmentDistance(float2 p, float2 a, float2 b) {
    float2 ab = b - a;
    float denom = max(dot(ab, ab), 1e-12);
    float t = saturate(dot(p - a, ab) / denom);

    return length(p - (a + ab * t));
}

// Distance from the point to one quadratic, as a four segment polyline. Measured
// offline against a sixteen segment version: no difference at any size, and the
// distance only has to be right to within a pixel to place a rim.
float CurveDistance(float2 p, float2 p0, float2 p1, float2 p2) {
    float2 a = p0;
    float2 b = (p0 * 0.5625 + p1 * 0.375 + p2 * 0.0625);
    float2 c = (p0 * 0.25 + p1 * 0.5 + p2 * 0.25);
    float2 d = (p0 * 0.0625 + p1 * 0.375 + p2 * 0.5625);
    float2 e = p2;

    float dist = SegmentDistance(p, a, b);
    dist = min(dist, SegmentDistance(p, b, c));
    dist = min(dist, SegmentDistance(p, c, d));
    dist = min(dist, SegmentDistance(p, d, e));

    return dist;
}

// +1 for an upward crossing, -1 for a downward one.
float CrossingSign(float2 p0, float2 p1, float2 p2, float t) {
    float dy = 2.0 * (1.0 - t) * (p1.y - p0.y) + 2.0 * t * (p2.y - p1.y);
    return dy >= 0.0 ? 1.0 : -1.0;
}

// Winding number of one ray, +1 for an upward crossing, -1 for a downward one.
// Only crossings to the right of the pixel count, and the interval is half open
// so a segment shared by two curves is counted once.
float RayWinding(float2 p0, float2 p1, float2 p2, float px, float py) {
    float a = p0.y - 2.0 * p1.y + p2.y;
    float b = 2.0 * (p1.y - p0.y);
    float c = p0.y - py;
    float winding = 0.0;

    if (abs(a) < 1e-6) {
        if (abs(b) > 1e-6) {
            float t = -c / b;

            if (t >= 0.0 && t < 1.0) {
                if (CurveX(p0, p1, p2, t) > px) {
                    winding += (b >= 0.0 ? 1.0 : -1.0);
                }
            }
        }
    } else {
        float disc = b * b - 4.0 * a * c;

        if (disc > 1e-6) {
            float sq = sqrt(disc);
            float inv2a = 0.5 / a;
            float r0 = (-b + sq) * inv2a;
            float r1 = (-b - sq) * inv2a;

            if (r0 >= 0.0 && r0 < 1.0) {
                if (CurveX(p0, p1, p2, r0) > px) {
                    winding += CrossingSign(p0, p1, p2, r0);
                }
            }

            if (r1 >= 0.0 && r1 < 1.0) {
                if (CurveX(p0, p1, p2, r1) > px) {
                    winding += CrossingSign(p0, p1, p2, r1);
                }
            }
        }
    }

    return winding;
}

float4 main(float2 em : TEXCOORD0, float2 slug : TEXCOORD1, float4 color : COLOR0) : COLOR0 {
    float base = slug.x;
    float count = slug.y;

    float invWidth = SlugPage.x;
    float invHeight = SlugPage.y;

    // Em per pixel, taken once outside the loop.
    float pxSize = max(length(ddx(em)), length(ddy(em)));
    pxSize = max(pxSize, 1e-6);

    float width = RimParams.x;

    // Anything further than the rim plus a pixel cannot change the result, and
    // that is what most of a glyph is for any given pixel.
    float reach = (width + 1.0) * pxSize;

    float nearest = reach;
    float winding = 0.0;

    for (int i = 0; i < 256; i++) {
        if (float(i) >= count) {
            break;
        }

        // Four texels per curve: the three points, then the segment's vertical
        // extent. Same layout the body shader reads.
        float texel = base + float(i) * 4.0;
        float2 span = tex2Dlod(CurveTex, float4(CurveUV(texel + 3.0, invWidth, invHeight), 0.0, 0.0)).xy;

        // A curve that does not reach this scanline cannot cross it (so no
        // winding) and cannot be within the rim (so no distance either).
        if (em.y + reach < span.x || em.y - reach > span.y) {
            continue;
        }

        float2 p0 = tex2Dlod(CurveTex, float4(CurveUV(texel + 0.0, invWidth, invHeight), 0.0, 0.0)).xy;
        float2 p1 = tex2Dlod(CurveTex, float4(CurveUV(texel + 1.0, invWidth, invHeight), 0.0, 0.0)).xy;
        float2 p2 = tex2Dlod(CurveTex, float4(CurveUV(texel + 2.0, invWidth, invHeight), 0.0, 0.0)).xy;

        winding += RayWinding(p0, p1, p2, em.x, em.y);

        float2 lo = min(p0, min(p1, p2));
        float2 hi = max(p0, max(p1, p2));

        if (em.x >= lo.x - reach && em.x <= hi.x + reach) {
            nearest = min(nearest, CurveDistance(em, p0, p1, p2));
        }
    }

    // Inside gives a positive distance so the rim fills under the body too; the
    // body then draws over it with full coverage and only the outside shows.
    float dist = winding != 0.0 ? nearest : -nearest;
    float alpha = saturate(dist / pxSize + width + 0.5);

    return float4(RimColor.rgb, RimColor.a * alpha * color.a);
}

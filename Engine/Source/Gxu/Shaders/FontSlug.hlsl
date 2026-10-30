// Analytic glyph coverage from quadratic Bezier outlines (Slug idea, reference
// shaders by Eric Lengyel, MIT / public domain patent).
//
// One texel pair describes one quadratic: (p0.xy, p1.xy) then (p2.xy, 0, 0),
// coordinates in em space with y up. For every pixel we solve y(t) = em.y for
// each curve, count how many crossings lie to the right, and use the parity as
// coverage. No atlas, no distance field, so the result is exact at any scale.
//
// ps_3_0 constraint: tex2Dlod must be used. tex2D needs gradients, gradients
// cannot live in a loop with break, and the compiler then unrolls everything
// (1738 slots). tex2Dlod keeps the loop dynamic (77 slots).

sampler2D CurveTex : register(s0);

// (firstCurveTexel, curveCount, 1/texWidth, 1/texHeight)
float4 SlugParams : register(c0);

// Diagnostic switch: 1 makes the probe draw a solid magenta box so the geometry
// and render state can be verified separately from the coverage math.
#define SLUG_PROBE_SOLID 0

// Diagnostic switch: 1 paints the first texel of the glyph so the curve texture
// can be inspected without any coverage maths.
#define SLUG_SHOW_CURVE 0

float4 main(float2 em : TEXCOORD0, float4 color : COLOR0) : COLOR0 {
#if SLUG_PROBE_SOLID
    return float4(1.0, 0.0, 1.0, 1.0);
#endif
    // ps_3_0 has no integer arithmetic or bitwise ops, so everything is float and
    // the parity is recovered with frac(x * 0.5).
    float base = SlugParams.x;
    float count = SlugParams.y;
    float invWidth = SlugParams.z;

    // Curves are packed along row 0, so sample the centre of that row. v = 0.5
    // would read the middle row of the page, which is unused.
    float rowV = 0.5 * SlugParams.w;

    float crossings = 0.0;

    for (int i = 0; i < 256; i++) {
        if (float(i) >= count) {
            break;
        }

        // Gr1616F: each texel carries two halves, so a curve spans three texels.
        float4 texelA = tex2Dlod(CurveTex, float4((base + float(i) * 3.0 + 0.5) * invWidth, rowV, 0.0, 0.0));
        float4 texelB = tex2Dlod(CurveTex, float4((base + float(i) * 3.0 + 1.5) * invWidth, rowV, 0.0, 0.0));
        float4 texelC = tex2Dlod(CurveTex, float4((base + float(i) * 3.0 + 2.5) * invWidth, rowV, 0.0, 0.0));

#if SLUG_SHOW_CURVE
        if (i == 0) {
            // p0.x in red, p0.y in green, p1.x in blue: em space is roughly -0.1..1.0
            return float4(saturate(texelA.x), saturate(texelA.y), saturate(texelA.z), 1.0);
        }
#endif

        float2 p0 = texelA.xy;
        float2 p1 = texelB.xy;
        float2 p2 = texelC.xy;

        float a = p0.y - 2.0 * p1.y + p2.y;
        float b = 2.0 * (p1.y - p0.y);
        float c = p0.y - em.y;

        float t = 0.0;
        bool hit = false;

        if (abs(a) < 1e-6) {
            if (abs(b) > 1e-6) {
                t = -c / b;
                hit = (t >= 0.0) && (t <= 1.0);
            }
        } else {
            float disc = b * b - 4.0 * a * c;
            if (disc >= 0.0) {
                float sq = sqrt(disc);
                float r0 = (-b + sq) / (2.0 * a);
                float r1 = (-b - sq) / (2.0 * a);

                if (r0 >= 0.0 && r0 <= 1.0) { t = r0; hit = true; }
                if (r1 >= 0.0 && r1 <= 1.0) { t = r1; hit = true; }
            }
        }

        if (hit) {
            float mt = 1.0 - t;
            float x = mt * mt * p0.x + 2.0 * mt * t * p1.x + t * t * p2.x;

            if (x > em.x) {
                crossings += 1.0;
            }
        }
    }

    // even-odd: a glyph outline has no self intersections, so parity is enough
    float inside = frac(crossings * 0.5) * 2.0;

    return float4(color.rgb, color.a * inside);
}

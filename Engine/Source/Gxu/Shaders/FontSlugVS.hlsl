// Vertex stage for the Slug text path.
//
// The UI vertex shader that ships in the game data was not written for this path
// and there is no way to ask it whether it forwards a second texture coordinate,
// so the batch brings its own: transform by the view-projection, pass the colour
// and both texcoord sets straight through. TEXCOORD1 is what carries the glyph's
// curve base and count, so it has to survive to the pixel shader.
//
// The four matrix rows arrive as four separate constants, in the same register
// layout the original shader expects: one row per register, one dp4 per output
// component. That avoids depending on how the engine packed a float4x4.

float4 Row0 : register(c0);
float4 Row1 : register(c1);
float4 Row2 : register(c2);
float4 Row3 : register(c3);

struct SLUGVSIN {
    float4 p : POSITION;
    float4 c : COLOR0;
    float2 t0 : TEXCOORD0;
    float2 t1 : TEXCOORD1;
};

struct SLUGVSOUT {
    float4 p : POSITION;
    float4 c : COLOR0;
    float2 t0 : TEXCOORD0;
    float2 t1 : TEXCOORD1;
};

SLUGVSOUT main(SLUGVSIN input) {
    SLUGVSOUT output;

    // Text geometry is already in screen space; only the projection is left.
    float4 p = float4(input.p.xyz, 1.0);

    output.p.x = dot(p, Row0);
    output.p.y = dot(p, Row1);
    output.p.z = dot(p, Row2);
    output.p.w = dot(p, Row3);

    output.c = input.c;
    output.t0 = input.t0;
    output.t1 = input.t1;

    return output;
}

// Vertex stage for the Slug text path, ps_5_0/vs_5_0 flavour.
//
// The D3D9 version has to put the four matrix rows in c0..c3 because SM3 has no
// constant buffers. Here the same four values come out of the cbuffer the device
// already uploads, and the interpolated colour moves off COLOR0: SM4+ has no
// colour registers, so it rides TEXCOORD2 and matches what the pixel shader
// declares.

cbuffer Constants : register(b0) { float4 c[256]; };

struct SLUGVSIN {
    float4 p : POSITION;
    float4 c : COLOR0;
    float2 t0 : TEXCOORD0;
    float2 t1 : TEXCOORD1;
};

struct SLUGVSOUT {
    float4 p : SV_Position;
    float4 c : TEXCOORD2;
    float2 t0 : TEXCOORD0;
    float2 t1 : TEXCOORD1;
};

SLUGVSOUT main(SLUGVSIN input) {
    SLUGVSOUT output;

    // Text geometry is already in screen space; only the projection is left.
    float4 p = float4(input.p.xyz, 1.0);

    output.p.x = dot(p, c[0]);
    output.p.y = dot(p, c[1]);
    output.p.z = dot(p, c[2]);
    output.p.w = dot(p, c[3]);

    output.c = input.c;
    output.t0 = input.t0;
    output.t1 = input.t1;

    return output;
}

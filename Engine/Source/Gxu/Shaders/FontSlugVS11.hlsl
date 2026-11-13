// Vertex stage for the Slug text path, ps_5_0/vs_5_0 flavour.
//
// The D3D9 version has to put the four matrix rows in c0..c3 because SM3 has no
// constant buffers. Here the same four values come out of the cbuffer the device
// already uploads, and the interpolated colour moves off COLOR0: SM4+ has no
// colour registers, so it rides TEXCOORD2 and matches what the pixel shader
// declares.

cbuffer Constants : register(b0) { float4 c[256]; };

struct SLUGVSIN {
    // Three floats, not four: the stream carries a C3Vector, and SM4+ input
    // layouts are strict about it where D3D9 quietly padded. A float4 here made
    // CreateInputLayout fail, and a failed layout draws nothing at all.
    float3 p : POSITION;
    float4 c : COLOR0;
    float2 t0 : TEXCOORD0;
    float2 t1 : TEXCOORD1;
};

// Everything the pixel shader needs rides TEXCOORD0 as one float4:
// xy = em coords, zw = (curve base texel, curve count). Keeping the glyph data
// in a single interpolator avoids depending on how TEXCOORD0/1 pack across the
// VS/PS signature link; FontSlug11.hlsl reads the same float4.
struct SLUGVSOUT {
    float4 p : SV_Position;
    float4 t0 : TEXCOORD0;
    float4 c : TEXCOORD2;
};

SLUGVSOUT main(SLUGVSIN input) {
    SLUGVSOUT output;

    // Text geometry is already in screen space; only the projection is left.
    float4 p = float4(input.p, 1.0);

    output.p.x = dot(p, c[0]);
    output.p.y = dot(p, c[1]);
    output.p.z = dot(p, c[2]);
    output.p.w = dot(p, c[3]);

    output.t0 = float4(input.t0, input.t1);
    output.c = input.c;

    return output;
}

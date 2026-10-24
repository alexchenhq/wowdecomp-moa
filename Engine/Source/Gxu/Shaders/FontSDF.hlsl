// Signed distance field font pixel shader (ps_3_0).
//
// The atlas stores a distance field of the GLYPH BODY ONLY. The outline is no
// longer baked into the texture as a solid rim; it is expanded analytically
// here, so a stroke keeps its intended width instead of merging into the body.
//
// The screen-space footprint of one texel is measured with ddx/ddy so the
// reconstruction stays correct at any uiScale.

sampler2D FontAtlas : register(s0);

// (1/atlasWidth, 1/atlasHeight, atlasWidth, atlasHeight)
float4 AtlasSize : register(c0);

// (outlineWidthInTexels, outlineAlpha, 0, 0)
float4 OutlineParams : register(c1);

// outline colour
float4 OutlineColor : register(c2);

float4 main(float2 uv : TEXCOORD0, float4 color : COLOR0) : COLOR0 {
    float dist = tex2D(FontAtlas, uv).a;

    // Texels covered by one screen pixel.
    float2 ddxTexel = ddx(uv / AtlasSize.xy);
    float2 ddyTexel = ddy(uv / AtlasSize.xy);
    float texelsPerPixel = max(length(ddxTexel), length(ddyTexel));

    // kSpread is 4.0 texels, so the encoded range spans 8 texels.
    float sd = (dist - 0.5) * 8.0;
    float px = max(texelsPerPixel, 1e-5);

    float body = saturate(sd / px + 0.5);

    if (OutlineParams.x <= 0.0) {
        return float4(color.rgb, color.a * body);
    }

    // Expand the shape outward by the outline width.
    float rim = saturate((sd + OutlineParams.x) / px + 0.5);

    float3 rgb = lerp(OutlineColor.rgb, color.rgb, body);
    float alpha = max(body * color.a, rim * OutlineParams.y);

    return float4(rgb, alpha);
}

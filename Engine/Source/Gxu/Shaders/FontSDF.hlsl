// Multi-channel signed distance field font pixel shader (ps_3_0).
//
// The atlas stores a three-channel MSDF in RGB: each channel holds the distance
// to one colour class of contour edges. Taking the median of the three keeps
// corners sharp, which a single-channel field cannot do.
//
// The outline is not baked into the texture; it is expanded analytically below.
// The screen-space footprint of one texel is measured with ddx/ddy so the
// reconstruction stays correct at any uiScale.

sampler2D FontAtlas : register(s0);

// (1/atlasWidth, 1/atlasHeight, atlasWidth, atlasHeight)
float4 AtlasSize : register(c0);

// (outlineWidthInTexels, outlineAlpha, 0, edgeSharpness)
float4 OutlineParams : register(c1);

// outline colour
float4 OutlineColor : register(c2);

float4 main(float2 uv : TEXCOORD0, float4 color : COLOR0) : COLOR0 {
    float3 msd = tex2D(FontAtlas, uv).rgb;

    // Median of three: max(min(r,g), min(max(r,g), b)).
    float lo = min(msd.r, msd.g);
    float hi = max(msd.r, msd.g);
    float mid = max(lo, min(hi, msd.b));

    // Texels covered by one screen pixel.
    float2 ddxTexel = ddx(uv / AtlasSize.xy);
    float2 ddyTexel = ddy(uv / AtlasSize.xy);
    float texelsPerPixel = max(length(ddxTexel), length(ddyTexel));

    // kSpread is 4.0 texels, so the encoded range spans 8 texels.
    float sd = (mid - 0.5) * 8.0;
    float px = max(texelsPerPixel, 1e-5);

    // Edge sharpness comes from FontSDF.cfg so it can be tuned without a
    // rebuild. Above 1.0 the ramp is narrower than a pixel (crisper, can alias);
    // below 1.0 it is softer.
    float sharpness = OutlineParams.w > 0.0 ? OutlineParams.w : 1.6;

    float body = saturate(sd / px * sharpness + 0.5);

    if (OutlineParams.x <= 0.0) {
        return float4(color.rgb, color.a * body);
    }

    // Expand the shape outward by the outline width.
    float rim = saturate((sd + OutlineParams.x) / px * sharpness + 0.5);

    float3 rgb = lerp(OutlineColor.rgb, color.rgb, body);
    float alpha = max(body * color.a, rim * OutlineParams.y);

    return float4(rgb, alpha);
}

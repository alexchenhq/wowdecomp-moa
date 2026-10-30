#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "Engine/Source/Gxu/GxuFontSlugCache.h"
#include "Engine/Source/Gxu/GxuFontSlug.h"
#include "Engine/Source/Gxu/GxuFontSlugShader.h"
#include "Engine/Source/Gxu/GxuFontMiscClasses.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/Gx/CGxVertexPCT.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Gxu/GxuFont.h"

namespace {

// The console the client opens only shows crash output, so diagnostics go to a
// file that can be read afterwards.
void SlugLog(const char* format, ...) {
    FILE* file = fopen("sluglab.log", "a");

    if (!file) {
        return;
    }

    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fputc('\n', file);
    fclose(file);
}

const uint32_t kSlugPageWidth = 256;
const uint32_t kSlugPageHeight = 256;
const uint32_t kSlugTexelBytes = 8;      // RGBA16F

// CPU side of the page, uploaded on demand by the latch callback.
uint16_t s_slugCurveData[kSlugPageWidth * kSlugPageHeight * 4];

CTexture* s_slugTexture = nullptr;
CGxTex* s_slugGxTex = nullptr;   // resolved device texture, what the state takes
CGxShader* s_slugShader = nullptr;

uint32_t s_slugCurveCount = 0;
bool s_slugReady = false;

float s_slugMinX = 0.0f;
float s_slugMinY = 0.0f;
float s_slugMaxX = 1.0f;
float s_slugMaxY = 1.0f;

void SlugTextureCallback(EGxTexCommand command, uint32_t width, uint32_t height,
                         uint32_t face, uint32_t level, void* userArg,
                         uint32_t& texelStrideInBytes, const void*& texels) {
    (void)userArg;
    (void)face;
    (void)level;

    SlugLog("[Slug] texcallback cmd=%d %ux%u face=%u level=%u", command, width, height, face, level);

    if (command != GxTex_Latch) {
        return;
    }

    texelStrideInBytes = width * kSlugTexelBytes;
    texels = s_slugCurveData;

    SlugLog("[Slug] latch served stride=%u raw texel0 = %04X %04X %04X %04X",
        texelStrideInBytes, s_slugCurveData[0], s_slugCurveData[1],
        s_slugCurveData[2], s_slugCurveData[3]);
}

CTexture* CreateSlugTexture() {
    CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1, 0, 0, 0);

    return TextureCreate(kSlugPageWidth, kSlugPageHeight, GxTex_Rgba16F, GxTex_Rgba16F,
        flags, nullptr, SlugTextureCallback, "GxuFontSlug", 0);
}

CGxShader* GetSlugShader() {
    if (s_slugShader) {
        return s_slugShader;
    }

    CGxShader* shader = new CGxShader;

    shader->m_target = GxSh_Pixel;
    shader->m_code.SetCount(g_gxuFontSlugPixelShaderSize);
    memcpy(shader->m_code.m_data, g_gxuFontSlugPixelShader, g_gxuFontSlugPixelShaderSize);
    shader->m_loaded = 0;

    if (!shader->Valid()) {
        SlugLog("[Slug] pixel shader FAILED to create");
        delete shader;
        return nullptr;
    }

    DebugLog("[Slug] pixel shader active (ps_3_0)");
        fflush(stdout);

    s_slugShader = shader;

    return shader;
}

} // namespace

bool GxuFontSlugDebugBuild(FT_Face face, uint32_t code, uint32_t pixelSize) {
    if (!face || s_slugReady) {
        return s_slugReady;
    }

    SLUGGLYPH glyph;

    if (!GxuFontSlugExtractGlyph(face, code, pixelSize, &glyph)) {
        return false;
    }

    const uint32_t texels = GxuFontSlugPackCurves(glyph, s_slugCurveData, kSlugPageWidth * kSlugPageHeight);

    if (!texels) {
        return false;
    }

    s_slugCurveCount = static_cast<uint32_t>(glyph.m_curves.size());
    s_slugMinX = glyph.m_minX;
    s_slugMinY = glyph.m_minY;
    s_slugMaxX = glyph.m_maxX;
    s_slugMaxY = glyph.m_maxY;

    s_slugTexture = CreateSlugTexture();
    SlugLog("[Slug] texture create -> %p", static_cast<void*>(s_slugTexture));

    if (!s_slugTexture) {
        return false;
    }

    // The texture only becomes a GPU resource when it is resolved and updated;
    // binding a CTexture that was never latched faults inside the device. The font
    // atlas does exactly this through UpdateDirty.
    s_slugGxTex = TextureGetGxTex(s_slugTexture, 1, nullptr);
    SlugLog("[Slug] resolve -> %p", static_cast<void*>(s_slugGxTex));

    if (!s_slugGxTex) {
        SlugLog("[Slug] curve texture failed to resolve");
        return false;
    }

    GxTexUpdate(s_slugGxTex, 0, 0, kSlugPageWidth, kSlugPageHeight, 1);
    SlugLog("[Slug] update issued");

    s_slugReady = true;

    for (uint32_t k = 0; k < 3 && k < s_slugCurveCount; k++) {
        const SLUGCURVE& c = glyph.m_curves[k];
        SlugLog("[Slug] curve %u: p0(%.3f,%.3f) p1(%.3f,%.3f) p2(%.3f,%.3f)",
            k, c.m_p0[0], c.m_p0[1], c.m_p1[0], c.m_p1[1], c.m_p2[0], c.m_p2[1]);
    }

    SlugLog("[Slug] glyph %c curves=%u texels=%u bounds x %.3f..%.3f y %.3f..%.3f",
        static_cast<char>(code), s_slugCurveCount, texels,
        s_slugMinX, s_slugMaxX, s_slugMinY, s_slugMaxY);

    return true;
}

CTexture* GxuFontSlugTexture() {
    return s_slugTexture;
}

void GxuFontSlugShaderParams(float* out) {
    if (!out) {
        return;
    }

    out[0] = 0.0f;                                                  // first curve texel
    out[1] = static_cast<float>(s_slugCurveCount);
    out[2] = 1.0f / static_cast<float>(kSlugPageWidth);
    out[3] = 1.0f / static_cast<float>(kSlugPageHeight);
}

bool GxuFontSlugGlyphBounds(float* minX, float* minY, float* maxX, float* maxY) {
    if (!s_slugReady) {
        return false;
    }

    if (minX) *minX = s_slugMinX;
    if (minY) *minY = s_slugMinY;
    if (maxX) *maxX = s_slugMaxX;
    if (maxY) *maxY = s_slugMaxY;

    return true;
}

bool GxuFontSlugReady() {
    return s_slugReady && s_slugTexture != nullptr && GetSlugShader() != nullptr;
}

void GxuFontSlugDebugDraw(FT_Face face, uint32_t pixelSize) {
    // The target glyph is fixed for now; switching it later is just a parameter.
    if (!GxuFontSlugDebugBuild(face, '8', pixelSize)) {
        return;
    }

    CGxShader* shader = GetSlugShader();

    if (!shader || !s_slugGxTex) {
        return;
    }

    static bool s_loggedDraw = false;

    if (!s_loggedDraw) {
        s_loggedDraw = true;
        SlugLog("[Slug] probe reached, screen %ux%u, quad 60,60 - 280,280",
            g_widthPixels, g_heightPixels);
    }

    // The latch may only run once the device is ready, so refresh a few times.
    static uint32_t s_updateCount = 0;

    if (s_updateCount < 3) {
        s_updateCount++;
        GxTexUpdate(s_slugGxTex, 0, 0, kSlugPageWidth, kSlugPageHeight, 1);
        SlugLog("[Slug] draw-time update %u", s_updateCount);
    }

    // RenderBatch runs many times per frame; draw the probe on a rough fraction of
    // those calls so it shows up without paying for it dozens of times a frame.
    static uint32_t s_drawCounter = 0;

    if ((++s_drawCounter & 31) != 1) {
        return;
    }

    g_theGxDevicePtr->RsPush();
    GxRsSet(GxRs_PixelShader, shader);
    // The render state takes the resolved CGxTex, not the CTexture wrapper: the
    // device casts the value straight to CGxTex and reads its flags.
    GxRsSet(GxRs_Texture0, s_slugGxTex);

    float params[4];
    GxuFontSlugShaderParams(params);
    GxShaderConstantsSet(GxSh_Pixel, 0, params, 1);

    // A fixed screen-space box, in the same pixel space the font batches use.
    const float x0 = 60.0f;
    const float y0 = 60.0f;
    const float x1 = x0 + 220.0f;
    const float y1 = y0 + 220.0f;

    CGxBuf* buf = GxBufStream(GxPoolTarget_Vertex, sizeof(CGxVertexPCT), 4);
    CGxVertexPCT* verts = reinterpret_cast<CGxVertexPCT*>(GxBufLock(buf));

    if (!verts) {
        g_theGxDevicePtr->RsPop();
        return;
    }

    const CImVector white = { 0xFF, 0xFF, 0xFF, 0xFF };

    // Triangle strip: TL, TR, BL, BR. y grows downward on screen, em space is up.
    const float positions[4][2] = { { x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y1 } };
    const float coords[4][2] = {
        { s_slugMinX, s_slugMaxY },
        { s_slugMaxX, s_slugMaxY },
        { s_slugMinX, s_slugMinY },
        { s_slugMaxX, s_slugMinY },
    };

    for (int i = 0; i < 4; i++) {
        verts[i].p.x = positions[i][0];
        verts[i].p.y = positions[i][1];
        verts[i].p.z = 0.0f;
        verts[i].c = white;
        verts[i].tc.x = coords[i][0];
        verts[i].tc.y = coords[i][1];
    }

    GxBufUnlock(buf, sizeof(CGxVertexPCT) * 4);
    GxPrimVertexPtr(buf, GxVBF_PCT);

    CGxBatch batch;
    batch.m_primType = GxPrim_TriangleStrip;
    batch.m_start = 0;
    batch.m_count = 4;
    batch.m_minIndex = 0;
    batch.m_maxIndex = 3;

    GxDraw(&batch, 1);

    g_theGxDevicePtr->RsPop();
}

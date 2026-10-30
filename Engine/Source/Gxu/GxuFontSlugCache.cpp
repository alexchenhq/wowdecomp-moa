#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Engine/Source/Gxu/GxuFontSlugCache.h"
#include "Engine/Source/Gxu/GxuFontSlug.h"
#include "Engine/Source/Gxu/GxuFontSlugShader.h"
#include "Engine/Source/Gxu/GxuFontMiscClasses.h"
#include "Engine/Source/Gxu/GxuFontFace.h"
#include "Engine/Source/Gxu/GxuFontFont.h"
#include "Engine/Source/Gxu/GxuFont.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/Gx/CGxVertexPCT.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Storm/H/storm.h"

namespace {

const uint32_t kPageWidth = 256;
const uint32_t kPageHeight = 256;
const uint32_t kTexelBytes = 4;          // Gr1616F, two halves per texel
const uint32_t kTotalTexels = kPageWidth * kPageHeight;
const uint32_t kTexelsPerCurve = 4;

// The console only shows crash output, so diagnostics go to a file.
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

uint16_t s_pageData[kPageWidth * kPageHeight * 2];

CTexture* s_texture = nullptr;
CGxTex* s_deviceTexture = nullptr;
CGxShader* s_shader = nullptr;

struct CacheEntry {
    FT_Face m_face;
    uint32_t m_code;
    SLUGGLYPHINFO m_info;
};

std::vector<CacheEntry> s_entries;
uint32_t s_cursorTexel = 0;

void PageCallback(EGxTexCommand command, uint32_t width, uint32_t height,
                  uint32_t face, uint32_t level, void* userArg,
                  uint32_t& texelStrideInBytes, const void*& texels) {
    if (command != GxTex_Latch) {
        return;
    }

    texelStrideInBytes = width * kTexelBytes;
    texels = s_pageData;
}

bool EnsureTexture() {
    if (s_texture && s_deviceTexture) {
        return true;
    }

    CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1, 0, 0, 0);
    s_texture = TextureCreate(kPageWidth, kPageHeight, GxTex_Gr1616F, GxTex_Gr1616F,
        flags, nullptr, PageCallback, "GxuFontSlug", 0);

    if (!s_texture) {
        SlugLog("[Slug] curve page creation failed");
        return false;
    }

    s_deviceTexture = TextureGetGxTex(s_texture, 1, nullptr);

    if (!s_deviceTexture) {
        SlugLog("[Slug] curve page resolve failed");
        return false;
    }

    return true;
}

void UploadPage() {
    if (s_deviceTexture) {
        GxTexUpdate(s_deviceTexture, 0, 0, kPageWidth, kPageHeight, 1);
    }
}

CGxShader* GetShader() {
    if (s_shader) {
        return s_shader;
    }

    CGxShader* shader = new CGxShader;

    shader->m_target = GxSh_Pixel;
    shader->m_code.SetCount(g_gxuFontSlugPixelShaderSize);
    memcpy(shader->m_code.m_data, g_gxuFontSlugPixelShader, g_gxuFontSlugPixelShaderSize);
    shader->m_loaded = 0;

    if (!shader->Valid()) {
        SlugLog("[Slug] pixel shader failed to create");
        delete shader;
        return nullptr;
    }

    SlugLog("[Slug] pixel shader active (ps_3_0)");
    s_shader = shader;

    return shader;
}

CGxBuf* GetIndexBuffer() {
    static CGxBuf* s_indexBuf = nullptr;

    if (s_indexBuf && s_indexBuf->m_hasData && s_indexBuf->m_valid) {
        return s_indexBuf;
    }

    s_indexBuf = GxBufStream(GxPoolTarget_Index, sizeof(uint16_t), 6);

    if (!s_indexBuf) {
        return nullptr;
    }

    uint16_t* indices = reinterpret_cast<uint16_t*>(GxBufLock(s_indexBuf));

    if (!indices) {
        return nullptr;
    }

    indices[0] = 0; indices[1] = 2; indices[2] = 1;
    indices[3] = 2; indices[4] = 3; indices[5] = 1;

    GxBufUnlock(s_indexBuf, sizeof(uint16_t) * 6);

    return s_indexBuf;
}

// One textured quad for a glyph. Screen space has y growing upward and em space is
// y up as well, so the smaller y takes the glyph bottom.
void DrawGlyphQuad(const SLUGGLYPHINFO& glyph, float x0, float y0, float width, const CImVector& color) {
    const float boxWidth = (glyph.m_maxX - glyph.m_minX) > 1e-4f ? (glyph.m_maxX - glyph.m_minX) : 1.0f;
    const float boxHeight = (glyph.m_maxY - glyph.m_minY) > 1e-4f ? (glyph.m_maxY - glyph.m_minY) : 1.0f;
    const float height = width * (boxHeight / boxWidth);

    const float x1 = x0 + width;
    const float y1 = y0 + height;

    CGxBuf* buf = GxBufStream(GxPoolTarget_Vertex, sizeof(CGxVertexPCT), 4);
    CGxVertexPCT* verts = reinterpret_cast<CGxVertexPCT*>(GxBufLock(buf));

    if (!verts) {
        return;
    }

    const float positions[4][2] = { { x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y1 } };
    const float coords[4][2] = {
        { glyph.m_minX, glyph.m_minY },
        { glyph.m_maxX, glyph.m_minY },
        { glyph.m_minX, glyph.m_maxY },
        { glyph.m_maxX, glyph.m_maxY },
    };

    for (int i = 0; i < 4; i++) {
        verts[i].p.x = positions[i][0];
        verts[i].p.y = positions[i][1];
        verts[i].p.z = 0.0f;
        verts[i].c = color;
        verts[i].tc.x = coords[i][0];
        verts[i].tc.y = coords[i][1];
    }

    GxBufUnlock(buf, sizeof(CGxVertexPCT) * 4);

    CGxBuf* indexBuf = GetIndexBuffer();

    if (!indexBuf) {
        return;
    }

    float params[4];
    GxuFontSlugShaderParams(glyph, params);
    GxShaderConstantsSet(GxSh_Pixel, 0, params, 1);

    GxPrimVertexPtr(buf, GxVBF_PCT);
    g_theGxDevicePtr->PrimIndexPtr(indexBuf);

    CGxBatch batch;
    batch.m_primType = GxPrim_Triangles;
    batch.m_start = 0;
    batch.m_count = 6;
    batch.m_minIndex = 0;
    batch.m_maxIndex = 3;

    GxDraw(&batch, 1);
}

} // namespace

bool GxuFontSlugGetGlyph(FT_Face face, uint32_t code, uint32_t pixelSize, SLUGGLYPHINFO* out) {
    if (!face || !out) {
        return false;
    }

    for (const CacheEntry& entry : s_entries) {
        if (entry.m_face == face && entry.m_code == code) {
            *out = entry.m_info;
            return true;
        }
    }

    SLUGGLYPH glyph;

    if (!GxuFontSlugExtractGlyph(face, code, pixelSize, &glyph)) {
        return false;
    }

    const uint32_t needed = static_cast<uint32_t>(glyph.m_curves.size()) * kTexelsPerCurve;

    if (!needed) {
        return false;
    }

    // Flush rather than fragment. A real allocator belongs with the text path; a
    // reset keeps the layout trivially correct for now.
    if (s_cursorTexel + needed > kTotalTexels) {
        SlugLog("[Slug] page full, flushing %u entries", static_cast<uint32_t>(s_entries.size()));
        s_entries.clear();
        s_cursorTexel = 0;
        memset(s_pageData, 0, sizeof(s_pageData));
    }

    if (!EnsureTexture()) {
        return false;
    }

    // Two uint16 per texel, so the byte offset is texel * 2.
    const uint32_t written = GxuFontSlugPackCurves(glyph, s_pageData + s_cursorTexel * 2, needed);

    if (!written) {
        return false;
    }

    CacheEntry entry;
    entry.m_face = face;
    entry.m_code = code;
    entry.m_info.m_baseTexel = s_cursorTexel;
    entry.m_info.m_curveCount = static_cast<uint32_t>(glyph.m_curves.size());
    entry.m_info.m_minX = glyph.m_minX;
    entry.m_info.m_minY = glyph.m_minY;
    entry.m_info.m_maxX = glyph.m_maxX;
    entry.m_info.m_maxY = glyph.m_maxY;

    s_entries.push_back(entry);
    s_cursorTexel += needed;

    UploadPage();

    SlugLog("[Slug] cached %c base=%u curves=%u used=%u/%u",
        static_cast<char>(code), entry.m_info.m_baseTexel, entry.m_info.m_curveCount,
        s_cursorTexel, kTotalTexels);

    *out = entry.m_info;

    return true;
}

CTexture* GxuFontSlugTexture() {
    return s_texture;
}

CGxTex* GxuFontSlugDeviceTexture() {
    return s_deviceTexture;
}

void GxuFontSlugShaderParams(const SLUGGLYPHINFO& glyph, float* out) {
    if (!out) {
        return;
    }

    out[0] = static_cast<float>(glyph.m_baseTexel);
    out[1] = static_cast<float>(glyph.m_curveCount);
    out[2] = 1.0f / static_cast<float>(kPageWidth);
    out[3] = 1.0f / static_cast<float>(kPageHeight);
}

void GxuFontSlugDebugDraw(FT_Face face, uint32_t pixelSize) {
    // Once per frame: repeated draws accumulate alpha and harden the soft edge.
    static int32_t s_lastFrame = -1;

    if (g_theGxDevicePtr->m_frameCount == s_lastFrame) {
        return;
    }

    s_lastFrame = g_theGxDevicePtr->m_frameCount;

    SLUGGLYPHINFO first;
    SLUGGLYPHINFO second;

    if (!GxuFontSlugGetGlyph(face, 'R', pixelSize, &first)) {
        return;
    }

    const bool haveSecond = GxuFontSlugGetGlyph(face, '8', pixelSize, &second);

    CGxShader* shader = GetShader();

    if (!shader || !s_deviceTexture) {
        return;
    }

    g_theGxDevicePtr->RsPush();
    GxRsSet(GxRs_PixelShader, shader);
    GxRsSet(GxRs_Texture0, s_deviceTexture);

    const CImVector white = { 0xFF, 0xFF, 0xFF, 0xFF };
    const CImVector tinted = { 0xFF, 0x80, 0xC0, 0xFF };

    const float glyphWidth = 0.18f * static_cast<float>(g_widthPixels);
    const float baseY = 0.12f * static_cast<float>(g_heightPixels);

    DrawGlyphQuad(first, 0.06f * static_cast<float>(g_widthPixels), baseY, glyphWidth, white);

    if (haveSecond) {
        DrawGlyphQuad(second, 0.28f * static_cast<float>(g_widthPixels), baseY, glyphWidth, tinted);
    }

    g_theGxDevicePtr->RsPop();
}

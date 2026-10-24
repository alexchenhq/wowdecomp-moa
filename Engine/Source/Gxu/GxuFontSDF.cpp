#include "Engine/Source/Gxu/GxuFontSDF.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace GxuFontSdf {

namespace {

constexpr float kBig = 1.0e9f;

// 1D squared Euclidean distance transform (Felzenszwalb & Huttenlocher).
// f: sampled function, d: output squared distances, both of length n.
void DistanceTransform1D(const std::vector<float>& f, std::vector<float>& d) {
    const int n = static_cast<int>(f.size());

    std::vector<int> v(n);
    std::vector<float> z(n + 1);

    int k = 0;
    v[0] = 0;
    z[0] = -kBig;
    z[1] = kBig;

    for (int q = 1; q < n; q++) {
        float s = ((f[q] + static_cast<float>(q) * q) - (f[v[k]] + static_cast<float>(v[k]) * v[k]))
                / static_cast<float>(2 * q - 2 * v[k]);

        while (s <= z[k]) {
            k--;
            s = ((f[q] + static_cast<float>(q) * q) - (f[v[k]] + static_cast<float>(v[k]) * v[k]))
              / static_cast<float>(2 * q - 2 * v[k]);
        }

        k++;
        v[k] = q;
        z[k] = s;
        z[k + 1] = kBig;
    }

    k = 0;

    for (int q = 0; q < n; q++) {
        while (z[k + 1] < static_cast<float>(q)) {
            k++;
        }

        const float diff = static_cast<float>(q - v[k]);
        d[q] = diff * diff + f[v[k]];
    }
}

// Squared distance transform of a 2D field, separable in both axes.
void DistanceTransform2D(std::vector<float>& grid, int w, int h) {
    const int maxDim = std::max(w, h);

    std::vector<float> line(static_cast<size_t>(maxDim));
    std::vector<float> out(static_cast<size_t>(maxDim));

    for (int x = 0; x < w; x++) {
        line.resize(h);
        for (int y = 0; y < h; y++) {
            line[y] = grid[y * w + x];
        }
        out.resize(h);
        DistanceTransform1D(line, out);
        for (int y = 0; y < h; y++) {
            grid[y * w + x] = out[y];
        }
    }

    for (int y = 0; y < h; y++) {
        line.resize(w);
        for (int x = 0; x < w; x++) {
            line[x] = grid[y * w + x];
        }
        out.resize(w);
        DistanceTransform1D(line, out);
        for (int x = 0; x < w; x++) {
            grid[y * w + x] = out[x];
        }
    }
}

inline uint32_t PackGray(uint32_t v) {
    v &= 0xFF;
    return (v << 24) | (v << 16) | (v << 8) | v;
}

inline float DistanceSqToSegment(float px, float py, float ax, float ay, float bx, float by) {
    const float dx = bx - ax;
    const float dy = by - ay;
    const float len2 = dx * dx + dy * dy;

    float t = 0.0f;

    if (len2 > 1e-12f) {
        t = ((px - ax) * dx + (py - ay) * dy) / len2;

        if (t < 0.0f) {
            t = 0.0f;
        } else if (t > 1.0f) {
            t = 1.0f;
        }
    }

    const float cx = ax + t * dx;
    const float cy = ay + t * dy;
    const float ex = px - cx;
    const float ey = py - cy;

    return ex * ex + ey * ey;
}

} // namespace

void ClearCell(uint32_t cellW, uint32_t cellH, uint32_t* dest, uint32_t destStride) {    for (uint32_t y = 0; y < cellH; y++) {
        uint32_t* row = dest + static_cast<size_t>(y) * destStride;
        for (uint32_t x = 0; x < cellW; x++) {
            row[x] = 0;
        }
    }
}

void BuildCell(const uint8_t* gray, uint32_t gw, uint32_t gh, uint32_t pitch,
               uint32_t cellW, uint32_t cellH, uint32_t yStart,
               uint32_t* dest, uint32_t destStride) {
    if (!gray || !gw || !gh || !cellW || !cellH) {
        ClearCell(cellW, cellH, dest, destStride);
        return;
    }

    const int pad = static_cast<int>(kSpread) + 2;
    const int w = static_cast<int>(cellW) + pad * 2;
    const int h = static_cast<int>(cellH) + pad * 2;
    const size_t total = static_cast<size_t>(w) * h;

    // Binary mask: 1 inside the glyph, 0 outside.
    std::vector<uint8_t> mask(total, 0);

    for (uint32_t y = 0; y < gh; y++) {
        const uint8_t* src = gray + static_cast<size_t>(y) * pitch;
        const int gy = static_cast<int>(yStart + y) + pad;

        if (gy < 0 || gy >= h) {
            continue;
        }

        for (uint32_t x = 0; x < gw; x++) {
            const int gx = static_cast<int>(x) + pad;

            if (gx >= w) {
                break;
            }

            // Threshold at 50% coverage: that contour is what the distance
            // field should reproduce.
            if (src[x] >= 128) {
                mask[static_cast<size_t>(gy) * w + gx] = 1;
            }
        }
    }

    // Distance to the nearest inside pixel (for every pixel).
    std::vector<float> toInside(total);
    // Distance to the nearest outside pixel (for every pixel).
    std::vector<float> toOutside(total);

    for (size_t i = 0; i < total; i++) {
        toInside[i] = mask[i] ? 0.0f : kBig;
        toOutside[i] = mask[i] ? kBig : 0.0f;
    }

    DistanceTransform2D(toInside, w, h);
    DistanceTransform2D(toOutside, w, h);

    const float invScale = 1.0f / (2.0f * kSpread);

    for (uint32_t y = 0; y < cellH; y++) {
        uint32_t* row = dest + static_cast<size_t>(y) * destStride;
        const int gy = static_cast<int>(y) + pad;
        const int sy = static_cast<int>(y) - static_cast<int>(yStart);

        for (uint32_t x = 0; x < cellW; x++) {
            const int gx = static_cast<int>(x) + pad;
            const size_t idx = static_cast<size_t>(gy) * w + gx;

            float signedDistance;

            if (mask[idx]) {
                // Inside: positive distance to the boundary.
                signedDistance = std::sqrt(toOutside[idx]) - 0.5f;
            } else {
                // Outside: negative distance to the boundary.
                signedDistance = -(std::sqrt(toInside[idx]) - 0.5f);
            }

            // Sub-pixel refinement: where the source pixel is only partially
            // covered, the true contour lies inside that pixel rather than on
            // the binarized boundary. Using the coverage directly keeps thin
            // stems and corners from rounding off at small sizes.
            if (sy >= 0 && sy < static_cast<int>(gh) && x < gw) {
                const float cov = static_cast<float>(gray[static_cast<size_t>(sy) * pitch + x]) * (1.0f / 255.0f);

                if (cov > 0.0f && cov < 1.0f) {
                    signedDistance = cov - 0.5f;
                }
            }

            float encoded = 0.5f + signedDistance * invScale;
            encoded = std::min(1.0f, std::max(0.0f, encoded));

            row[x] = PackGray(static_cast<uint32_t>(encoded * 255.0f + 0.5f));
        }
    }
}

bool BuildFromPolygons(const float* pts, int pointCount,                       const int* contourStart, int contourCount,
                       float originX, float originY,
                       uint32_t gw, uint32_t gh, uint8_t* out) {
    if (!pts || pointCount < 2 || !contourStart || contourCount < 1 || !out || !gw || !gh) {
        return false;
    }

    // Reject degenerate input up front.
    bool any = false;

    for (int c = 0; c < contourCount; c++) {
        const int s = contourStart[c];
        const int e = (c + 1 < contourCount) ? contourStart[c + 1] : pointCount;

        if (s < 0 || e > pointCount || e - s >= 2) {
            any = true;
        }
    }

    if (!any) {
        return false;
    }

    const float invScale = 1.0f / (2.0f * kSpread);

    // Every sample is evaluated against the real outline, so the distance is
    // exact instead of being quantized to the rasterizer's pixel grid.
    for (uint32_t y = 0; y < gh; y++) {
        // Pixel centre in glyph space. y counts down in the bitmap, up in glyph space.
        const float py = originY - (static_cast<float>(y) + 0.5f);

        for (uint32_t x = 0; x < gw; x++) {
            const float px = originX + (static_cast<float>(x) + 0.5f);

            float bestSq = 3.0e38f;
            int crossings = 0;

            for (int c = 0; c < contourCount; c++) {
                const int s = contourStart[c];
                const int e = (c + 1 < contourCount) ? contourStart[c + 1] : pointCount;

                if (e - s < 2) {
                    continue;
                }

                for (int i = s; i < e; i++) {
                    const int j = (i + 1 < e) ? (i + 1) : s; // contours are implicitly closed

                    const float ax = pts[2 * i];
                    const float ay = pts[2 * i + 1];
                    const float bx = pts[2 * j];
                    const float by = pts[2 * j + 1];

                    const float d2 = DistanceSqToSegment(px, py, ax, ay, bx, by);

                    if (d2 < bestSq) {
                        bestSq = d2;
                    }

                    // Even-odd crossing test along +x.
                    if ((ay <= py) != (by <= py)) {
                        const float t = (py - ay) / (by - ay);
                        const float xCross = ax + t * (bx - ax);

                        if (xCross > px) {
                            crossings++;
                        }
                    }
                }
            }

            const float dist = std::sqrt(bestSq);
            const float signedDistance = (crossings & 1) ? dist : -dist;

            float encoded = 0.5f + signedDistance * invScale;
            encoded = std::min(1.0f, std::max(0.0f, encoded));

            out[static_cast<size_t>(y) * gw + x] = static_cast<uint8_t>(encoded * 255.0f + 0.5f);
        }
    }

    return true;
}

void BlitDistance(const uint8_t* sdf, uint32_t sdfWidth, uint32_t sdfHeight,
                  uint32_t cellW, uint32_t cellH, uint32_t yStart,
                  uint32_t* dest, uint32_t destStride) {
    ClearCell(cellW, cellH, dest, destStride);

    if (!sdf || !sdfWidth || !sdfHeight) {
        return;
    }

    for (uint32_t y = 0; y < sdfHeight; y++) {
        const uint32_t cellRow = yStart + y;

        if (cellRow >= cellH) {
            break;
        }

        const uint8_t* src = sdf + static_cast<size_t>(y) * sdfWidth;
        uint32_t* row = dest + static_cast<size_t>(cellRow) * destStride;

        for (uint32_t x = 0; x < sdfWidth && x < cellW; x++) {
            row[x] = PackGray(src[x]);
        }
    }
}

} // namespace GxuFontSdf

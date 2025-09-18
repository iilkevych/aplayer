//
// Created by Ihor Ilkevych on 8/1/25.
//
#define STB_TRUETYPE_IMPLEMENTATION
#include "afont.h"
#include "util.h"
#define LOG_TAG "afont"

AFont::AFont():bitmap(nullptr) {
    bitmap = (unsigned char*)malloc(1024*1024);
}

AFont::~AFont() {
    free(bitmap);
    free(cdata);
}

bool AFont::init(AAssetManager* am) {
    // Load font from assets
    AAsset* asset = AAssetManager_open(am, "Roboto-Regular.ttf", AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE(LOG_TAG, "Font not found in assets!");
        return false;
    }
    size_t fontSize = AAsset_getLength(asset);
    auto* fontBuffer = (unsigned char*)malloc(fontSize);
    AAsset_read(asset, fontBuffer, fontSize);
    AAsset_close(asset);
    stbtt_BakeFontBitmap(fontBuffer, 0, 96.0, bitmap, 1024, 1024, 32, 96, cdata);
    free(fontBuffer);
    return true;
}

vector<Vertex> AFont::buildTextQuads(const char* text, float sx, float sy, float oy) {
    vector<Vertex> out;
    // Numbers are usually w(48) = h(96) / 2
    // Vertex coordinates are relative from -1 to 1
    // lets fit 10 numbers into window.
    // each number is 102 px (0,2) wide.  sx * h / 2  = 0.2
    // sx = 0.2 / 48
    // sy * 680 = sx * 1024, sy = 0.0042 * 1024 / 680 = 0.0063
    float o = strlen(text) * 23 * sx;
    float x = 0.0f, y = 0.0f;
    while (*text) {
        if (*text >= 32 && *text < 128) {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(cdata, 1024, 1024, *text - 32, &x, &y, &q, 1);
            Vertex v0 = {{q.x0 * sx - o, -q.y0 * sy + oy}, {q.s0, q.t0}};
            Vertex v1 = {{q.x1 * sx - o, -q.y0 * sy + oy}, {q.s1, q.t0}};
            Vertex v2 = {{q.x1 * sx - o, -q.y1 * sy + oy}, {q.s1, q.t1}};
            Vertex v3 = {{q.x0 * sx - o, -q.y1 * sy + oy}, {q.s0, q.t1}};
            out.insert(out.end(), {v0, v1, v2, v0, v2, v3});
        }
        ++text;
    }
    return out;
}
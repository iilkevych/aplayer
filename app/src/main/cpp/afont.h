//
// Created by Ihor Ilkevych on 8/1/25.
//
#include <GLES2/gl2.h>
#include <vector>

#include "stb_truetype.h"
#include <android/asset_manager.h>
#include "util.h"
using namespace std;

struct Vertex {
    GLfloat pos[2];
    GLfloat uv[2];
};

class AFont {
public:
    AFont();
    ~AFont();
    unsigned char* bitmap;

    bool init(AAssetManager *am);
    vector<Vertex> buildTextQuads(const char *text, float sx, float sy, float oy);

private:
    stbtt_bakedchar cdata[96]{};
};

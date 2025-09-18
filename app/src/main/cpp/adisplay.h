//
// Created by Ihor Ilkevych on 8/1/25.
//
#include <GLES2/gl2.h>
#include "afont.h"
#include "adecoder.h"
using namespace std;

class ADisplay{
public:
    bool init(unsigned char* bitmap, ANativeWindow* window);
    void terminate();
    void draw(vector<Vertex> verts, AImage* image = nullptr);
private:
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    GLuint gProgram;
    GLuint gvPositionHandle;
    GLuint gTexCoordHandle;
    GLuint gTextureId;
    GLuint gVideoProgram;
    GLuint gVideoPositionHandle;
    GLuint gVideoTexCoordHandle;
    GLuint gVideoTextureId;
};

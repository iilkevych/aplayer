//
// Created by Ihor Ilkevych on 8/8/25.
//
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/native_window.h>
#include <android/hardware_buffer.h>

#include "adisplay.h"
#include "util.h"
#define LOG_TAG "adisplay"

// EGL extension function pointers
typedef EGLClientBuffer (EGLAPIENTRYP PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC) (const struct AHardwareBuffer *buffer);
PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC eglGetNativeClientBufferANDROID = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)
        eglGetProcAddress("eglGetNativeClientBufferANDROID");;

// OpenGL extension function pointers
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
        eglGetProcAddress("glEGLImageTargetTexture2DOES");

auto gVertexShader =
        "attribute vec4 vPosition;\n"
        "attribute vec2 vTexCoord;\n"
        "varying vec2 texCoord;\n"
        "void main() {\n"
        "  gl_Position = vPosition;\n"
        "  texCoord = vTexCoord;\n"
        "}\n";

auto gFragmentShader =
        "precision mediump float;\n"
        "varying vec2 texCoord;\n"
        "uniform sampler2D uTexture;\n"
        "void main() {\n"
        "  vec4 color = texture2D(uTexture, texCoord);\n"
        "  gl_FragColor = vec4(1.0, 1.0, 1.0, color.r);\n"
        "}\n";

auto gVideoFragmentShader =
        "#extension GL_OES_EGL_image_external : require\n"
        "precision mediump float;\n"
        "varying vec2 texCoord;\n"
        "uniform samplerExternalOES uTexture;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(uTexture, texCoord);\n"
        "}\n";

const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_NONE
};

const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
};

static void checkGlError(const char* op) {
    for (GLint error = glGetError(); error; error = glGetError()) {
        LOGI("after %s() glError (0x%x)\n", op, error);
    }
}

GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*)malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    LOGE(LOG_TAG, "Could not compile shader %d:\n%s\n", shaderType, buf);
                    free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
    }
    return shader;
}

GLuint createProgram(const char* pVertexSource, const char* pFragmentSource) {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader) {
        return 0;
    }

    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader) {
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        checkGlError("glAttachShader");
        glAttachShader(program, pixelShader);
        checkGlError("glAttachShader");
        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*)malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
                    LOGE(LOG_TAG, "Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    }
    return program;
}

bool ADisplay::init(unsigned char* bitmap, ANativeWindow* window) {
    // initialize OpenGL ES and EGL
//    EGLint w, h;
    // Create EGL image from hardware buffer
    if (!eglGetNativeClientBufferANDROID || !glEGLImageTargetTexture2DOES) {
        LOGE(LOG_TAG, "Required EGL/GL extensions not available");
        return false;
    }
    EGLConfig config;
    EGLint numConfigs, format;

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, nullptr, nullptr);
    /* Here, the application chooses the configuration it desires.
     * find the best match if possible, otherwise use the very first one
     */
    eglChooseConfig(display, attribs, &config, 1, &numConfigs);

    /* EGL_NATIVE_VISUAL_ID is an attribute of the EGLConfig that is
     * guaranteed to be accepted by ANativeWindow_setBuffersGeometry().
     * As soon as we picked a EGLConfig, we can safely reconfigure the
     * ANativeWindow buffers to match, using EGL_NATIVE_VISUAL_ID. */
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);

    ANativeWindow_setBuffersGeometry(window, 0, 0, format);
    surface = eglCreateWindowSurface(display, config, window, nullptr);

    /* A version of OpenGL has not been specified here.  This will default to
     * OpenGL 1.0.  You will need to change this if you want to use the newer
     * features of OpenGL like shaders. */
    context = eglCreateContext(display, config, nullptr, contextAttribs);

    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
        LOGW(LOG_TAG, "Unable to eglMakeCurrent");
    }
//    eglQuerySurface(display, surface, EGL_WIDTH, &w);
//    eglQuerySurface(display, surface, EGL_HEIGHT, &h);
//
//    LOGI(LOG_TAG, "setupGraphics(%d, %d)", w, h);
    gProgram = createProgram(gVertexShader, gFragmentShader);
    if (!gProgram) {
        LOGE(LOG_TAG, "Could not create gRrogram.");
        return false;
    }
    gvPositionHandle = glGetAttribLocation(gProgram, "vPosition");
    checkGlError("glGetAttribLocation");
    LOGI(LOG_TAG, "glGetAttribLocation(\"vPosition\") = %d\n", gvPositionHandle);
    gTexCoordHandle  = glGetAttribLocation(gProgram, "vTexCoord");
    checkGlError("glGetAttribLocation");
    LOGI(LOG_TAG, "glGetAttribLocation(\"vTexCoord\") = %d\n", gTexCoordHandle);

    glGenTextures(1, &gTextureId);
    glBindTexture(GL_TEXTURE_2D, gTextureId);
//    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 512, 512, 0, GL_ALPHA, GL_UNSIGNED_BYTE, engine->font->bitmap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 1024, 1024, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, bitmap);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gVideoProgram = createProgram(gVertexShader, gVideoFragmentShader);
    if (!gVideoProgram) {
        LOGE(LOG_TAG, "Could not create gVideoProgram.");
        return false;
    }

    gVideoPositionHandle = glGetAttribLocation(gVideoProgram, "vPosition");
    gVideoTexCoordHandle = glGetAttribLocation(gVideoProgram, "vTexCoord");
    glGenTextures(1, &gVideoTextureId);
    return true;

//    glViewport(0, 0, w, h);
//    checkGlError("glViewport");
}

/**
 * Tear down the EGL context currently associated with the display.
 */
void ADisplay::terminate() {
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
        }
        if (surface != EGL_NO_SURFACE) {
            eglDestroySurface(display, surface);
        }
        eglTerminate(display);
    }
    display = EGL_NO_DISPLAY;
    context = EGL_NO_CONTEXT;
    surface = EGL_NO_SURFACE;
}

void ADisplay::draw(vector<Vertex> verts, AImage* image) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // Black background
    checkGlError("glClearColor");
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    checkGlError("glClear");

    // Draw video background using zero-copy EGL image
    if (image) {
        // Get AHardwareBuffer from AImage
        AHardwareBuffer* hardwareBuffer = nullptr;
        if (AImage_getHardwareBuffer(image, &hardwareBuffer) == AMEDIA_OK && hardwareBuffer) {

            EGLClientBuffer clientBuffer = eglGetNativeClientBufferANDROID(hardwareBuffer);
            if (clientBuffer) {
                EGLAttrib attrs[] = {
                    EGL_IMAGE_PRESERVED, EGL_TRUE,
                    EGL_NONE
                };

                EGLImage eglImage = eglCreateImage(display, EGL_NO_CONTEXT,
                                                 EGL_NATIVE_BUFFER_ANDROID,
                                                 clientBuffer, attrs);

                if (eglImage != EGL_NO_IMAGE) {
                    // Bind to texture using zero-copy
                    glBindTexture(GL_TEXTURE_EXTERNAL_OES, gVideoTextureId);
                    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, eglImage);

                    // Draw fullscreen quad with video texture using external texture shader
                    glUseProgram(gVideoProgram);

                    // Fullscreen quad vertices (position + texcoord)
                    float videoVerts[] = {
                        -1.0f, -1.0f, 0.0f, 1.0f,  // Bottom-left
                        1.0f, -1.0f, 1.0f, 1.0f,  // Bottom-right
                        1.0f,  1.0f, 1.0f, 0.0f,  // Top-right
                        -1.0f,  1.0f, 0.0f, 0.0f   // Top-left
                    };

                    glActiveTexture(GL_TEXTURE0);
                    glUniform1i(glGetUniformLocation(gVideoProgram, "uTexture"), 0);

                    glEnableVertexAttribArray(gVideoPositionHandle);
                    glEnableVertexAttribArray(gVideoTexCoordHandle);
                    glVertexAttribPointer(gVideoPositionHandle, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), videoVerts);
                    glVertexAttribPointer(gVideoTexCoordHandle, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), videoVerts + 2);

                    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

                    glDisableVertexAttribArray(gVideoPositionHandle);
                    glDisableVertexAttribArray(gVideoTexCoordHandle);

                    // Cleanup EGL image
                    eglDestroyImage(display, eglImage);
                }
            }
        }
    }

    glUseProgram(gProgram);
    checkGlError("glUseProgram");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gTextureId);

    glEnableVertexAttribArray(gvPositionHandle);
    glEnableVertexAttribArray(gTexCoordHandle);
    glVertexAttribPointer(gvPositionHandle, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &verts[0].pos);
    glVertexAttribPointer(gTexCoordHandle, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &verts[0].uv);
    glDrawArrays(GL_TRIANGLES, 0, verts.size());
    glDisableVertexAttribArray(gvPositionHandle);
    glDisableVertexAttribArray(gTexCoordHandle);
    eglSwapBuffers(display, surface);

}
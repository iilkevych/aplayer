/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/choreographer.h>
#include <android_native_app_glue.h>

#include "util.h"
#include "adisplay.h"
#include "adecoder.h"

#define LOG_TAG "native-activity"

using namespace std::chrono;

/**
 * Shared state for our app.
 */
struct Engine {
    android_app* app;

    float scaleX;
    float scaleY;

    AFont* font;
    ADisplay* display;
    ADecoder* decoder;

    /// Resumes ticking the application.
    void Resume() {
        // Checked to make sure we don't double schedule Choreographer.
        if (!running_) {
            running_ = true;
            ScheduleNextTick();
        }
    }

    /// Pauses ticking the application.
    ///
    /// When paused, sensor and input events will still be processed, but the
    /// update and render parts of the loop will not run.
    void Pause() { running_ = false; }

private:
    bool running_;
    time_point<high_resolution_clock> start = high_resolution_clock::now();
    int fc = 0;
    time_point<high_resolution_clock> ft = high_resolution_clock::now();
    vector<Vertex> fv;

    void ScheduleNextTick() {
        AChoreographer_postVsyncCallback(AChoreographer_getInstance(),
                                           Tick, this);
    }

    static void Tick(const AChoreographerFrameCallbackData*, void* data) {
        CHECK_NOT_NULL(LOG_TAG, data);
        auto engine = reinterpret_cast<Engine*>(data);
        engine->DoTick();
    }

    void DoTick() {
        if (!running_) {
            return;
        }

        // Input and sensor feedback is handled via their own callbacks.
        // Choreographer ensures that those callbacks run before this callback does.

        // Choreographer does not continuously schedule the callback. We have to re-
        // register the callback each time we're ticked.
        ScheduleNextTick();
        if (display == nullptr) {
            // No display.
            return;
        }
        auto now = high_resolution_clock::now();

        // Calculate text scale based on window size to keep text size constant
        auto v = font->buildTextQuads(
                to_string(duration_cast<milliseconds>(now - start).count() % 100000).c_str(),
                scaleX, scaleY, 0.0f);
        fc++;
        auto e = duration_cast<milliseconds>(now - ft).count();
        if(e > 1000){
            auto fps = fc * 1000.0f / e;
            fc = 0;
            ft = now;
            fv = font->buildTextQuads((to_string((int)fps) + " fps").c_str(),
                                      scaleX * 0.5f, scaleY * 0.5f, -0.4f);
//            LOGI(LOG_TAG, "fps: %f", fps);
        }
        v.insert(v.end(), fv.begin(), fv.end());
        AImage* image = decoder ? decoder->acquireLatestImage() : nullptr;
        display->draw(v, image);

        if (image) {
            AImage_delete(image);
        }
    }

};

/**
 * Process the next main command.
 */
static void engine_handle_cmd(android_app* app, int32_t cmd) {
    auto* engine = (Engine*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            // The window is being shown, get it ready.
            if (engine->app->window != nullptr
                && engine->font != nullptr
                && engine->font->init(engine->app->activity->assetManager)
                && engine->display != nullptr
                && engine->display->init(engine->font->bitmap, engine->app->window)) {
                if(engine->decoder != nullptr
                    && !engine->decoder->init()){
                    LOGW(LOG_TAG, "Failed to initialize decoder");
                    delete engine->decoder;
                    engine->decoder = nullptr;
                }
                // Base scale factors for 1920x1080, scale inversely to maintain same text size
                engine->scaleX = 1920.0f / ANativeWindow_getWidth(engine->app->window) * 0.0042f;
                engine->scaleY = 1080.0f / ANativeWindow_getHeight(engine->app->window) * 0.0063f;

                engine->Resume();
            }
            break;
        case APP_CMD_TERM_WINDOW:
            // The window is being hidden or closed, clean it up.
            engine->display->terminate();
            engine->decoder->terminate();
            engine->Pause();
        default:
            break;
    }
}

/**
 * This is the main entry point of a native application that is using
 * android_native_app_glue.  It runs in its own thread, with its own
 * event loop for receiving input events and doing other things.
 */
void android_main(android_app* state) {
    Engine engine {};

    state->userData = &engine;
    state->onAppCmd = engine_handle_cmd;
    engine.app = state;
    engine.font = new AFont();
    engine.display = new ADisplay();
    engine.decoder = new ADecoder();

    while (!state->destroyRequested) {
        // Our input, sensor, and update/render logic is all driven by callbacks, so
        // we don't need to use the non-blocking poll.
        android_poll_source* source = nullptr;
        auto result = ALooper_pollOnce(-1, nullptr, nullptr,
                                       reinterpret_cast<void**>(&source));
        if (result == ALOOPER_POLL_ERROR) {
            fatal(LOG_TAG, "ALooper_pollOnce returned an error");
        }

        if (source != nullptr) {
            source->process(state, source);
        }
    }

    // Cleanup
    delete engine.decoder;
    delete engine.display;
    delete engine.font;
}
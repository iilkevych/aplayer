//
// Created by Ihor Ilkevych on 9/13/25.
//
#ifndef ADECODER_H
#define ADECODER_H

#include <android/native_window.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

#include <thread>
#include <semaphore>
#include <mutex>
#include <atomic>

class ADecoder {
public:
    ADecoder();
    ~ADecoder();

    bool init();
    void terminate();
    AImage* acquireLatestImage();

private:
    static const int MAX_IMAGES = 3;

    AMediaExtractor* extractor;
    AMediaCodec* codec;
    AImageReader* imageReader;

    std::thread extractorThread;
    bool run = false;
    std::mutex mtx;
    std::condition_variable cv;
    bool available = false;

    void extractorLoop();
};

#endif //ADECODER_H
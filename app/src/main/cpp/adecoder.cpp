//
// Created by Ihor Ilkevych on 9/13/25.
//
#include "adecoder.h"
#include "util.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

#define LOG_TAG "adecoder"

ADecoder::ADecoder() :
    extractor(nullptr),
    codec(nullptr),
    imageReader(nullptr){
}

ADecoder::~ADecoder() {
    terminate();
}

bool ADecoder::init() {
    // Find the most recent MP4 file
    const char* downloadDir = "/sdcard/Download";
    DIR* dir = opendir(downloadDir);
    if (!dir) {
        LOGE(LOG_TAG, "Failed to open directory: %s", downloadDir);
        return false;
    }
    std::string filePath;
    time_t maxTime = 0;

    struct dirent* entry;
    while (( entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename.length() >= 4 && filename.substr(filename.length() - 4) == ".mp4") {
            filename.insert(0, std::string(downloadDir) + "/");
            // Get file modification time
            struct stat fileStat;
            if (stat(filename.c_str(), &fileStat) == 0 && fileStat.st_mtime > maxTime) {
                maxTime = fileStat.st_mtime;
                filePath = filename;
            }
        }
    }
    closedir(dir);

    if (filePath.empty()) {
        LOGE(LOG_TAG, "No MP4 files found in Downloads folder");
        return false;
    }

    LOGI(LOG_TAG, "Initializing decoder from file: %s", filePath.c_str());

    // Create media extractor
    extractor = AMediaExtractor_new();
    if (!extractor) {
        LOGE(LOG_TAG, "Failed to create media extractor");
        return false;
    }

    // Open file and use file descriptor
    int fd = open(filePath.c_str(), O_RDONLY);
    if (fd < 0) {
        LOGE(LOG_TAG, "Failed to open file: %s, error: %s", filePath.c_str(), strerror(errno));
        return false;
    }

    // Get file size
    struct stat statBuf;
    if (fstat(fd, &statBuf) != 0) {
        LOGE(LOG_TAG, "Failed to get file stats");
        close(fd);
        return false;
    }

    // Set data source using file descriptor
    media_status_t status = AMediaExtractor_setDataSourceFd(extractor, fd, 0, statBuf.st_size);
    close(fd);

    if (status != AMEDIA_OK) {
        LOGE(LOG_TAG, "Failed to set data source fd: %d", status);
        return false;
    }

    // Find video track
    size_t numTracks = AMediaExtractor_getTrackCount(extractor);
    int videoTrackIndex = -1;
    AMediaFormat* videoFormat = nullptr;

    for (size_t i = 0; i < numTracks; i++) {
        AMediaFormat* format = AMediaExtractor_getTrackFormat(extractor, i);
        const char* mime;
        if (AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) {
            if (strncmp(mime, "video/", 6) == 0) {
                videoTrackIndex = i;
                videoFormat = format;
                break;
            }
        }
        if (format != videoFormat) {
            AMediaFormat_delete(format);
        }
    }

    if (videoTrackIndex == -1) {
        LOGE(LOG_TAG, "No video track found");
        return false;
    }

    // Select video track
    status = AMediaExtractor_selectTrack(extractor, videoTrackIndex);
    if (status != AMEDIA_OK) {
        LOGE(LOG_TAG, "Failed to select video track: %d", status);
        AMediaFormat_delete(videoFormat);
        return false;
    }

    // Get video dimensions
    int32_t width, height;
    if (!AMediaFormat_getInt32(videoFormat, AMEDIAFORMAT_KEY_WIDTH, &width) ||
        !AMediaFormat_getInt32(videoFormat, AMEDIAFORMAT_KEY_HEIGHT, &height)) {
        LOGE(LOG_TAG, "Failed to get video dimensions");
        AMediaFormat_delete(videoFormat);
        return false;
    }

    // Create ImageReader
    media_status_t imageReaderStatus = AImageReader_new(width, height, AIMAGE_FORMAT_YUV_420_888, MAX_IMAGES, &imageReader);
    if (imageReaderStatus != AMEDIA_OK) {
        LOGE(LOG_TAG, "Failed to create ImageReader: %d", imageReaderStatus);
        AMediaFormat_delete(videoFormat);
        return false;
    }


    // Get ANativeWindow from ImageReader
    ANativeWindow* surface;
    status = AImageReader_getWindow(imageReader, &surface);
    if (status != AMEDIA_OK) {
        LOGE(LOG_TAG, "Failed to get window from ImageReader: %d", status);
        AMediaFormat_delete(videoFormat);
        return false;
    }

    // Create codec
    const char* mime;
    AMediaFormat_getString(videoFormat, AMEDIAFORMAT_KEY_MIME, &mime);
    codec = AMediaCodec_createDecoderByType(mime);
    if (!codec) {
        LOGE(LOG_TAG, "Failed to create decoder for mime: %s", mime);
        AMediaFormat_delete(videoFormat);
        return false;
    }

    // Configure codec
    status = AMediaCodec_configure(codec, videoFormat, surface, nullptr, 0);
    if (status != AMEDIA_OK) {
        LOGE(LOG_TAG, "Failed to configure codec: %d", status);
        AMediaFormat_delete(videoFormat);
        return false;
    }

    AMediaFormat_delete(videoFormat);

    // Start codec
    status = AMediaCodec_start(codec);
    if (status != AMEDIA_OK) {
        LOGE(LOG_TAG, "Failed to start codec: %d", status);
        return false;
    }

    // Start extractor thread
    run = true;
    extractorThread = std::thread(&ADecoder::extractorLoop, this);

    LOGI(LOG_TAG, "ADecoder initialized successfully for %dx%d video", width, height);
    return true;
}

void ADecoder::terminate() {
    run = false;

    if (extractorThread.joinable()) {
        extractorThread.join();
    }

    if (codec) {
        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
        codec = nullptr;
    }

    if (imageReader) {
        AImageReader_delete(imageReader);
        imageReader = nullptr;
    }

    if (extractor) {
        AMediaExtractor_delete(extractor);
        extractor = nullptr;
    }
}

AImage* ADecoder::acquireLatestImage() {
    if (!imageReader || !codec) {
        return nullptr;
    }
    AImage* image = nullptr;
    // wait until main() sends data
    std::unique_lock lk(mtx);
    cv.wait(lk, [this]{ return available; });
    AImageReader_acquireLatestImage(imageReader, &image);
    available = false;
    lk.unlock();
//    LOGI(LOG_TAG, "return Image");
    return image;
}

void ADecoder::extractorLoop() {
    LOGI(LOG_TAG, "Extractor loop started");
    auto startTime = std::chrono::high_resolution_clock::now();

    while (run) {
        // Check if we can get an input buffer
        ssize_t inputBufferIndex = AMediaCodec_dequeueInputBuffer(codec, 10000); // 10ms timeout

        if (inputBufferIndex >= 0) {
            // Get input buffer
            size_t inputBufferSize;
            uint8_t* inputBuffer = AMediaCodec_getInputBuffer(codec, inputBufferIndex, &inputBufferSize);

            // Read sample from extractor
            ssize_t sampleSize = AMediaExtractor_readSampleData(extractor, inputBuffer, inputBufferSize);

            if (sampleSize < 0) {
                // End of stream - restart from beginning
                AMediaCodec_flush(codec);
                AMediaExtractor_seekTo(extractor, 0, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
//                // Sleep to slow down extraction
//                std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                continue;
            } else {
                // Reset presentation time and queue input buffer
                AMediaCodec_queueInputBuffer(codec, inputBufferIndex, 0, sampleSize, 0, 0);
                // Advance to next sample
                AMediaExtractor_advance(extractor);
            }
        }

        // Dequeue output buffers to keep the decoder pipeline flowing
        AMediaCodecBufferInfo bufferInfo;
        ssize_t outputBufferIndex = AMediaCodec_dequeueOutputBuffer(codec, &bufferInfo, 10000);

        if (outputBufferIndex >= 0) {
//            LOGI(LOG_TAG, "Got output buffer, releasing");
            std::lock_guard lk(mtx);
            AMediaCodec_releaseOutputBuffer(codec, outputBufferIndex, true);
            available = true;
        } else if (outputBufferIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* format = AMediaCodec_getOutputFormat(codec);
            LOGI(LOG_TAG, "Output format changed");
            if (format) {
                AMediaFormat_delete(format);
            }
        }
        cv.notify_one();
    }

    LOGI(LOG_TAG, "Extractor loop finished");
}

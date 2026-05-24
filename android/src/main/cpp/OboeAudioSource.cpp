#include "OboeAudioSource.h"
#include <android/log.h>

#define LOG_TAG "OboeAudioSource"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

OboeAudioSource::OboeAudioSource(SamplesCallback callback)
    : callback_(std::move(callback)) {}

OboeAudioSource::~OboeAudioSource() {
    stop();
}


bool OboeAudioSource::openStream() {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Input);
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    builder.setSharingMode(oboe::SharingMode::Exclusive);
    builder.setFormat(oboe::AudioFormat::Float);
    builder.setChannelCount(oboe::ChannelCount::Mono);
    builder.setSampleRate(48000);
    builder.setCallback(this);

    oboe::Result result = builder.openStream(stream_);

    if (result != oboe::Result::OK) {
        LOGE("Failed to open Exclusive stream: %s — retrying Shared", oboe::convertToText(result));

        // Fall back to Shared mode
        builder.setSharingMode(oboe::SharingMode::Shared);
        result = builder.openStream(stream_);

        if (result != oboe::Result::OK) {
            LOGE("Failed to open Shared stream: %s", oboe::convertToText(result));
            return false;
        }
    }

    sampleRate_ = static_cast<float>(stream_->getSampleRate());
    LOGI("Oboe stream opened: sampleRate=%.0f sharingMode=%s",
         sampleRate_,
         oboe::convertToText(stream_->getSharingMode()));
    return true;
}

bool OboeAudioSource::start() {
    if (!openStream()) return false;

    oboe::Result result = stream_->requestStart();
    if (result != oboe::Result::OK) {
        LOGE("Failed to start stream: %s", oboe::convertToText(result));
        stream_->close();
        stream_.reset();
        return false;
    }

    LOGI("Oboe stream started");
    return true;
}

void OboeAudioSource::stop() {
    stopped_.store(true, std::memory_order_relaxed);
    if (restartThread_.joinable()) {
        restartThread_.join();
    }
    if (stream_) {
        stream_->requestStop();
        stream_->close();
        stream_.reset();
        LOGI("Oboe stream stopped");
    }
}

float OboeAudioSource::sampleRate() const {
    return sampleRate_;
}

oboe::DataCallbackResult OboeAudioSource::onAudioReady(
    oboe::AudioStream* /*stream*/,
    void* audioData,
    int32_t numFrames
) {
    if (callback_) {
        callback_(static_cast<const float*>(audioData), numFrames, sampleRate_);
    }
    return oboe::DataCallbackResult::Continue;
}

void OboeAudioSource::onErrorAfterClose(oboe::AudioStream* /*stream*/, oboe::Result error) {
    LOGE("Oboe stream error after close: %s — attempting restart", oboe::convertToText(error));
    if (stopped_.load(std::memory_order_relaxed)) return;
    // Restart on a separate thread to avoid deadlock; join in stop()/destructor
    if (restartThread_.joinable()) restartThread_.join();
    restartThread_ = std::thread([this]() {
        if (!stopped_.load(std::memory_order_relaxed)) {
            start();
        }
    });
}

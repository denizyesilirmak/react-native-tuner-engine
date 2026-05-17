#include <jni.h>
#include <android/log.h>
#include <memory>
#include "AudioFrameDispatcher.hpp"
#include "OboeAudioSource.h"

#define LOG_TAG "TunerEngineJni"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Globals — one engine per process (single-instance module assumption)
static std::unique_ptr<AudioFrameDispatcher> gDispatcher;
static std::unique_ptr<OboeAudioSource> gAudioSource;

// JNI back-reference for pitch callbacks
static JavaVM* gJvm = nullptr;
static jweak gModuleRef = nullptr; // weak ref to TunerEngineModule kotlin object
static jmethodID gOnPitchMethod = nullptr;

// Called from C++ worker thread; marshals PitchResult → Java
static void onPitchResult(const PitchResult& r) {
    if (!gJvm || !gModuleRef || !gOnPitchMethod) return;

    JNIEnv* env = nullptr;
    bool didAttach = false;

    jint attachResult = gJvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (attachResult == JNI_EDETACHED) {
        if (gJvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            didAttach = true;
        } else {
            LOGE("Failed to attach thread");
            return;
        }
    } else if (attachResult != JNI_OK) {
        return;
    }

    jobject module = env->NewLocalRef(gModuleRef);
    if (module) {
        env->CallVoidMethod(
            module,
            gOnPitchMethod,
            static_cast<jboolean>(r.hasPitch),
            static_cast<jfloat>(r.frequency),
            static_cast<jfloat>(r.confidence),
            static_cast<jfloat>(r.rmsDb),
            env->NewStringUTF(r.noteName.c_str()),
            static_cast<jint>(r.octave),
            static_cast<jfloat>(r.cents)
        );
        env->DeleteLocalRef(module);
    }

    if (didAttach) {
        gJvm->DetachCurrentThread();
    }
}

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    gJvm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeInit(
    JNIEnv* env, jobject thiz,
    jfloat sampleRate, jint frameSize
) {
    // Store weak ref to the Kotlin module object for callbacks
    if (gModuleRef) env->DeleteWeakGlobalRef(gModuleRef);
    gModuleRef = env->NewWeakGlobalRef(thiz);

    // Cache the callback method ID
    jclass cls = env->GetObjectClass(thiz);
    gOnPitchMethod = env->GetMethodID(cls, "onPitchDetected",
        "(ZFFFLjava/lang/String;IF)V");

    gDispatcher = std::make_unique<AudioFrameDispatcher>(
        static_cast<int>(frameSize),
        static_cast<float>(sampleRate),
        onPitchResult
    );

    LOGI("nativeInit: sampleRate=%.0f frameSize=%d", (float)sampleRate, (int)frameSize);
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeStart(
    JNIEnv* env, jobject thiz
) {
    if (!gDispatcher) {
        Java_com_tunerengine_TunerEngineModule_nativeInit(env, thiz, 48000.0f, 2048);
    }

    gAudioSource = std::make_unique<OboeAudioSource>(
        [](const float* samples, int count, float /*sr*/) {
            if (gDispatcher) gDispatcher->push(samples, count);
        }
    );

    if (!gAudioSource->start()) {
        LOGE("Failed to start Oboe stream");
        gAudioSource.reset();
        return;
    }

    // Sync dispatcher to actual sample rate reported by Oboe
    if (gDispatcher) {
        gDispatcher->setSampleRate(gAudioSource->sampleRate());
        gDispatcher->start();
    }

    LOGI("Audio capture started");
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeStop(
    JNIEnv* /*env*/, jobject /*thiz*/
) {
    if (gDispatcher) gDispatcher->stop();
    if (gAudioSource) {
        gAudioSource->stop();
        gAudioSource.reset();
    }
    LOGI("Audio capture stopped");
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeConfigure(
    JNIEnv* env, jobject thiz,
    jfloat sampleRate, jint frameSize,
    jfloat noiseGateDb, jfloat confidenceThreshold,
    jfloat minFrequency, jfloat maxFrequency,
    jfloat a4
) {
    Java_com_tunerengine_TunerEngineModule_nativeInit(env, thiz, sampleRate, frameSize);
    if (gDispatcher) {
        gDispatcher->setNoiseGateDb(noiseGateDb);
        gDispatcher->setConfidenceThreshold(confidenceThreshold);
        gDispatcher->setFrequencyRange(minFrequency, maxFrequency);
        gDispatcher->setA4(a4);
    }
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeSetA4(
    JNIEnv* /*env*/, jobject /*thiz*/, jfloat hz
) {
    if (gDispatcher) gDispatcher->setA4(hz);
}

JNIEXPORT jboolean JNICALL
Java_com_tunerengine_TunerEngineModule_nativeIsRunning(
    JNIEnv* /*env*/, jobject /*thiz*/
) {
    return static_cast<jboolean>(gAudioSource != nullptr);
}

} // extern "C"

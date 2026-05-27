#include <jni.h>
#include <android/log.h>
#include <memory>
#include <mutex>
#include <atomic>
#include <string>
#include <fbjni/fbjni.h>
#include <jsi/jsi.h>
#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/CallInvokerHolder.h>
#include "AudioFrameDispatcher.hpp"
#include "OboeAudioSource.h"
#include "OnsetDetector.hpp"
#include "PostProcessor.hpp"

#define LOG_TAG "TunerEngineJni"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Globals — one engine per process (single-instance module assumption).
// All access must hold gMutex, except:
//   - gJvm: written once in JNI_OnLoad, then read-only
//   - gJsInvoker: accessed via std::atomic_load/store (lock-free shared_ptr)
//   - gModuleRef / gOnPitchMethod: written only while dispatcher worker is
//     joined (so onPitchResult cannot be in-flight), therefore safe to read
//     from the worker without the mutex
//   - gDispatcherAtomic: read lock-free from the Oboe audio thread
static std::mutex gMutex;
static std::unique_ptr<AudioFrameDispatcher> gDispatcher;
static std::shared_ptr<OboeAudioSource> gAudioSource;
// Atomic raw pointer used by the Oboe audio callback to push samples.
// Written under gMutex; read lock-free from the audio thread.
static std::atomic<AudioFrameDispatcher*> gDispatcherAtomic{nullptr};

// JSI call invoker — set once from initialize(), read from worker thread.
// std::atomic_load/store is used for lock-free, thread-safe shared_ptr access.
static std::shared_ptr<facebook::react::CallInvoker> gJsInvoker;

// JNI back-reference for Kotlin-side pitch snapshot update (getStatus()).
static JavaVM* gJvm = nullptr;
static jobject gModuleRef = nullptr;
static jmethodID gOnPitchMethod = nullptr;

// Called from AudioFrameDispatcher's worker thread.
//
// Two delivery paths run in order:
//   1. JSI (Bridgeless / New Arch): schedules a direct call into JS via
//      __tunerEngineOnPitch, mirroring the iOS implementation.
//   2. JNI → Kotlin (always): updates the @Volatile snapshot fields so that
//      getStatus() returns fresh data regardless of which path is active.
//
// gModuleRef and gOnPitchMethod are stable during execution: they are only
// replaced after the worker is joined (see initDispatcherLocked), so they
// cannot change while this function is executing.
static void onPitchResult(const PitchResult& r) {
    // ── Path 1: JSI direct call ──────────────────────────────────────────────
    auto jsInvoker = std::atomic_load(&gJsInvoker);
    if (jsInvoker) {
        bool   hasPitch    = r.hasPitch;
        double frequency   = r.frequency;
        double confidence  = r.confidence;
        double rmsDb       = r.rmsDb;
        std::string note   = r.noteName;
        int    octave      = r.octave;
        double cents       = r.cents;
        std::string nearestStr = r.nearestString;
        double stringDev   = r.stringDeviation;

        jsInvoker->invokeAsync(
            [hasPitch, frequency, confidence, rmsDb,
             note, octave, cents, nearestStr, stringDev]
            (facebook::jsi::Runtime& rt) {
                auto cb = rt.global().getProperty(rt, "__tunerEngineOnPitch");
                if (!cb.isObject()) return;
                auto fn = cb.asObject(rt);
                if (!fn.isFunction(rt)) return;

                facebook::jsi::Object obj(rt);
                obj.setProperty(rt, "hasPitch",        facebook::jsi::Value(hasPitch));
                obj.setProperty(rt, "frequency",        facebook::jsi::Value(frequency));
                obj.setProperty(rt, "confidence",       facebook::jsi::Value(confidence));
                obj.setProperty(rt, "rmsDb",            facebook::jsi::Value(rmsDb));
                obj.setProperty(rt, "noteName",
                    facebook::jsi::String::createFromUtf8(rt, note));
                obj.setProperty(rt, "octave",           facebook::jsi::Value(octave));
                obj.setProperty(rt, "cents",            facebook::jsi::Value(cents));
                obj.setProperty(rt, "nearestString",
                    facebook::jsi::String::createFromUtf8(rt, nearestStr));
                obj.setProperty(rt, "stringDeviation",  facebook::jsi::Value(stringDev));

                fn.asFunction(rt).call(rt, std::move(obj));
            }
        );
    }

    // ── Path 2: JNI → Kotlin (snapshot update for getStatus()) ──────────────
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

    jstring noteName   = env->NewStringUTF(r.noteName.c_str());
    jstring nearestStr = env->NewStringUTF(r.nearestString.c_str());
    env->CallVoidMethod(
        gModuleRef, gOnPitchMethod,
        static_cast<jboolean>(r.hasPitch),
        static_cast<jfloat>(r.frequency),
        static_cast<jfloat>(r.confidence),
        static_cast<jfloat>(r.rmsDb),
        noteName,
        static_cast<jint>(r.octave),
        static_cast<jfloat>(r.cents),
        nearestStr,
        static_cast<jfloat>(r.stringDeviation)
    );
    env->DeleteLocalRef(noteName);
    env->DeleteLocalRef(nearestStr);

    if (didAttach) gJvm->DetachCurrentThread();
}

// Rebuilds the dispatcher. Must be called with gMutex held.
// Correct teardown order:
//   1. Null the atomic  → audio callback stops pushing immediately
//   2. Join old worker  → onPitchResult() cannot be in-flight after this
//   3. Swap gModuleRef  → safe because no callbacks can race us
//   4. Create new dispatcher
//   5. Restore atomic   → audio callback resumes with new dispatcher
static void initDispatcherLocked(
    JNIEnv* env, jobject thiz,
    float sampleRate, int frameSize, float overlapRatio
) {
    gDispatcherAtomic.store(nullptr, std::memory_order_release);

    if (gDispatcher) {
        gDispatcher->stop();
        gDispatcher.reset();
    }

    if (gModuleRef) env->DeleteGlobalRef(gModuleRef);
    gModuleRef = env->NewGlobalRef(thiz);

    jclass cls = env->GetObjectClass(thiz);
    gOnPitchMethod = env->GetMethodID(cls, "onPitchDetected",
        "(ZFFFLjava/lang/String;IFLjava/lang/String;F)V");

    gDispatcher = std::make_unique<AudioFrameDispatcher>(
        frameSize, sampleRate, onPitchResult, overlapRatio
    );

    gDispatcherAtomic.store(gDispatcher.get(), std::memory_order_release);

    LOGI("nativeInit: sampleRate=%.0f frameSize=%d overlapRatio=%.2f",
         sampleRate, frameSize, overlapRatio);
}

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    gJvm = vm;
    // Initialize fbjni so HybridClass internals (field IDs, type registration)
    // are set up for this library. Safe to call from multiple libraries.
    return facebook::jni::initialize(vm, []() {});
}

// Called from TunerEngineModule.initialize() once the JS context is ready.
// Stores the JSI CallInvoker for direct-to-JS event delivery (Bridgeless mode).
JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeSetCallInvoker(
    JNIEnv* /*env*/, jobject /*thiz*/, jobject callInvokerHolder
) {
    if (!callInvokerHolder) return;
    auto holder = jni::alias_ref<facebook::react::CallInvokerHolder::javaobject>{
        reinterpret_cast<facebook::react::CallInvokerHolder::javaobject>(callInvokerHolder)
    };
    std::atomic_store(&gJsInvoker, holder->cthis()->getCallInvoker());
    LOGI("JSI CallInvoker registered — direct event delivery active");
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeInit(
    JNIEnv* env, jobject thiz,
    jfloat sampleRate, jint frameSize, jfloat overlapRatio
) {
    std::lock_guard<std::mutex> lock(gMutex);
    initDispatcherLocked(env, thiz, sampleRate, frameSize, overlapRatio);
}

JNIEXPORT jboolean JNICALL
Java_com_tunerengine_TunerEngineModule_nativeStart(
    JNIEnv* env, jobject thiz
) {
    std::lock_guard<std::mutex> lock(gMutex);

    if (!gDispatcher) {
        initDispatcherLocked(env, thiz, 48000.0f, 2048, 0.0f);
    }

    gAudioSource = std::make_shared<OboeAudioSource>(
        [](const float* samples, int count, float /*sr*/) {
            // Lock-free read — safe because gDispatcherAtomic is atomic and the
            // pointed-to object outlives the audio source (stopped before reset).
            auto* d = gDispatcherAtomic.load(std::memory_order_acquire);
            if (d) d->push(samples, count);
        }
    );

    if (!gAudioSource->start()) {
        LOGE("Failed to start Oboe stream");
        gAudioSource.reset();
        return JNI_FALSE;
    }

    gDispatcher->setSampleRate(gAudioSource->sampleRate());
    gDispatcher->start();

    LOGI("Audio capture started");
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeStop(
    JNIEnv* env, jobject /*thiz*/
) {
    std::lock_guard<std::mutex> lock(gMutex);

    // Stop audio callback before tearing down so the audio thread cannot
    // slip a push() through while we destroy the dispatcher.
    gDispatcherAtomic.store(nullptr, std::memory_order_release);

    // Join the Oboe audio thread first (no more pushes after this).
    if (gAudioSource) {
        gAudioSource->stop();
        gAudioSource.reset();
    }

    // Join the dispatcher worker thread (no more onPitchResult calls after this).
    if (gDispatcher) {
        gDispatcher->stop();
        gDispatcher.reset();
    }

    // Safe to release the module ref: both threads above are joined.
    if (gModuleRef) {
        env->DeleteGlobalRef(gModuleRef);
        gModuleRef = nullptr;
    }

    LOGI("Audio capture stopped");
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeConfigure(
    JNIEnv* env, jobject thiz,
    jfloat sampleRate, jint frameSize,
    jfloat noiseGateDb, jfloat confidenceThreshold,
    jfloat minFrequency, jfloat maxFrequency,
    jfloat a4, jfloat overlapRatio
) {
    std::lock_guard<std::mutex> lock(gMutex);
    initDispatcherLocked(env, thiz, sampleRate, frameSize, overlapRatio);
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
    std::lock_guard<std::mutex> lock(gMutex);
    if (gDispatcher) gDispatcher->setA4(hz);
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeSetHpfCutoff(
    JNIEnv* /*env*/, jobject /*thiz*/, jfloat hz
) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gDispatcher) gDispatcher->setHpfCutoff(hz);
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeSetPostProcessorConfig(
    JNIEnv* /*env*/, jobject /*thiz*/, jfloat emaAlpha, jint hysteresisFrames
) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gDispatcher) {
        PostProcessor::Config cfg;
        cfg.emaAlpha         = emaAlpha;
        cfg.hysteresisFrames = static_cast<int>(hysteresisFrames);
        gDispatcher->setPostProcessorConfig(cfg);
    }
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeSetInstrument(
    JNIEnv* env, jobject /*thiz*/, jstring name
) {
    std::lock_guard<std::mutex> lock(gMutex);
    const char* nameChars = env->GetStringUTFChars(name, nullptr);
    if (gDispatcher && nameChars) gDispatcher->setInstrument(std::string(nameChars));
    env->ReleaseStringUTFChars(name, nameChars);
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeSetTuning(
    JNIEnv* env, jobject /*thiz*/, jstring name
) {
    std::lock_guard<std::mutex> lock(gMutex);
    const char* nameChars = env->GetStringUTFChars(name, nullptr);
    if (gDispatcher && nameChars) gDispatcher->setTuning(std::string(nameChars));
    env->ReleaseStringUTFChars(name, nameChars);
}

JNIEXPORT jboolean JNICALL
Java_com_tunerengine_TunerEngineModule_nativeIsRunning(
    JNIEnv* /*env*/, jobject /*thiz*/
) {
    std::lock_guard<std::mutex> lock(gMutex);
    return static_cast<jboolean>(gAudioSource != nullptr);
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeSetOnsetDetection(
    JNIEnv* /*env*/, jobject /*thiz*/, jboolean enabled
) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gDispatcher) gDispatcher->setOnsetDetectionEnabled(static_cast<bool>(enabled));
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeSetTemperament(
    JNIEnv* env, jobject /*thiz*/, jstring name
) {
    std::lock_guard<std::mutex> lock(gMutex);
    const char* nameChars = env->GetStringUTFChars(name, nullptr);
    if (gDispatcher && nameChars) gDispatcher->setTemperament(std::string(nameChars));
    env->ReleaseStringUTFChars(name, nameChars);
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeSetAdaptiveFrameSize(
    JNIEnv* /*env*/, jobject /*thiz*/, jboolean enabled
) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gDispatcher) gDispatcher->setAdaptiveFrameSize(static_cast<bool>(enabled));
}

} // extern "C"

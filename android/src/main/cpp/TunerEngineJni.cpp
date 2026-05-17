#include <jni.h>
#include <android/log.h>
#include <memory>
#include "TunerEngine.hpp"

#define LOG_TAG "TunerEngineJni"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::unique_ptr<TunerEngine> gEngine;
static bool gIsRunning = false;

extern "C" {

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeInit(
  JNIEnv* env, jobject /* thiz */,
  jfloat sampleRate, jint frameSize
) {
  gEngine = std::make_unique<TunerEngine>(sampleRate, static_cast<int>(frameSize));
  LOGI("TunerEngine initialized: sampleRate=%.0f frameSize=%d", (float)sampleRate, (int)frameSize);
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeStart(
  JNIEnv* env, jobject /* thiz */
) {
  if (!gEngine) {
    gEngine = std::make_unique<TunerEngine>(48000.0f, 2048);
  }
  gIsRunning = true;
  LOGI("TunerEngine started");
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeStop(
  JNIEnv* env, jobject /* thiz */
) {
  gIsRunning = false;
  LOGI("TunerEngine stopped");
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeConfigure(
  JNIEnv* env, jobject /* thiz */,
  jfloat sampleRate, jint frameSize,
  jfloat noiseGateDb, jfloat confidenceThreshold,
  jfloat minFrequency, jfloat maxFrequency,
  jfloat a4
) {
  gEngine = std::make_unique<TunerEngine>(sampleRate, static_cast<int>(frameSize));
  gEngine->setNoiseGateDb(noiseGateDb);
  gEngine->setConfidenceThreshold(confidenceThreshold);
  gEngine->setFrequencyRange(minFrequency, maxFrequency);
  gEngine->setA4(a4);
}

JNIEXPORT void JNICALL
Java_com_tunerengine_TunerEngineModule_nativeSetA4(
  JNIEnv* env, jobject /* thiz */, jfloat hz
) {
  if (gEngine) {
    gEngine->setA4(hz);
  }
}

JNIEXPORT jboolean JNICALL
Java_com_tunerengine_TunerEngineModule_nativeIsRunning(
  JNIEnv* env, jobject /* thiz */
) {
  return static_cast<jboolean>(gIsRunning);
}

} // extern "C"

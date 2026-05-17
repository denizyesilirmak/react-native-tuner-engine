package com.tunerengine

import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.bridge.WritableNativeMap

class TunerEngineModule(reactContext: ReactApplicationContext) :
  NativeTunerEngineSpec(reactContext) {

  private var isRunning = false

  override fun configure(opts: ReadableMap?, promise: Promise) {
    val sampleRate = opts?.getDouble("sampleRate")?.toFloat() ?: 48000.0f
    val frameSize = opts?.getInt("frameSize") ?: 2048
    val noiseGateDb = opts?.getDouble("noiseGateDb")?.toFloat() ?: -55.0f
    val confidenceThreshold = opts?.getDouble("confidenceThreshold")?.toFloat() ?: 0.75f
    val minFrequency = opts?.getDouble("minFrequency")?.toFloat() ?: 60.0f
    val maxFrequency = opts?.getDouble("maxFrequency")?.toFloat() ?: 1200.0f
    val a4 = opts?.getDouble("a4")?.toFloat() ?: 440.0f

    nativeConfigure(sampleRate, frameSize, noiseGateDb, confidenceThreshold, minFrequency, maxFrequency, a4)
    promise.resolve(null)
  }

  override fun start(promise: Promise) {
    nativeStart()
    isRunning = true
    promise.resolve(null)
  }

  override fun stop(promise: Promise) {
    nativeStop()
    isRunning = false
    promise.resolve(null)
  }

  override fun setA4(hz: Double) {
    nativeSetA4(hz.toFloat())
  }

  override fun setInstrument(name: String) {
    // Instrument preset support added in M2
  }

  override fun setTemperament(name: String) {
    // Temperament support added in M2
  }

  override fun requestPermission(promise: Promise) {
    promise.resolve(true)
  }

  override fun getStatus(): ReadableMap {
    val map = WritableNativeMap()
    map.putBoolean("isRunning", isRunning)
    map.putBoolean("engineReady", nativeIsRunning())
    return map
  }

  override fun addListener(eventName: String) {}

  override fun removeListeners(count: Double) {}

  // JNI methods
  private external fun nativeInit(sampleRate: Float, frameSize: Int)
  private external fun nativeStart()
  private external fun nativeStop()
  private external fun nativeConfigure(
    sampleRate: Float, frameSize: Int,
    noiseGateDb: Float, confidenceThreshold: Float,
    minFrequency: Float, maxFrequency: Float,
    a4: Float
  )
  private external fun nativeSetA4(hz: Float)
  private external fun nativeIsRunning(): Boolean

  companion object {
    const val NAME = NativeTunerEngineSpec.NAME

    init {
      System.loadLibrary("tunerengine")
    }
  }
}

package com.tunerengine

import android.Manifest
import android.content.pm.PackageManager
import androidx.core.app.ActivityCompat
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.bridge.WritableNativeMap
import com.facebook.react.modules.core.DeviceEventManagerModule

class TunerEngineModule(reactContext: ReactApplicationContext) :
  NativeTunerEngineSpec(reactContext) {

  private var isRunning = false

  override fun configure(opts: ReadableMap?, promise: Promise) {
    val sampleRate = opts?.getDouble("sampleRate")?.toFloat() ?: 48000.0f
    val frameSize = if (opts?.hasKey("frameSize") == true) opts.getInt("frameSize") else 2048
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
    val activity = currentActivity
    if (activity == null) {
      promise.resolve(false)
      return
    }

    if (ActivityCompat.checkSelfPermission(activity, Manifest.permission.RECORD_AUDIO)
        == PackageManager.PERMISSION_GRANTED) {
      promise.resolve(true)
      return
    }

    ActivityCompat.requestPermissions(activity, arrayOf(Manifest.permission.RECORD_AUDIO), 0)
    // For a proper permission result callback, integrate with the activity's onRequestPermissionsResult.
    // For M1, resolve optimistically after request.
    promise.resolve(false)
  }

  override fun getStatus(): ReadableMap {
    val map = WritableNativeMap()
    map.putBoolean("isRunning", isRunning)
    map.putBoolean("engineReady", nativeIsRunning())
    return map
  }

  override fun addListener(eventName: String) {}

  override fun removeListeners(count: Double) {}

  // Called from the C++ worker thread via JNI
  @Suppress("unused")
  fun onPitchDetected(
    hasPitch: Boolean,
    frequency: Float,
    confidence: Float,
    rmsDb: Float,
    noteName: String,
    octave: Int,
    cents: Float
  ) {
    val params = Arguments.createMap().apply {
      putBoolean("hasPitch", hasPitch)
      putDouble("frequency", frequency.toDouble())
      putDouble("confidence", confidence.toDouble())
      putDouble("rmsDb", rmsDb.toDouble())
      putString("noteName", noteName)
      putInt("octave", octave)
      putDouble("cents", cents.toDouble())
    }

    reactApplicationContext
      .getJSModule(DeviceEventManagerModule.RCTDeviceEventEmitter::class.java)
      .emit("onPitch", params)
  }

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

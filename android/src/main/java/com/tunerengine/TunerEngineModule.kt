package com.tunerengine

import android.Manifest
import android.content.pm.PackageManager
import androidx.core.app.ActivityCompat
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.bridge.WritableMap
import com.facebook.react.bridge.WritableNativeMap
import com.facebook.react.modules.core.DeviceEventManagerModule
import com.facebook.react.modules.core.PermissionAwareActivity
import com.facebook.react.modules.core.PermissionListener

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
    val overlapRatio = opts?.getDouble("overlapRatio")?.toFloat() ?: 0.0f

    nativeConfigure(sampleRate, frameSize, noiseGateDb, confidenceThreshold, minFrequency, maxFrequency, a4, overlapRatio)

    val hpfCutoffHz = opts?.getDouble("hpfCutoffHz")?.toFloat()
    if (hpfCutoffHz != null) nativeSetHpfCutoff(hpfCutoffHz)

    val emaAlpha = opts?.getDouble("emaAlpha")?.toFloat()
    val hysteresisFrames = if (opts?.hasKey("hysteresisFrames") == true) opts.getInt("hysteresisFrames") else null
    if (emaAlpha != null || hysteresisFrames != null) {
      nativeSetPostProcessorConfig(emaAlpha ?: 0.35f, hysteresisFrames ?: 3)
    }

    if (opts?.hasKey("onsetDetection") == true) {
      nativeSetOnsetDetection(opts.getBoolean("onsetDetection"))
    }

    promise.resolve(null)
  }

  override fun start(promise: Promise) {
    val started = nativeStart()
    if (started) {
      isRunning = true
      promise.resolve(null)
    } else {
      promise.reject("START_ERROR", "Failed to start audio capture")
    }
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
    nativeSetInstrument(name)
  }

  override fun setTuning(name: String) {
    nativeSetTuning(name)
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

    val permissionAwareActivity = activity as? PermissionAwareActivity
    if (permissionAwareActivity == null) {
      promise.resolve(false)
      return
    }

    permissionAwareActivity.requestPermissions(
      arrayOf(Manifest.permission.RECORD_AUDIO),
      REQUEST_PERMISSION_CODE,
      PermissionListener { requestCode, _, grantResults ->
        if (requestCode == REQUEST_PERMISSION_CODE) {
          val granted = grantResults.isNotEmpty() &&
              grantResults[0] == PackageManager.PERMISSION_GRANTED
          promise.resolve(granted)
          true
        } else {
          false
        }
      }
    )
  }

  override fun getStatus(): WritableMap {
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
    cents: Float,
    nearestString: String,
    stringDeviation: Float
  ) {
    val params = Arguments.createMap().apply {
      putBoolean("hasPitch", hasPitch)
      putDouble("frequency", frequency.toDouble())
      putDouble("confidence", confidence.toDouble())
      putDouble("rmsDb", rmsDb.toDouble())
      putString("noteName", noteName)
      putInt("octave", octave)
      putDouble("cents", cents.toDouble())
      putString("nearestString", nearestString)
      putDouble("stringDeviation", stringDeviation.toDouble())
    }

    reactApplicationContext
      .getJSModule(DeviceEventManagerModule.RCTDeviceEventEmitter::class.java)
      .emit("onPitch", params)
  }

  // JNI methods
  private external fun nativeInit(sampleRate: Float, frameSize: Int, overlapRatio: Float)
  private external fun nativeStart(): Boolean
  private external fun nativeStop()
  private external fun nativeConfigure(
    sampleRate: Float, frameSize: Int,
    noiseGateDb: Float, confidenceThreshold: Float,
    minFrequency: Float, maxFrequency: Float,
    a4: Float, overlapRatio: Float
  )
  private external fun nativeSetA4(hz: Float)
  private external fun nativeSetInstrument(name: String)
  private external fun nativeSetTuning(name: String)
  private external fun nativeSetHpfCutoff(hz: Float)
  private external fun nativeSetPostProcessorConfig(emaAlpha: Float, hysteresisFrames: Int)
  private external fun nativeSetOnsetDetection(enabled: Boolean)
  private external fun nativeIsRunning(): Boolean

  companion object {
    const val NAME = NativeTunerEngineSpec.NAME
    private const val REQUEST_PERMISSION_CODE = 1001

    init {
      System.loadLibrary("tunerengine")
    }
  }
}

package com.tunerengine

import android.Manifest
import android.content.pm.PackageManager
import androidx.core.app.ActivityCompat
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.bridge.WritableMap
import com.facebook.react.bridge.WritableNativeMap
import com.facebook.react.modules.core.PermissionAwareActivity
import com.facebook.react.modules.core.PermissionListener

class TunerEngineModule(reactContext: ReactApplicationContext) :
  NativeTunerEngineSpec(reactContext) {

  private var isRunning = false

  // Latest pitch data written by the C++ worker thread, read by JS via getStatus()
  @Volatile private var latestHasPitch = false
  @Volatile private var latestFrequency = 0f
  @Volatile private var latestConfidence = 0f
  @Volatile private var latestRmsDb = 0f
  @Volatile private var latestNoteName = ""
  @Volatile private var latestOctave = 0
  @Volatile private var latestCents = 0f
  @Volatile private var latestNearestString = ""
  @Volatile private var latestStringDeviation = 0f
  @Volatile private var pitchSequence = 0L  // monotonic counter for change detection

  override fun configure(opts: ReadableMap?, promise: Promise) {
    try {
      val sampleRate = if (opts?.hasKey("sampleRate") == true) opts.getDouble("sampleRate").toFloat() else 48000.0f
      val frameSize = if (opts?.hasKey("frameSize") == true) opts.getInt("frameSize") else 2048
      val noiseGateDb = if (opts?.hasKey("noiseGateDb") == true) opts.getDouble("noiseGateDb").toFloat() else -55.0f
      val confidenceThreshold = if (opts?.hasKey("confidenceThreshold") == true) opts.getDouble("confidenceThreshold").toFloat() else 0.75f
      val minFrequency = if (opts?.hasKey("minFrequency") == true) opts.getDouble("minFrequency").toFloat() else 60.0f
      val maxFrequency = if (opts?.hasKey("maxFrequency") == true) opts.getDouble("maxFrequency").toFloat() else 1200.0f
      val a4 = if (opts?.hasKey("a4") == true) opts.getDouble("a4").toFloat() else 440.0f
      val overlapRatio = if (opts?.hasKey("overlapRatio") == true) opts.getDouble("overlapRatio").toFloat() else 0.0f

      nativeConfigure(sampleRate, frameSize, noiseGateDb, confidenceThreshold, minFrequency, maxFrequency, a4, overlapRatio)

      if (opts?.hasKey("hpfCutoffHz") == true) {
        nativeSetHpfCutoff(opts.getDouble("hpfCutoffHz").toFloat())
      }

      val hasEma = opts?.hasKey("emaAlpha") == true
      val hasHyst = opts?.hasKey("hysteresisFrames") == true
      if (hasEma || hasHyst) {
        val emaAlpha = if (hasEma) opts!!.getDouble("emaAlpha").toFloat() else 0.35f
        val hysteresisFrames = if (hasHyst) opts!!.getInt("hysteresisFrames") else 3
        nativeSetPostProcessorConfig(emaAlpha, hysteresisFrames)
      }

      if (opts?.hasKey("onsetDetection") == true) {
        nativeSetOnsetDetection(opts.getBoolean("onsetDetection"))
      }

      promise.resolve(null)
    } catch (e: Exception) {
      promise.reject("CONFIGURE_ERROR", "configure failed: ${e.message}", e)
    }
  }

  override fun start(promise: Promise) {
    try {
      val started = nativeStart()
      if (started) {
        isRunning = true
        promise.resolve(null)
      } else {
        promise.reject("START_ERROR", "Failed to start audio capture")
      }
    } catch (e: Exception) {
      promise.reject("START_ERROR", "start failed: ${e.message}", e)
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
    map.putDouble("seq", pitchSequence.toDouble())
    map.putBoolean("hasPitch", latestHasPitch)
    map.putDouble("frequency", latestFrequency.toDouble())
    map.putDouble("confidence", latestConfidence.toDouble())
    map.putDouble("rmsDb", latestRmsDb.toDouble())
    map.putString("noteName", latestNoteName)
    map.putInt("octave", latestOctave)
    map.putDouble("cents", latestCents.toDouble())
    map.putString("nearestString", latestNearestString)
    map.putDouble("stringDeviation", latestStringDeviation.toDouble())
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
    latestHasPitch = hasPitch
    latestFrequency = frequency
    latestConfidence = confidence
    latestRmsDb = rmsDb
    latestNoteName = noteName
    latestOctave = octave
    latestCents = cents
    latestNearestString = nearestString
    latestStringDeviation = stringDeviation
    pitchSequence++
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

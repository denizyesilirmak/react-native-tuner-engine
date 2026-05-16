package com.tunerengine

import com.facebook.react.bridge.ReactApplicationContext

class TunerEngineModule(reactContext: ReactApplicationContext) :
  NativeTunerEngineSpec(reactContext) {

  override fun multiply(a: Double, b: Double): Double {
    return a * b
  }

  companion object {
    const val NAME = NativeTunerEngineSpec.NAME
  }
}

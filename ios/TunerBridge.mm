#import "TunerBridge.h"
#include "TunerEngine.hpp"
#include <memory>

@implementation TunerBridge {
  std::unique_ptr<TunerEngine> _engine;
  bool _isRunning;
}

- (instancetype)init {
  self = [super init];
  if (self) {
    _isRunning = false;
  }
  return self;
}

- (void)configure:(NSDictionary *)opts {
  float sampleRate = opts[@"sampleRate"] ? [opts[@"sampleRate"] floatValue] : 48000.0f;
  int frameSize = opts[@"frameSize"] ? [opts[@"frameSize"] intValue] : 2048;

  _engine = std::make_unique<TunerEngine>(sampleRate, frameSize);

  if (opts[@"noiseGateDb"]) {
    _engine->setNoiseGateDb([opts[@"noiseGateDb"] floatValue]);
  }
  if (opts[@"confidenceThreshold"]) {
    _engine->setConfidenceThreshold([opts[@"confidenceThreshold"] floatValue]);
  }
  if (opts[@"minFrequency"] && opts[@"maxFrequency"]) {
    _engine->setFrequencyRange(
      [opts[@"minFrequency"] floatValue],
      [opts[@"maxFrequency"] floatValue]
    );
  }
  if (opts[@"a4"]) {
    _engine->setA4([opts[@"a4"] floatValue]);
  }
}

- (void)start {
  if (!_engine) {
    _engine = std::make_unique<TunerEngine>(48000.0f, 2048);
  }
  _isRunning = true;
}

- (void)stop {
  _isRunning = false;
}

- (void)setA4:(float)hz {
  if (_engine) {
    _engine->setA4(hz);
  }
}

- (void)setInstrument:(NSString *)name {
  // Instrument preset support added in M2
}

- (void)setTemperament:(NSString *)name {
  // Temperament support added in M2
}

- (NSDictionary *)getStatus {
  return @{
    @"isRunning": @(_isRunning),
    @"engineReady": @(_engine != nullptr)
  };
}

@end

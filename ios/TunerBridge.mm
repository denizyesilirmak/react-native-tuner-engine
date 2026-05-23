#import "TunerBridge.h"
#import "IosAudioSource.h"
#include "AudioFrameDispatcher.hpp"
#include "PostProcessor.hpp"
#include <memory>

@implementation TunerBridge {
  IosAudioSource* _audioSource;
  std::unique_ptr<AudioFrameDispatcher> _dispatcher;
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
  int frameSize    = opts[@"frameSize"]  ? [opts[@"frameSize"] intValue]   : 2048;

  [self buildDispatcherWithSampleRate:sampleRate frameSize:frameSize opts:opts];
}

- (void)buildDispatcherWithSampleRate:(float)sr frameSize:(int)fs opts:(NSDictionary*)opts {
  __weak __typeof__(self) weakSelf = self;

  _dispatcher = std::make_unique<AudioFrameDispatcher>(fs, sr,
    [weakSelf](const PitchResult& r) {
      __strong __typeof__(weakSelf) strongSelf = weakSelf;
      if (!strongSelf || !strongSelf.onPitch) return;

      NSDictionary* event = @{
        @"hasPitch":         @(r.hasPitch),
        @"frequency":        @(r.frequency),
        @"confidence":       @(r.confidence),
        @"rmsDb":            @(r.rmsDb),
        @"noteName":         @(r.noteName.c_str()),
        @"octave":           @(r.octave),
        @"cents":            @(r.cents),
        @"nearestString":    @(r.nearestString.c_str()),
        @"stringDeviation":  @(r.stringDeviation)
      };
      strongSelf.onPitch(event);
    }
  );

  if (opts[@"noiseGateDb"])       _dispatcher->setNoiseGateDb([opts[@"noiseGateDb"] floatValue]);
  if (opts[@"confidenceThreshold"]) _dispatcher->setConfidenceThreshold([opts[@"confidenceThreshold"] floatValue]);
  if (opts[@"minFrequency"] && opts[@"maxFrequency"]) {
    _dispatcher->setFrequencyRange([opts[@"minFrequency"] floatValue], [opts[@"maxFrequency"] floatValue]);
  }
  if (opts[@"a4"]) _dispatcher->setA4([opts[@"a4"] floatValue]);
  if (opts[@"hpfCutoffHz"]) _dispatcher->setHpfCutoff([opts[@"hpfCutoffHz"] floatValue]);
  if (opts[@"emaAlpha"] || opts[@"hysteresisFrames"]) {
    PostProcessor::Config cfg;
    if (opts[@"emaAlpha"])          cfg.emaAlpha         = [opts[@"emaAlpha"] floatValue];
    if (opts[@"hysteresisFrames"])  cfg.hysteresisFrames = [opts[@"hysteresisFrames"] intValue];
    _dispatcher->setPostProcessorConfig(cfg);
  }
}

- (void)startWithCompletion:(void(^)(NSError* _Nullable error))completion {
  if (_isRunning) {
    completion(nil);
    return;
  }

  _audioSource = [[IosAudioSource alloc] init];

  // Spin up dispatcher if not yet configured
  if (!_dispatcher) {
    [self buildDispatcherWithSampleRate:48000.0f frameSize:2048 opts:@{}];
  }

  __weak __typeof__(self) weakSelf = self;
  _audioSource.onSamples = ^(const float* samples, int count, float sampleRate) {
    __strong __typeof__(weakSelf) strongSelf = weakSelf;
    if (!strongSelf || !strongSelf->_dispatcher) return;
    strongSelf->_dispatcher->push(samples, count);
  };

  NSError* error = nil;
  if (![_audioSource startWithError:&error]) {
    completion(error);
    return;
  }

  // Sync dispatcher sample rate to what AVAudioSession actually gave us
  const float actualSampleRate = _audioSource.sampleRate;
  _dispatcher->setSampleRate(actualSampleRate);
  _dispatcher->start();
  _isRunning = true;

  completion(nil);
}

- (void)stop {
  if (!_isRunning) return;
  _isRunning = false;

  if (_dispatcher) _dispatcher->stop();
  [_audioSource stop];
  _audioSource = nil;
}

- (void)setA4:(float)hz {
  if (_dispatcher) _dispatcher->setA4(hz);
}

- (void)setInstrument:(NSString *)name {
  if (_dispatcher) _dispatcher->setInstrument(std::string([name UTF8String]));
}

- (void)setTuning:(NSString *)name {
  if (_dispatcher) _dispatcher->setTuning(std::string([name UTF8String]));
}

- (void)setTemperament:(NSString *)name {}

- (NSDictionary *)getStatus {
  return @{
    @"isRunning":    @(_isRunning),
    @"engineReady":  @(_dispatcher != nullptr)
  };
}

@end

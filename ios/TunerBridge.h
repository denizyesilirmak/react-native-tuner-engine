#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef void (^PitchEventCallback)(NSDictionary* event);
// Band levels in dBFS, low to high. `bands` is only valid during the call.
typedef void (^SpectrumEventCallback)(const float* bands, int count);

@interface TunerBridge : NSObject

@property(nonatomic, copy, nullable) PitchEventCallback onPitch;
@property(nonatomic, copy, nullable) SpectrumEventCallback onSpectrum;

- (void)configure:(NSDictionary *)opts;
- (void)startWithCompletion:(void(^)(NSError* _Nullable error))completion;
- (void)stop;
- (void)setA4:(float)hz;
- (void)setInstrument:(NSString *)name;
- (void)setTuning:(NSString *)name;
- (void)setTuningTranspose:(float)semitones;
- (void)setTemperament:(NSString *)name;
- (NSDictionary *)getStatus;

@end

NS_ASSUME_NONNULL_END

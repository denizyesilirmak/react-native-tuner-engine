#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef void (^PitchEventCallback)(NSDictionary* event);

@interface TunerBridge : NSObject

@property(nonatomic, copy, nullable) PitchEventCallback onPitch;

- (void)configure:(NSDictionary *)opts;
- (void)startWithCompletion:(void(^)(NSError* _Nullable error))completion;
- (void)stop;
- (void)setA4:(float)hz;
- (void)setInstrument:(NSString *)name;
- (void)setTuning:(NSString *)name;
- (void)setTemperament:(NSString *)name;
- (NSDictionary *)getStatus;

@end

NS_ASSUME_NONNULL_END

#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface TunerBridge : NSObject

- (void)configure:(NSDictionary *)opts;
- (void)start;
- (void)stop;
- (void)setA4:(float)hz;
- (void)setInstrument:(NSString *)name;
- (void)setTemperament:(NSString *)name;
- (NSDictionary *)getStatus;

@end

NS_ASSUME_NONNULL_END

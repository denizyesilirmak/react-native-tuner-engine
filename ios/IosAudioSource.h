#pragma once

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef void (^AudioSamplesCallback)(const float* samples, int count, float sampleRate);

// Manages AVAudioSession + AVAudioEngine mic capture.
// Delivers interleaved float32 mono samples to the callback on AVAudioEngine's real-time thread.
// The callback must not block or allocate.
@interface IosAudioSource : NSObject

@property(nonatomic, copy, nullable) AudioSamplesCallback onSamples;

- (BOOL)startWithError:(NSError **)error;
- (void)stop;
- (float)sampleRate;
// NO once capture has ended for good without stop being called: an
// interruption that did not resume, or a restart that failed.
- (BOOL)isAlive;

@end

NS_ASSUME_NONNULL_END

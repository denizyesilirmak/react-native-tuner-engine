#import "IosAudioSource.h"
#import <AVFoundation/AVFoundation.h>

@implementation IosAudioSource {
  AVAudioEngine* _engine;
  AVAudioInputNode* _inputNode;
  float _sampleRate;
  BOOL _isRunning;
}

- (instancetype)init {
  self = [super init];
  if (self) {
    _sampleRate = 48000.0f;
    _isRunning = NO;
    [self registerNotifications];
  }
  return self;
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [self stop];
}

- (float)sampleRate {
  return _sampleRate;
}

- (BOOL)startWithError:(NSError **)error {
  if (_isRunning) return YES;

  AVAudioSession* session = [AVAudioSession sharedInstance];

  NSError* sessionError = nil;
  [session setCategory:AVAudioSessionCategoryPlayAndRecord
           withOptions:AVAudioSessionCategoryOptionDefaultToSpeaker |
                       AVAudioSessionCategoryOptionAllowBluetooth
                 error:&sessionError];
  if (sessionError) {
    if (error) *error = sessionError;
    return NO;
  }

  [session setMode:AVAudioSessionModeMeasurement error:nil];
  [session setPreferredSampleRate:48000.0 error:nil];
  [session setPreferredIOBufferDuration:0.0213 error:nil];
  [session setActive:YES error:&sessionError];
  if (sessionError) {
    if (error) *error = sessionError;
    return NO;
  }

  _sampleRate = (float)session.sampleRate;
  _engine = [[AVAudioEngine alloc] init];
  _inputNode = _engine.inputNode;

  AVAudioFormat* recordingFormat =
    [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
                                     sampleRate:_sampleRate
                                       channels:1
                                    interleaved:YES];

  __weak __typeof__(self) weakSelf = self;

  [_inputNode installTapOnBus:0
                   bufferSize:2048
                       format:recordingFormat
                        block:^(AVAudioPCMBuffer* buffer, AVAudioTime* when) {
    (void)when;
    __strong __typeof__(weakSelf) strongSelf = weakSelf;
    if (!strongSelf || !strongSelf->_isRunning) return;

    const float* data = buffer.floatChannelData[0];
    const AVAudioFrameCount frames = buffer.frameLength;

    AudioSamplesCallback cb = strongSelf.onSamples;
    if (cb && frames > 0) {
      cb(data, (int)frames, strongSelf->_sampleRate);
    }
  }];

  NSError* engineError = nil;
  [_engine prepare];
  [_engine startAndReturnError:&engineError];
  if (engineError) {
    if (error) *error = engineError;
    [_inputNode removeTapOnBus:0];
    _engine = nil;
    return NO;
  }

  _isRunning = YES;
  return YES;
}

- (void)stop {
  if (!_isRunning) return;
  _isRunning = NO;

  if (_inputNode) {
    [_inputNode removeTapOnBus:0];
  }
  if (_engine && _engine.isRunning) {
    [_engine stop];
  }
  _engine = nil;
  _inputNode = nil;

  [[AVAudioSession sharedInstance] setActive:NO
    withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
           error:nil];
}

#pragma mark - Notifications

- (void)registerNotifications {
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(handleInterruption:)
           name:AVAudioSessionInterruptionNotification
         object:nil];

  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(handleRouteChange:)
           name:AVAudioSessionRouteChangeNotification
         object:nil];
}

- (void)handleInterruption:(NSNotification *)notification {
  NSDictionary* info = notification.userInfo;
  AVAudioSessionInterruptionType type =
    (AVAudioSessionInterruptionType)[info[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];

  if (type == AVAudioSessionInterruptionTypeBegan) {
    [self stop];
  } else if (type == AVAudioSessionInterruptionTypeEnded) {
    AVAudioSessionInterruptionOptions options =
      (AVAudioSessionInterruptionOptions)[info[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue];
    if (options & AVAudioSessionInterruptionOptionShouldResume) {
      NSError* err = nil;
      [self startWithError:&err];
    }
  }
}

- (void)handleRouteChange:(NSNotification *)notification {
  NSDictionary* info = notification.userInfo;
  AVAudioSessionRouteChangeReason reason =
    (AVAudioSessionRouteChangeReason)[info[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];

  if (reason == AVAudioSessionRouteChangeReasonOldDeviceUnavailable) {
    [self stop];
    NSError* err = nil;
    [self startWithError:&err];
  }
}

@end

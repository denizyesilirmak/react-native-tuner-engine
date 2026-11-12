#import "IosAudioSource.h"
#import <AVFoundation/AVFoundation.h>

@implementation IosAudioSource {
  AVAudioEngine* _engine;
  AVAudioInputNode* _inputNode;
  float _sampleRate;
  BOOL _isRunning;
  BOOL _interrupted; // stopped by an interruption, to resume when it ends
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

// start/stop come from the module's queue while the session notifications
// arrive on whatever thread posts them; one lock keeps the engine consistent.
- (BOOL)startWithError:(NSError **)error {
  @synchronized (self) {
    return [self startLockedWithError:error];
  }
}

- (BOOL)startLockedWithError:(NSError **)error {
  if (_isRunning) return YES;

  AVAudioSession* session = [AVAudioSession sharedInstance];

  NSError* sessionError = nil;
  [session setCategory:AVAudioSessionCategoryPlayAndRecord
           // A2DP only: plain AllowBluetooth lets a connected headset take
           // over the microphone (hands-free profile, 16 kHz, far from the
           // instrument). This keeps the built-in mic and still lets the
           // reference notes play through the headphones.
           withOptions:AVAudioSessionCategoryOptionDefaultToSpeaker |
                       AVAudioSessionCategoryOptionAllowBluetoothA2DP
                 error:&sessionError];
  if (sessionError) {
    if (error) *error = sessionError;
    return NO;
  }

  // iOS mutes haptics and system sounds while a record-capable session is
  // active unless opted in, which silenced the host app's in-tune feedback.
  [session setAllowHapticsAndSystemSoundsDuringRecording:YES error:&sessionError];
  if (sessionError) {
    NSLog(@"[IosAudioSource] setAllowHapticsAndSystemSoundsDuringRecording error (non-fatal): %@", sessionError.localizedDescription);
    sessionError = nil;
  }

  [session setMode:AVAudioSessionModeMeasurement error:&sessionError];
  if (sessionError) {
    NSLog(@"[IosAudioSource] setMode error (non-fatal): %@", sessionError.localizedDescription);
    sessionError = nil;
  }

  [session setPreferredSampleRate:48000.0 error:&sessionError];
  if (sessionError) {
    NSLog(@"[IosAudioSource] setPreferredSampleRate error (non-fatal): %@", sessionError.localizedDescription);
    sessionError = nil;
  }

  [session setPreferredIOBufferDuration:0.0213 error:&sessionError];
  if (sessionError) {
    NSLog(@"[IosAudioSource] setPreferredIOBufferDuration error (non-fatal): %@", sessionError.localizedDescription);
    sessionError = nil;
  }
  [session setActive:YES error:&sessionError];
  if (sessionError) {
    if (error) *error = sessionError;
    return NO;
  }

  _engine = [[AVAudioEngine alloc] init];
  _inputNode = _engine.inputNode;

  // The tap must run at the input hardware's rate, which is not always the
  // session's: a Mac mic in the simulator or a Bluetooth headset (16-24 kHz)
  // differs, and a mismatched tap format throws an NSException that kills
  // the app. No input at all reports 0 Hz; fail instead of crashing.
  const double hwRate = [_inputNode inputFormatForBus:0].sampleRate;
  if (hwRate <= 0) {
    if (error) {
      *error = [NSError errorWithDomain:@"TunerEngine"
                                   code:-1
                               userInfo:@{NSLocalizedDescriptionKey: @"No audio input available"}];
    }
    _engine = nil;
    _inputNode = nil;
    return NO;
  }
  _sampleRate = (float)hwRate;

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

  // The hardware format can change under a running engine (a headset
  // plugged in or connected); the engine then stops itself and says so here.
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(handleConfigurationChange:)
           name:AVAudioEngineConfigurationChangeNotification
         object:_engine];

  _isRunning = YES;
  return YES;
}

- (void)stop {
  @synchronized (self) {
    _interrupted = NO;
    [self stopLockedReleasingSession:YES];
  }
}

// releaseSession NO: a restart follows at once, so the session stays as it is.
- (void)stopLockedReleasingSession:(BOOL)releaseSession {
  if (!_isRunning) return;
  _isRunning = NO;

  [[NSNotificationCenter defaultCenter]
    removeObserver:self
              name:AVAudioEngineConfigurationChangeNotification
            object:nil];

  if (_inputNode) {
    [_inputNode removeTapOnBus:0];
  }
  if (_engine && _engine.isRunning) {
    [_engine stop];
  }
  _engine = nil;
  _inputNode = nil;

  if (!releaseSession) return;

  AVAudioSession* session = [AVAudioSession sharedInstance];
  [session setActive:NO
         withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
               error:nil];
  // Hand the session back as a plain playback one. Left on PlayAndRecord +
  // Measurement, whatever the host app plays next without setting its own
  // category inherits the record route and the unprocessed, quieter output.
  [session setCategory:AVAudioSessionCategoryPlayback
                  mode:AVAudioSessionModeDefault
               options:0
                 error:nil];
}

// Stop and start again on the current route, picking up its sample rate.
- (void)restart {
  @synchronized (self) {
    if (!_isRunning) return;
    [self stopLockedReleasingSession:NO];
    NSError* err = nil;
    if (![self startLockedWithError:&err]) {
      NSLog(@"[IosAudioSource] restart failed: %@", err.localizedDescription);
    }
  }
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
    @synchronized (self) {
      _interrupted = _isRunning;
      [self stopLockedReleasingSession:NO];
    }
  } else if (type == AVAudioSessionInterruptionTypeEnded) {
    @synchronized (self) {
      if (!_interrupted) return; // stopped by the caller meanwhile
      _interrupted = NO;
    }
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
    // Off the posting thread: the engine must not be torn down from inside
    // its own notification.
    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf restart];
    });
  }
}

- (void)handleConfigurationChange:(NSNotification *)notification {
  id changedEngine = notification.object;
  __weak __typeof__(self) weakSelf = self;
  dispatch_async(dispatch_get_main_queue(), ^{
    __strong __typeof__(weakSelf) strongSelf = weakSelf;
    if (!strongSelf) return;
    @synchronized (strongSelf) {
      // Stale: that engine was already replaced, or the route handler has
      // restarted it in the meantime.
      if (changedEngine != strongSelf->_engine || strongSelf->_engine.isRunning) return;
    }
    [strongSelf restart];
  });
}

@end

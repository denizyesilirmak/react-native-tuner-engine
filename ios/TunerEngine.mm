#import "TunerEngine.h"
#import "TunerBridge.h"
#import <AVFoundation/AVFoundation.h>

@implementation TunerEngine {
  TunerBridge* _tunerBridge;
}

RCT_EXPORT_MODULE(TunerEngine)

- (instancetype)init {
  self = [super init];
  if (self) {
    NSLog(@"[TunerEngine] init");
    _tunerBridge = [[TunerBridge alloc] init];

    __weak __typeof__(self) weakSelf = self;
    _tunerBridge.onPitch = ^(NSDictionary* event) {
      __strong __typeof__(weakSelf) strongSelf = weakSelf;
      if (!strongSelf) return;
      static int n = 0;
      if (++n <= 5) {
        NSLog(@"[TunerEngine] onPitch #%d hasPitch=%@ cbSet=%d", n, event[@"hasPitch"], !!strongSelf->_eventEmitterCallback);
      }
      if (!strongSelf->_eventEmitterCallback) return;
      strongSelf->_eventEmitterCallback("onPitch", event);
    };
  }
  return self;
}

- (void)setEventEmitterCallback:(EventEmitterCallbackWrapper *)eventEmitterCallbackWrapper {
  [super setEventEmitterCallback:eventEmitterCallbackWrapper];
  NSLog(@"[TunerEngine] setEventEmitterCallback called — cbSet=%d", !!_eventEmitterCallback);
}

- (void)addListener:(NSString *)eventName {}
- (void)removeListeners:(double)count {}

- (void)configure:(NSDictionary *)opts
          resolve:(RCTPromiseResolveBlock)resolve
           reject:(RCTPromiseRejectBlock)reject {
  [_tunerBridge configure:opts];
  resolve(nil);
}

- (void)start:(RCTPromiseResolveBlock)resolve
       reject:(RCTPromiseRejectBlock)reject {
  [_tunerBridge startWithCompletion:^(NSError* error) {
    if (error) {
      reject(@"START_ERROR", error.localizedDescription, error);
    } else {
      resolve(nil);
    }
  }];
}

- (void)stop:(RCTPromiseResolveBlock)resolve
      reject:(RCTPromiseRejectBlock)reject {
  [_tunerBridge stop];
  resolve(nil);
}

- (void)setA4:(double)hz {
  [_tunerBridge setA4:(float)hz];
}

- (void)setInstrument:(NSString *)name {
  [_tunerBridge setInstrument:name];
}

- (void)setTemperament:(NSString *)name {
  [_tunerBridge setTemperament:name];
}

- (void)requestPermission:(RCTPromiseResolveBlock)resolve
                   reject:(RCTPromiseRejectBlock)reject {
  [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
    resolve(@(granted));
  }];
}

- (NSDictionary *)getStatus {
  return [_tunerBridge getStatus];
}

+ (BOOL)requiresMainQueueSetup {
  return NO;
}

- (std::shared_ptr<facebook::react::TurboModule>)getTurboModule:
    (const facebook::react::ObjCTurboModule::InitParams &)params
{
  return std::make_shared<facebook::react::NativeTunerEngineSpecJSI>(params);
}

@end

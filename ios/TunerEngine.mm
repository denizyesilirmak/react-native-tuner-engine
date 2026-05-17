#import "TunerEngine.h"
#import "TunerBridge.h"

@implementation TunerEngine {
  TunerBridge* _bridge;
}

- (instancetype)init {
  self = [super init];
  if (self) {
    _bridge = [[TunerBridge alloc] init];

    __weak typeof(self) weakSelf = self;
    _bridge.onPitch = ^(NSDictionary* event) {
      __strong typeof(weakSelf) strongSelf = weakSelf;
      if (!strongSelf) return;
      [strongSelf sendEventWithName:@"onPitch" body:event];
    };
  }
  return self;
}

- (NSArray<NSString *> *)supportedEvents {
  return @[@"onPitch"];
}

- (void)configure:(NSDictionary *)opts
          resolve:(RCTPromiseResolveBlock)resolve
           reject:(RCTPromiseRejectBlock)reject {
  [_bridge configure:opts];
  resolve(nil);
}

- (void)start:(RCTPromiseResolveBlock)resolve
       reject:(RCTPromiseRejectBlock)reject {
  [_bridge startWithCompletion:^(NSError* error) {
    if (error) {
      reject(@"START_ERROR", error.localizedDescription, error);
    } else {
      resolve(nil);
    }
  }];
}

- (void)stop:(RCTPromiseResolveBlock)resolve
      reject:(RCTPromiseRejectBlock)reject {
  [_bridge stop];
  resolve(nil);
}

- (void)setA4:(double)hz {
  [_bridge setA4:(float)hz];
}

- (void)setInstrument:(NSString *)name {
  [_bridge setInstrument:name];
}

- (void)setTemperament:(NSString *)name {
  [_bridge setTemperament:name];
}

- (void)requestPermission:(RCTPromiseResolveBlock)resolve
                   reject:(RCTPromiseRejectBlock)reject {
  [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
    resolve(@(granted));
  }];
}

- (NSDictionary *)getStatus {
  return [_bridge getStatus];
}

// RCTEventEmitter overrides — addListener/removeListeners are inherited
+ (BOOL)requiresMainQueueSetup {
  return NO;
}

- (std::shared_ptr<facebook::react::TurboModule>)getTurboModule:
    (const facebook::react::ObjCTurboModule::InitParams &)params
{
  return std::make_shared<facebook::react::NativeTunerEngineSpecJSI>(params);
}

+ (NSString *)moduleName {
  return @"TunerEngine";
}

@end

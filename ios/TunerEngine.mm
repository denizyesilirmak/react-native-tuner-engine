#import "TunerEngine.h"
#import "TunerBridge.h"
#import <AVFoundation/AVFoundation.h>
#import <ReactCommon/RCTTurboModule.h>
#import <ReactCommon/CallInvoker.h>
#import <jsi/jsi.h>

@implementation TunerEngine {
  TunerBridge* _tunerBridge;
  std::shared_ptr<facebook::react::CallInvoker> _jsInvoker;
}

RCT_EXPORT_MODULE(TunerEngine)

- (instancetype)init {
  self = [super init];
  if (self) {
    [super addListener:@"onPitch"]; // keeps _listenerCount=1 for old-arch fallback
    _tunerBridge = [[TunerBridge alloc] init];
  }
  return self;
}

- (NSArray<NSString *> *)supportedEvents {
  return @[@"onPitch"];
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
  _jsInvoker = params.jsInvoker;

  __weak __typeof__(self) weakSelf = self;
  _tunerBridge.onPitch = ^(NSDictionary* event) {
    __strong __typeof__(weakSelf) strongSelf = weakSelf;
    if (!strongSelf) return;

    auto jsInvoker = strongSelf->_jsInvoker;
    if (!jsInvoker) {
      [strongSelf sendEventWithName:@"onPitch" body:event];
      return;
    }

    // Extract primitives before crossing thread boundary
    bool hasPitch     = [event[@"hasPitch"] boolValue];
    double frequency  = [event[@"frequency"] doubleValue];
    double confidence = [event[@"confidence"] doubleValue];
    double rmsDb      = [event[@"rmsDb"] doubleValue];
    std::string note  = std::string([event[@"noteName"] UTF8String]);
    int octave        = [event[@"octave"] intValue];
    double cents      = [event[@"cents"] doubleValue];

    jsInvoker->invokeAsync([=](facebook::jsi::Runtime& rt) {
      auto cb = rt.global().getProperty(rt, "__tunerEngineOnPitch");
      if (!cb.isObject()) return;
      auto fn = cb.asObject(rt);
      if (!fn.isFunction(rt)) return;

      facebook::jsi::Object obj(rt);
      obj.setProperty(rt, "hasPitch",   facebook::jsi::Value(hasPitch));
      obj.setProperty(rt, "frequency",  facebook::jsi::Value(frequency));
      obj.setProperty(rt, "confidence", facebook::jsi::Value(confidence));
      obj.setProperty(rt, "rmsDb",      facebook::jsi::Value(rmsDb));
      obj.setProperty(rt, "noteName",
          facebook::jsi::String::createFromUtf8(rt, note));
      obj.setProperty(rt, "octave",     facebook::jsi::Value(octave));
      obj.setProperty(rt, "cents",      facebook::jsi::Value(cents));

      fn.asFunction(rt).call(rt, std::move(obj));
    });
  };

  return std::make_shared<facebook::react::NativeTunerEngineSpecJSI>(params);
}

@end

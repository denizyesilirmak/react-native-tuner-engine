#import "TunerEngine.h"
#import "TunerBridge.h"

@implementation TunerEngine {
  TunerBridge *_bridge;
}

- (instancetype)init {
  self = [super init];
  if (self) {
    _bridge = [[TunerBridge alloc] init];
  }
  return self;
}

- (void)configure:(NSDictionary *)opts
         resolve:(RCTPromiseResolveBlock)resolve
          reject:(RCTPromiseRejectBlock)reject {
  [_bridge configure:opts];
  resolve(nil);
}

- (void)start:(RCTPromiseResolveBlock)resolve
       reject:(RCTPromiseRejectBlock)reject {
  [_bridge start];
  resolve(nil);
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
  resolve(@(YES));
}

- (NSDictionary *)getStatus {
  return [_bridge getStatus];
}

- (void)addListener:(NSString *)eventName {}

- (void)removeListeners:(double)count {}

- (std::shared_ptr<facebook::react::TurboModule>)getTurboModule:
    (const facebook::react::ObjCTurboModule::InitParams &)params
{
  return std::make_shared<facebook::react::NativeTunerEngineSpecJSI>(params);
}

+ (NSString *)moduleName
{
  return @"TunerEngine";
}

@end

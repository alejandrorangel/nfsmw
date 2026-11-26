// MetalRenderer - the Metal backend that will eventually sit under the
// Ecstasy (eView/eSolid/Texture) platform layer. For now it only clears the
// drawable so the app/window/device plumbing can be validated.
#pragma once

#import <MetalKit/MetalKit.h>

@interface MetalRenderer : NSObject <MTKViewDelegate>
- (nonnull instancetype)initWithView:(nonnull MTKView *)view;
@end

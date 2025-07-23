#define GLFW_EXPOSE_NATIVE_COCOA true

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#import <Metal/Metal.hpp> // C++ wrapper (metal-cpp)
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

extern "C" void* createMetalLayer(GLFWwindow* window, void* devicePtr) {
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    NSView* contentView = [nsWindow contentView];
    [contentView setWantsLayer:YES];

    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    metalLayer.device = (__bridge id<MTLDevice>)devicePtr;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metalLayer.framebufferOnly = YES;
    metalLayer.contentsScale = [contentView window].backingScaleFactor;
    metalLayer.drawableSize = contentView.bounds.size;

    [contentView setLayer:metalLayer];

    return (__bridge_retained void*)metalLayer;
}

using namespace MTL;

struct MetalDrawableInfo {
    MTL::Drawable* drawable;
    MTL::Texture* texture;
};

extern "C" MetalDrawableInfo getNextDrawableWithTexture(void* layerPtr) {
    CAMetalLayer* layer = (__bridge CAMetalLayer*)layerPtr;
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable) return { nullptr, nullptr };

    const void* retainedDrawable = CFBridgingRetain((id<MTLDrawable>)drawable);
    const void* retainedTexture = CFBridgingRetain(drawable.texture);

    return {
        const_cast<MTL::Drawable*>(static_cast<const MTL::Drawable*>(retainedDrawable)),
        const_cast<MTL::Texture*>(static_cast<const MTL::Texture*>(retainedTexture))
    };
}
// MetalBackend.mm
#import <Cocoa/Cocoa.h>          // <-- gives you NSView
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import "MetalBackend.hpp"

struct MetalBackend {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    CAMetalLayer* layer;
    int width, height;
};

MetalBackend* metal_backend_create(void* nativeHandle) {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();

    MetalBackend* backend = new MetalBackend();
    backend->device = device;
    backend->queue = [device newCommandQueue];

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;

    NSObject* obj = (__bridge NSObject*)nativeHandle;
    NSView* targetView = nil;

    if ([obj isKindOfClass:[NSWindow class]]) {
        NSWindow* win = (NSWindow*)obj;
        targetView = [win contentView];
    } else if ([obj isKindOfClass:[NSView class]]) {
        targetView = (NSView*)obj;
    }

    if (targetView) {
        targetView.layer = layer;
        targetView.wantsLayer = YES;
    } else {
        NSLog(@"[MetalBackend] Invalid handle passed (not NSWindow or NSView)");
    }

    backend->layer = layer;
    return backend;
}


void metal_backend_destroy(MetalBackend* b) {
    delete b;
}

void metal_backend_resize(MetalBackend* b, int w, int h) {
    b->width = w;
    b->height = h;
    b->layer.drawableSize = CGSizeMake(w, h);
}

void metal_backend_clear(MetalBackend* b, float r, float g, float bcol, float a) {
    id<CAMetalDrawable> drawable = [b->layer nextDrawable];
    if (!drawable) return;

    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = drawable.texture;
    desc.colorAttachments[0].loadAction = MTLLoadActionClear;
    desc.colorAttachments[0].clearColor = MTLClearColorMake(r, g, bcol, a);
    desc.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLCommandBuffer> cmd = [b->queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:desc];
    [enc endEncoding];
    [cmd presentDrawable:drawable];
    [cmd commit];
}

void metal_backend_present(MetalBackend* b) {
    // No-op, present is done in clear for now
}

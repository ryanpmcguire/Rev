// MetalBackend.mm
#import <Cocoa/Cocoa.h>          // <-- gives you NSView
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import "MetalBackend.hpp"

struct MetalContext {

    id<MTLDevice> device;

    id<CAMetalDrawable> drawable;
    id<MTLCommandQueue> queue;
    id<MTLCommandBuffer> cmd;
    id<MTLRenderCommandEncoder> enc;

    CAMetalLayer* layer;
    int width, height;

    float r, g, b, a;
};

struct MetalShader {
    id<MTLLibrary> library;
    id<MTLFunction> vertexFn;
    id<MTLFunction> fragmentFn;
};

MetalContext* metal_context_create(void* nativeHandle) {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();

    MetalContext* context = new MetalContext();
    context->device = device;
    context->queue = [device newCommandQueue];

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

    context->layer = layer;

    context->r = 1.0f;
    context->g = 0.0f;
    context->b = 0.0f;
    context->a = 1.0f;

    return context;
}

void metal_context_destroy(MetalContext* c) {
    delete c;
}

void metal_context_resize(MetalContext* c, int w, int h) {

    c->width = w;
    c->height = h;

    c->layer.drawableSize = CGSizeMake(w, h);
}

// Begin frame (prepare context)
void metal_begin_frame(MetalContext* c) {

    c->drawable = [c->layer nextDrawable];
    
    if (!c->drawable) {
        NSLog(@"No drawable this frame");
        return;
    }

    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = c->drawable.texture;
    desc.colorAttachments[0].loadAction = MTLLoadActionClear;
    desc.colorAttachments[0].clearColor = MTLClearColorMake(c->r, c->g, c->b, c->a);
    desc.colorAttachments[0].storeAction = MTLStoreActionStore;

    c->cmd = [c->queue commandBuffer];
    c->enc = [c->cmd renderCommandEncoderWithDescriptor:desc];

    MTLViewport vp = {
        0.0, 0.0,
        (double)c->width, (double)c->height,
        0.0, 1.0
    };

    [c->enc setViewport:vp];

    MTLScissorRect scissor = {
        0, 0,
        (NSUInteger)c->width, (NSUInteger)c->height
    };

    [c->enc setScissorRect:scissor];
}

// End frame (show result)
void metal_end_frame(MetalContext* c) {

    [c->enc endEncoding];
    [c->cmd presentDrawable:c->drawable];
    [c->cmd commit];
    [c->cmd waitUntilCompleted];
}

// Uniform Buffer
//--------------------------------------------------

void* metal_create_uniform_buffer(MetalContext* ctx, size_t size) {

    if (!ctx || !ctx->device) return nullptr;

    id<MTLBuffer> buf = [ctx->device newBufferWithLength:size
                                                 options:MTLResourceStorageModeShared];
    if (!buf) return nullptr;

    // Return as opaque pointer, retaining ownership
    return (__bridge_retained void*)buf;
}

void metal_destroy_uniform_buffer(void* buffer) {

    if (!buffer) return;

    // Transfer ownership back, dropping ref
    id<MTLBuffer> buf = (__bridge_transfer id<MTLBuffer>)buffer;
    (void)buf; // ARC/release cleans up
}

void* metal_map_uniform_buffer(void* buffer) {

    if (!buffer) return nullptr;

    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)buffer;
    return [buf contents]; // CPU-visible pointer
}

void metal_bind_uniform_buffer(MetalContext* ctx, void* buffer, int index) {

    if (!ctx || !ctx->enc || !buffer) {
        
        NSLog(@"Couldn't bind uniform buffer!");

        return;
    }

    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)buffer;
    id<MTLRenderCommandEncoder> enc = ctx->enc;

    [enc setVertexBuffer:buf offset:0 atIndex:index];
    [enc setFragmentBuffer:buf offset:0 atIndex:index];
}

// Vertex Buffer
//--------------------------------------------------

void* metal_create_vertex_buffer(MetalContext* ctx, size_t size) {

    if (!ctx || !ctx->device) return nullptr;

    id<MTLBuffer> buf = [ctx->device newBufferWithLength:size
                                                 options:MTLResourceStorageModeShared];
    return (__bridge_retained void*)buf;
}

void metal_destroy_vertex_buffer(void* buffer) {

    if (!buffer) return;

    id<MTLBuffer> buf = (__bridge_transfer id<MTLBuffer>)buffer;
    (void)buf; // ARC handles release
}

void* metal_map_vertex_buffer(void* buffer) {

    if (!buffer) return nullptr;

    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)buffer;
    return [buf contents];
}

void metal_bind_vertex_buffer(MetalContext* ctx, void* buffer, int index) {

    if (!ctx || !ctx->enc || !buffer) {

        NSLog(@"Couldn't bind vertex buffer!");

        return;
    }

    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)buffer;
    id<MTLRenderCommandEncoder> enc = ctx->enc;

    [enc setVertexBuffer:buf offset:0 atIndex:index];
}

// Pipeline
//--------------------------------------------------

void* metal_create_pipeline(MetalContext* ctx, MetalShader* shader) {

    if (!ctx || !ctx->device) {
        NSLog(@"Failed to create pipeline: no context/device");
        return nullptr;
    };

    // Hard-coded vertex descriptor: float3 positions only
    MTLVertexDescriptor* vdesc = [MTLVertexDescriptor vertexDescriptor];

    vdesc.attributes[0].format = MTLVertexFormatFloat2;
    vdesc.attributes[0].offset = 0;
    vdesc.attributes[0].bufferIndex = 0;

    vdesc.layouts[0].stride = sizeof(float) * 2;
    vdesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    NSLog(@"VertexFn = %@", shader->vertexFn);
    NSLog(@"FragmentFn = %@", shader->fragmentFn);

    MTLRenderPipelineDescriptor* pdesc = [[MTLRenderPipelineDescriptor alloc] init];
    pdesc.vertexFunction   = shader->vertexFn;
    pdesc.fragmentFunction = shader->fragmentFn;
    pdesc.vertexDescriptor = nil;
    pdesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    NSError* err = nil;
    id<MTLRenderPipelineState> pso =
        [ctx->device newRenderPipelineStateWithDescriptor:pdesc error:&err];
    if (!pso) {
        NSLog(@"[Metal] Failed to create pipeline: %@", err);
        return nullptr;
    }

    return (__bridge_retained void*)pso;
}

void metal_destroy_pipeline(void* pipeline) {

    if (!pipeline) return;

    id<MTLRenderPipelineState> pso =
        (__bridge_transfer id<MTLRenderPipelineState>)pipeline;
    (void)pso; // ARC drops ref
}

void metal_bind_pipeline(MetalContext* ctx, void* pipeline) {

    if (!ctx || !ctx->enc || !pipeline) {
        
        NSLog(@"Couldn't bind vertex buffer!");

        return;
    };

    id<MTLRenderPipelineState> pso =
        (__bridge id<MTLRenderPipelineState>)pipeline;
    [ctx->enc setRenderPipelineState:pso];
}

// Shader
//--------------------------------------------------

void* metal_create_shader(MetalContext* ctx, const char* source, size_t length) {

    if (!ctx || !ctx->device) return nullptr;

    NSString* src = [[NSString alloc] initWithBytes:source
                                             length:length
                                           encoding:NSUTF8StringEncoding];

    NSError* err = nil;
    id<MTLLibrary> lib = [ctx->device newLibraryWithSource:src options:nil error:&err];
    if (!lib) {
        NSLog(@"[Metal] Shader compile failed: %@", err);
        return nullptr;
    }

    MetalShader* shader = new MetalShader();
    shader->library = lib;

    shader->vertexFn   = [lib newFunctionWithName:@"vertex_main"];
    shader->fragmentFn = [lib newFunctionWithName:@"fragment_main"];

    if (!shader->vertexFn || !shader->fragmentFn) {
        NSLog(@"[Metal] Missing entry points in shader source");
        delete shader;
        return nullptr;
    }

    return shader;
}

void metal_destroy_shader(void* shader) {
    if (!shader) return;
    id<MTLFunction> fn = (__bridge_transfer id<MTLFunction>)shader;
    (void)fn; // ARC release
}

// Functions
//--------------------------------------------------

void metal_draw_arrays_instanced(MetalContext* ctx,
                                 int topology,     // your enum -> Metal primitive type
                                 size_t start,
                                 size_t verticesPer,
                                 size_t numInstances) {

    if (!ctx || !ctx->enc) {
        NSLog(@"Couldn't draw instanced arrays!");
        return;
    };

    [ctx->enc drawPrimitives:MTLPrimitiveTypeTriangle
                 vertexStart:(NSUInteger)start
                 vertexCount:(NSUInteger)verticesPer
               instanceCount:(NSUInteger)numInstances];
}

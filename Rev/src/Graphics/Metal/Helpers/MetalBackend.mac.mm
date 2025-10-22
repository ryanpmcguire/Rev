// MetalBackend.mm
#import <vector>
#import <Cocoa/Cocoa.h>          // <-- gives you NSView
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import "MetalBackend.hpp"

struct MetalContext {

    NSView* view;

    id<MTLDevice> device;

    id<CAMetalDrawable> drawable;
    id<MTLCommandQueue> queue;
    id<MTLCommandBuffer> cmd;
    id<MTLRenderCommandEncoder> enc;

    CAMetalLayer* layer;

    int width, height;
    float scale;

    float r, g, b, a;
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

    context->view = targetView;
    context->layer = layer;

    CGFloat scale = context->view.window.backingScaleFactor;
    context->scale = scale;

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

    CGFloat scale = c->view.window.backingScaleFactor;
    c->scale = scale;
    
    c->layer.drawableSize = CGSizeMake(w * scale, h * scale);
}

float metal_context_get_scale(MetalContext* c) {
    return c->scale;
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
        (double)c->width * c->scale, (double)c->height * c->scale,
        0.0, 1.0
    };

    [c->enc setViewport:vp];

    MTLScissorRect scissor = {
        0, 0,
        (NSUInteger)(c->width * c->scale), (NSUInteger)(c->height * c->scale)
    };

    [c->enc setScissorRect:scissor];
}

// End frame (show result)
void metal_end_frame(MetalContext* c) {
    
    [c->enc endEncoding];

    if (c->drawable == nil) {
        NSLog(@"[Metal] Warning: No drawable available this frame.");
    } else {
        [c->cmd presentDrawable:c->drawable];
    }

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

// Texture
//--------------------------------------------------

struct MetalTexture {
    id<MTLTexture> tex;
    id<MTLSamplerState> sampler;
    size_t width, height, channels;
};

void* metal_create_texture(MetalContext* ctx,
                                      const unsigned char* data,
                                      size_t width,
                                      size_t height,
                                      size_t channels)
{

    if (!ctx || !ctx->device) return nullptr;

    // Pick a pixel format
    MTLPixelFormat format = MTLPixelFormatR8Unorm;
    if (channels == 4) {
        format = MTLPixelFormatRGBA8Unorm;
    } else if (channels == 3) {
        // Metal doesn’t have RGB8 — must expand later
        format = MTLPixelFormatRGBA8Unorm;
    }

    // Texture descriptor
    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                           width:(NSUInteger)width
                                                          height:(NSUInteger)height
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModeShared;

    id<MTLTexture> tex = [ctx->device newTextureWithDescriptor:desc];
    if (!tex) return nullptr;

    // Upload pixel data
    MTLRegion region = { {0,0,0}, { (NSUInteger)width, (NSUInteger)height, 1 } };

    if (channels == 1) {
        [tex replaceRegion:region mipmapLevel:0 withBytes:data bytesPerRow:width * 1];
    }
    
    else if (channels == 3) {

        // Expand RGB → RGBA
        std::vector<unsigned char> rgba(width * height * 4);

        for (size_t i=0; i<width*height; i++) {
            rgba[i*4+0] = data[i*3+0];
            rgba[i*4+1] = data[i*3+1];
            rgba[i*4+2] = data[i*3+2];
            rgba[i*4+3] = 255;
        }

        [tex replaceRegion:region mipmapLevel:0 withBytes:rgba.data() bytesPerRow:width*4];

    }
    
    else if (channels == 4) {
        [tex replaceRegion:region mipmapLevel:0 withBytes:data bytesPerRow:width*4];
    }

    // Sampler state
    MTLSamplerDescriptor* sdesc = [[MTLSamplerDescriptor alloc] init];
    sdesc.minFilter   = MTLSamplerMinMagFilterNearest;
    sdesc.magFilter   = MTLSamplerMinMagFilterNearest;
    sdesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sdesc.tAddressMode = MTLSamplerAddressModeClampToEdge;

    id<MTLSamplerState> sampler = [ctx->device newSamplerStateWithDescriptor:sdesc];

    // Wrap up
    MetalTexture* wrapper = new MetalTexture();
    wrapper->tex = tex;
    wrapper->sampler = sampler;
    wrapper->width = width;
    wrapper->height = height;
    wrapper->channels = channels;

    return wrapper;
}

void metal_destroy_texture(void* texture) {
    if (!texture) return;
    MetalTexture* t = static_cast<MetalTexture*>(texture);
    t->tex = nil;
    t->sampler = nil;
    delete t;
}

void metal_bind_texture(MetalContext* ctx, void* texture, int unit) {
    if (!ctx || !ctx->enc || !texture) return;
    MetalTexture* t = static_cast<MetalTexture*>(texture);
    [ctx->enc setFragmentTexture:t->tex atIndex:unit];
    [ctx->enc setFragmentSamplerState:t->sampler atIndex:unit];
}

void metal_unbind_texture(MetalContext* ctx, int unit) {
    if (!ctx || !ctx->enc) return;
    [ctx->enc setFragmentTexture:nil atIndex:unit];
    [ctx->enc setFragmentSamplerState:nil atIndex:unit];
}

// Shader
//--------------------------------------------------

struct MetalShader {
    id<MTLLibrary> library;
    id<MTLFunction> vertexFn;
    id<MTLFunction> fragmentFn;
};

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

// Pipeline
//--------------------------------------------------

void* metal_create_pipeline(MetalContext* ctx,
                            MetalShader* shader,
                            std::vector<float> attribs,
                            bool instanced)
{
    if (!ctx || !ctx->device) {
        NSLog(@"[Metal] Failed to create pipeline: no context/device");
        return nullptr;
    }

    // Create a vertex descriptor only if attributes are defined
    MTLVertexDescriptor* vdesc = nil;

    if (!attribs.empty()) {

        vdesc = [MTLVertexDescriptor vertexDescriptor];

        // Compute stride (sum of attribute sizes * sizeof(float))
        size_t stride = 0;
        for (float a : attribs)
            stride += static_cast<size_t>(a);
        stride *= sizeof(float);

        // Build each vertex attribute
        size_t offset = 0;
        for (NSUInteger i = 0; i < attribs.size(); ++i) {

            size_t comps = static_cast<size_t>(attribs[i]);
            MTLVertexFormat fmt =
                (comps == 1) ? MTLVertexFormatFloat  :
                (comps == 2) ? MTLVertexFormatFloat2 :
                (comps == 3) ? MTLVertexFormatFloat3 :
                               MTLVertexFormatFloat4;

            vdesc.attributes[i].format = fmt;
            vdesc.attributes[i].offset = offset;
            vdesc.attributes[i].bufferIndex = 0; // single interleaved vertex buffer

            offset += comps * sizeof(float);
        }

        vdesc.layouts[0].stride = stride;
        vdesc.layouts[0].stepFunction = instanced
            ? MTLVertexStepFunctionPerInstance
            : MTLVertexStepFunctionPerVertex;
        vdesc.layouts[0].stepRate = 1;
    }

    // Build pipeline descriptor
    MTLRenderPipelineDescriptor* pdesc = [[MTLRenderPipelineDescriptor alloc] init];
    pdesc.vertexFunction   = shader->vertexFn;
    pdesc.fragmentFunction = shader->fragmentFn;
    pdesc.vertexDescriptor = vdesc; // nil OK for no vertex inputs

    // Color attachment setup & blending
    MTLRenderPipelineColorAttachmentDescriptor* colorAttachment = pdesc.colorAttachments[0];
    colorAttachment.pixelFormat = MTLPixelFormatBGRA8Unorm;
    colorAttachment.blendingEnabled = YES;
    colorAttachment.rgbBlendOperation = MTLBlendOperationAdd;
    colorAttachment.alphaBlendOperation = MTLBlendOperationAdd;
    colorAttachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    colorAttachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    colorAttachment.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    colorAttachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    // Create the pipeline state
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
        
        NSLog(@"Couldn't bind pipeline!");

        return;
    };

    id<MTLRenderPipelineState> pso =
        (__bridge id<MTLRenderPipelineState>)pipeline;
    [ctx->enc setRenderPipelineState:pso];
}

// Functions
//--------------------------------------------------

void metal_draw_arrays(MetalContext* ctx,
                                 int topology,     // your enum -> Metal primitive type
                                 size_t start,
                                 size_t verticesPer) {

    if (!ctx || !ctx->enc) {
        NSLog(@"Couldn't draw instanced arrays!");
        return;
    };

    [ctx->enc drawPrimitives:MTLPrimitiveTypeTriangle
                 vertexStart:(NSUInteger)start
                 vertexCount:(NSUInteger)verticesPer];
}

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
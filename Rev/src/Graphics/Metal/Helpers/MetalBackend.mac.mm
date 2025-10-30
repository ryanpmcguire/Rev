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

    // Own cmd and enc
    id<MTLCommandBuffer> _cmd;
    id<MTLRenderCommandEncoder> _enc;

    // Currently bound cmd and enc
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
        NSLog(@"[Metal] No drawable this frame");
        return;
    }

    // Create pass descriptor
    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = c->drawable.texture;
    desc.colorAttachments[0].loadAction = MTLLoadActionClear;
    desc.colorAttachments[0].storeAction = MTLStoreActionStore;
    desc.colorAttachments[0].clearColor = MTLClearColorMake(c->r, c->g, c->b, c->a);

    c->_cmd = [c->queue commandBuffer];
    c->_enc = [c->_cmd renderCommandEncoderWithDescriptor:desc];

    // Set as current active encoder
    c->cmd = c->_cmd;
    c->enc = c->_enc;

    // Viewport and scissor
    MTLViewport vp = { 0.0, 0.0,
                       (double)c->width * c->scale,
                       (double)c->height * c->scale,
                       0.0, 1.0 };
    [c->enc setViewport:vp];

    MTLScissorRect scissor = { 0, 0,
                               (NSUInteger)(c->width * c->scale),
                               (NSUInteger)(c->height * c->scale) };
    [c->enc setScissorRect:scissor];
}

void metal_end_frame(MetalContext* c) {
    if (!c || !c->cmd) return;

    // No need to end encoding again — we ended both render/blit encoders already
    [c->cmd presentDrawable:c->drawable];
    [c->cmd commit];
    [c->cmd waitUntilCompleted];

    c->cmd = nil;
    c->enc = nil;
    c->_cmd = nil;
    c->_enc = nil;
}


// Framebuffer
//--------------------------------------------------

struct MetalFramebuffer {
    id<MTLTexture> color;
    id<MTLTexture> stencil;
    MTLRenderPassDescriptor* desc;
    id<MTLCommandBuffer> cmd;
    id<MTLRenderCommandEncoder> enc;
    size_t width;
    size_t height;
    size_t colorChannels;
};

// Create a new framebuffer
void* metal_create_framebuffer(MetalContext* ctx,
                               size_t width,
                               size_t height,
                               size_t colorChannels)
{
    if (!ctx || !ctx->device) return nullptr;

    MetalFramebuffer* fb = new MetalFramebuffer();
    fb->width = width;
    fb->height = height;
    fb->colorChannels = colorChannels;

    // Apply scaling (so offscreen rendering matches onscreen resolution)
    CGFloat scale = ctx->scale > 0.0 ? ctx->scale : 1.0;
    NSUInteger scaledWidth  = (NSUInteger)(width  * scale);
    NSUInteger scaledHeight = (NSUInteger)(height * scale);

    // --- Color attachment ---
    MTLPixelFormat colorFormat = MTLPixelFormatBGRA8Unorm;
    if (colorChannels == 1) colorFormat = MTLPixelFormatR8Unorm;

    MTLTextureDescriptor* colorDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:colorFormat
                                                           width:scaledWidth
                                                          height:scaledHeight
                                                       mipmapped:NO];

    // Allow both render target and shader sampling (common use)
    colorDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    colorDesc.storageMode = MTLStorageModePrivate;

    fb->color = [ctx->device newTextureWithDescriptor:colorDesc];
    if (!fb->color) {
        NSLog(@"[MetalFramebuffer] Failed to create color texture (%lux%lu)", scaledWidth, scaledHeight);
        delete fb;
        return nullptr;
    }

    // --- Stencil attachment ---
    MTLTextureDescriptor* stencilDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatStencil8
                                                           width:scaledWidth
                                                          height:scaledHeight
                                                       mipmapped:NO];
    stencilDesc.usage = MTLTextureUsageRenderTarget;
    stencilDesc.storageMode = MTLStorageModePrivate;

    fb->stencil = [ctx->device newTextureWithDescriptor:stencilDesc];
    if (!fb->stencil) {
        NSLog(@"[MetalFramebuffer] Failed to create stencil texture");
        fb->color = nil;
        delete fb;
        return nullptr;
    }

    NSLog(@"[MetalFramebuffer] Created (%lux%lu) at scale %.2f",
          scaledWidth, scaledHeight, scale);

    return fb;
}

// Destroy framebuffer
void metal_destroy_framebuffer(void* framebuffer) {
    if (!framebuffer) return;
    MetalFramebuffer* fb = static_cast<MetalFramebuffer*>(framebuffer);
    fb->color = nil;
    fb->stencil = nil;
    fb->desc = nil;
    delete fb;
}

void metal_framebuffer_begin_frame(MetalContext* ctx, void* framebuffer) {
    if (!ctx || !framebuffer) return;
    MetalFramebuffer* fb = static_cast<MetalFramebuffer*>(framebuffer);

    fb->cmd = [ctx->queue commandBuffer];

    // --- Rebuild render pass descriptor every frame ---
    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = fb->color;
    desc.colorAttachments[0].loadAction = MTLLoadActionClear;
    desc.colorAttachments[0].storeAction = MTLStoreActionStore;
    desc.colorAttachments[0].clearColor = MTLClearColorMake(1.0, 1.0, 1.0, 1.0);
    desc.stencilAttachment.texture = fb->stencil;
    desc.stencilAttachment.loadAction = MTLLoadActionClear;
    desc.stencilAttachment.storeAction = MTLStoreActionStore;
    desc.stencilAttachment.clearStencil = 0;

    fb->desc = desc; // update reference

    fb->enc = [fb->cmd renderCommandEncoderWithDescriptor:fb->desc];
    if (!fb->enc) {
        NSLog(@"[MetalFramebuffer] Failed to create encoder for offscreen pass");
        return;
    }

    // --- Apply Retina scaling just like metal_begin_frame ---
    double scaledWidth  = fb->width  * ctx->scale;
    double scaledHeight = fb->height * ctx->scale;

    MTLViewport vp = {0.0, 0.0, scaledWidth, scaledHeight, 0.0, 1.0};
    [fb->enc setViewport:vp];

    MTLScissorRect sc = {0, 0, (NSUInteger)scaledWidth, (NSUInteger)scaledHeight};
    [fb->enc setScissorRect:sc];

    ctx->enc = fb->enc;
    ctx->cmd = fb->cmd;

    NSLog(@"[MetalFramebuffer] Began offscreen frame (%zux%zu @ scale %.2f)",
          fb->width, fb->height, ctx->scale);
}



void metal_framebuffer_end_frame(MetalContext* ctx, void* framebuffer) {

    if (!ctx || !framebuffer) return;
    MetalFramebuffer* fb = static_cast<MetalFramebuffer*>(framebuffer);

    if (!fb->enc || !fb->cmd) return;

    [fb->enc endEncoding];
    [fb->cmd commit];
    [fb->cmd waitUntilCompleted];

    fb->enc = nil;
    fb->cmd = nil;

    // Unbind the framebuffer (back to onscreen context)
    ctx->enc = ctx->_enc;
    ctx->cmd = ctx->_cmd;

    NSLog(@"[MetalFramebuffer] Ended offscreen frame");
}

void metal_framebuffer_blit_to_drawable(MetalContext* ctx, void* framebuffer) {

    if (!ctx || !framebuffer || !ctx->drawable || !ctx->cmd) {
        NSLog(@"[Metal] Blit failed: missing context/drawable/cmd");
        return;
    }

    MetalFramebuffer* fb = static_cast<MetalFramebuffer*>(framebuffer);
    id<MTLTexture> src = fb->color;
    id<MTLTexture> dst = ctx->drawable.texture; // valid: id<CAMetalDrawable> has `texture`

    id<MTLBlitCommandEncoder> blit = [ctx->cmd blitCommandEncoder];
    if (!blit) {
        NSLog(@"[Metal] Failed to create blit encoder");
        return;
    }

    // Region to copy
    MTLOrigin origin = {0, 0, 0};
    MTLSize size = {(NSUInteger)MIN(fb->width, ctx->width),
                    (NSUInteger)MIN(fb->height, ctx->height),
                    1};

    [blit copyFromTexture:src
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:origin
               sourceSize:size
                toTexture:dst
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:origin];

    [blit endEncoding];
}

void metal_present(MetalContext* ctx, void* framebuffer)
{
    if (!ctx || !framebuffer || !ctx->layer) {
        NSLog(@"[Metal] metal_present: invalid context or framebuffer");
        return;
    }

    MetalFramebuffer* fb = static_cast<MetalFramebuffer*>(framebuffer);

    // 1. Acquire a drawable from the layer
    id<CAMetalDrawable> drawable = [ctx->layer nextDrawable];
    if (!drawable) {
        NSLog(@"[Metal] metal_present: no drawable available");
        return;
    }

    // 2. Create a fresh command buffer
    id<MTLCommandBuffer> cmd = [ctx->queue commandBuffer];
    if (!cmd) {
        NSLog(@"[Metal] metal_present: failed to create command buffer");
        return;
    }

    // 3. Create a blit encoder
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    if (!blit) {
        NSLog(@"[Metal] metal_present: failed to create blit encoder");
        return;
    }

    // 4. Copy framebuffer color texture → drawable’s texture
    id<MTLTexture> src = fb->color;
    id<MTLTexture> dst = drawable.texture;

    // Apply Retina scaling when defining the region to copy
    NSUInteger scaledWidth  = (NSUInteger)(fb->width  * ctx->scale);
    NSUInteger scaledHeight = (NSUInteger)(fb->height * ctx->scale);

    MTLOrigin origin = {0, 0, 0};
    MTLSize size = {
        MIN(scaledWidth,  dst.width),
        MIN(scaledHeight, dst.height),
        1
    };

    [blit copyFromTexture:src
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:origin
               sourceSize:size
                toTexture:dst
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:origin];

    [blit endEncoding];

    // 5. Present the drawable and submit work
    [cmd presentDrawable:drawable];
    [cmd commit];
    [cmd waitUntilCompleted];

    NSLog(@"[Metal] Presented framebuffer (%zux%zu @ scale %.2f)",
          fb->width, fb->height, ctx->scale);
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
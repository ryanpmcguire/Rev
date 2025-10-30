// MetalBackend.hpp
#pragma once

#include <vector>

struct MetalContext;
struct MetalFramebuffer;
struct MetalTexture;
struct MetalShader;

MetalContext* metal_context_create(void* nsView);
void metal_context_destroy(MetalContext*);

void metal_context_resize(MetalContext* c, int w, int h);
float metal_context_get_scale(MetalContext* c);

void metal_begin_frame(MetalContext* c);
void metal_end_frame(MetalContext* c);
void metal_present(MetalContext*c, void* framebuffer);

// Texture
void* metal_create_texture(MetalContext* ctx, const unsigned char* data, size_t width, size_t height, size_t channels);
void metal_destroy_texture(void* texture);
void metal_bind_texture(MetalContext* ctx, void* texture, int unit);
void metal_unbind_texture(MetalContext* ctx, int unit);

// Framebuffer
void* metal_create_framebuffer(MetalContext* ctx, size_t width, size_t height, size_t colorChannels);
void metal_destroy_framebuffer(void* framebuffer);
void metal_resize_framebuffer(MetalContext* ctx, void* framebuffer, size_t width, size_t height);
void metal_framebuffer_begin_frame(MetalContext* ctx, void* framebuffer);
void metal_framebuffer_end_frame(MetalContext* ctx, void* framebuffer);
void metal_framebuffer_blit_to_drawable(MetalContext* ctx, void* framebuffer);

// Uniform Buffer
void* metal_create_uniform_buffer(MetalContext* ctx, size_t size);
void  metal_destroy_uniform_buffer(void* buffer);
void* metal_map_uniform_buffer(void* buffer); // returns CPU-visible memory
void  metal_bind_uniform_buffer(MetalContext* ctx, void* buffer, int index);

// Vertex Buffer
void* metal_create_vertex_buffer(MetalContext* ctx, size_t size);
void  metal_destroy_vertex_buffer(void* buffer);
void* metal_map_vertex_buffer(void* buffer); // returns CPU-visible memory
void  metal_bind_vertex_buffer(MetalContext* ctx, void* buffer, int index);

// Shader
enum MetalShaderStage { MetalShaderStageVertex, MetalShaderStageFragment };
void* metal_create_shader(MetalContext* ctx, const char* source, size_t length);
void  metal_destroy_shader(void* shader);

// Pipeline
void* metal_create_pipeline(MetalContext* ctx, MetalShader* shader, std::vector<float> attribs, bool instanced);
void  metal_destroy_pipeline(void* pipeline);
void  metal_bind_pipeline(MetalContext* ctx, void* pipeline);

// Functions
void metal_draw_arrays(MetalContext* ctx, int topology, size_t start, size_t verticesPer);
void metal_draw_arrays_instanced(MetalContext* ctx, int topology, size_t start, size_t verticesPer, size_t numInstances);
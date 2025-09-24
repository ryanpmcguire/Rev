// MetalBackend.hpp
#pragma once

struct MetalContext;

MetalContext* metal_context_create(void* nsView);
void metal_context_destroy(MetalContext*);

void metal_context_resize(MetalContext*, int w, int h);

void metal_begin_frame(MetalContext* c);
void metal_end_frame(MetalContext* c);

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

// Pipeline
void* metal_create_pipeline(MetalContext* ctx, void* vertShader, void* fragShader);
void  metal_destroy_pipeline(void* pipeline);
void  metal_bind_pipeline(MetalContext* ctx, void* pipeline);

// Shader
enum MetalShaderStage { MetalShaderStageVertex, MetalShaderStageFragment };
void* metal_create_shader(MetalContext* ctx, const char* source, size_t length, MetalShaderStage stage);
void  metal_destroy_shader(void* shader);
// MetalBackend.hpp
#pragma once

struct MetalBackend;

MetalBackend* metal_backend_create(void* nsView);
void metal_backend_destroy(MetalBackend*);

void metal_backend_resize(MetalBackend*, int w, int h);
void metal_backend_clear(MetalBackend*, float r, float g, float b, float a);
void metal_backend_present(MetalBackend*);

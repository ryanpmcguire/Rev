export module Rev.Graphics.RenderCommandEncoder;

#if defined(PLATFORM_OPENGL)
    export import Rev.OpenGL.RenderCommandEncoder;
#elif defined(PLATFORM_METAL)
    export import Rev.Metal.RenderCommandEncoder;
#elif defined(PLATFORM_DIRECTX)
    export import Rev.DirectX.RenderCommandEncoder;
#elif defined(PLATFORM_VULKAN)
    export import Rev.Vulkan.RenderCommandEncoder;
#else
    #error "No valid graphics backend defined!"
#endif
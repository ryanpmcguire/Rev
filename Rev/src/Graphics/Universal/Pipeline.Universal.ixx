export module Rev.Graphics.Pipeline;

#if defined(PLATFORM_OPENGL)
    export import Rev.OpenGL.Pipeline;
#elif defined(PLATFORM_METAL)
    export import Rev.Metal.Pipeline;
#elif defined(PLATFORM_DIRECTX)
    export import Rev.DirectX.Pipeline;
#elif defined(PLATFORM_VULKAN)
    export import Rev.Vulkan.Pipeline;
#else
    #error "No valid graphics backend defined!"
#endif
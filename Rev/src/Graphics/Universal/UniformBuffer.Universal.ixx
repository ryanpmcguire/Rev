export module Rev.Graphics.UniformBuffer;

#if defined(PLATFORM_OPENGL)
    export import Rev.OpenGL.UniformBuffer;
#elif defined(PLATFORM_METAL)
    export import Rev.Metal.UniformBuffer;
#elif defined(PLATFORM_DIRECTX)
    export import Rev.DirectX.UniformBuffer;
#elif defined(PLATFORM_VULKAN)
    export import Rev.Vulkan.UniformBuffer;
#else
    #error "No valid graphics backend defined!"
#endif
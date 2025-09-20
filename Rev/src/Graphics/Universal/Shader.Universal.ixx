export module Rev.Graphics.Shader;

#if defined(PLATFORM_OPENGL)
    export import Rev.OpenGL.Shader;
#elif defined(PLATFORM_METAL)
    export import Rev.Metal.Shader;
#elif defined(PLATFORM_DIRECTX)
    export import Rev.DirectX.Shader;
#elif defined(PLATFORM_VULKAN)
    export import Rev.Vulkan.Shader;
#else
    #error "No valid graphics backend defined!"
#endif
export module Rev.Graphics.Texture;

#if defined(PLATFORM_OPENGL)
    export import Rev.OpenGL.Texture;
#elif defined(PLATFORM_METAL)
    export import Rev.Metal.Texture;
#elif defined(PLATFORM_DIRECTX)
    export import Rev.DirectX.Texture;
#elif defined(PLATFORM_VULKAN)
    export import Rev.Vulkan.Texture;
#else
    #error "No valid graphics backend defined!"
#endif
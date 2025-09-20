export module Rev.Graphics.Canvas;

#if defined(PLATFORM_OPENGL)
    export import Rev.OpenGL.Canvas;
#elif defined(PLATFORM_METAL)
    export import Rev.Metal.Canvas;
#elif defined(PLATFORM_DIRECTX)
    export import Rev.DirectX.Canvas;
#elif defined(PLATFORM_VULKAN)
    export import Rev.Vulkan.Canvas;
#else
    #error "No valid graphics backend defined!"
#endif
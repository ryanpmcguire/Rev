export module Rev.Graphics.VertexBuffer;

#if defined(PLATFORM_OPENGL)
    export import Rev.OpenGL.VertexBuffer;
#elif defined(PLATFORM_METAL)
    export import Rev.Metal.VertexBuffer;
#elif defined(PLATFORM_DIRECTX)
    export import Rev.DirectX.VertexBuffer;
#elif defined(PLATFORM_VULKAN)
    export import Rev.Vulkan.VertexBuffer;
#else
    #error "No valid graphics backend defined!"
#endif
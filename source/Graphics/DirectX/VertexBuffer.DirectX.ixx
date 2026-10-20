module;
#include "Helpers/Buffers.hpp"
export module Rev.Graphics.VertexBuffer;
import Rev.Core.Vertex;
import Rev.Core.Vertex3;
export namespace Rev::Graphics {
    struct VertexBuffer : DirectX::VertexBuffer {
        using DirectX::VertexBuffer::VertexBuffer;
        Core::Vertex* verts() { return static_cast<Core::Vertex*>(data); }
        Core::Vertex3* verts3() { return static_cast<Core::Vertex3*>(data); }
    };
}

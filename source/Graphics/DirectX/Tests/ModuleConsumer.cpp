#include <vector>
import Rev.Core.Resource;
import Rev.Core.Vertex;
import Rev.Core.Vertex3;
import Rev.Graphics.Canvas;
import Rev.Graphics.CommandBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.TextureBuffer;
import Rev.Graphics.CameraTexture;

// Compile against the exported API, rather than only the implementation headers.
void useResources(Rev::Graphics::Canvas& canvas, Rev::Core::Resource vs, Rev::Core::Resource ps) {
    using namespace Rev::Graphics;
    Pipeline pipeline(canvas.context, {.attribs = Rev::Core::Vertex::attribs, .directXVert = vs, .directXFrag = ps});
    UniformBuffer uniform(canvas.context, 16);
    VertexBuffer vertices(canvas.context, {.num = 3, .attribs = Rev::Core::Vertex::attribs});
    vertices.verts()[0].x = 1;
    vertices.markChanged(); vertices.upload();
    vertices.set(std::vector<Rev::Core::Vertex>(3));
    TextureBuffer points(canvas.context); points.resize(4);
    points.floats()[0] = 1; points.markChanged();
    CameraTexture camera(canvas.context);
    CommandBuffer commands;
    canvas.beginFrame(); pipeline.bind(); uniform.bind(1); vertices.bind();
    canvas.drawArrays(Pipeline::TriangleList, 0, 3); canvas.endFrame();
}

int main() { return 0; } // Linking this TU validates all referenced exported methods.

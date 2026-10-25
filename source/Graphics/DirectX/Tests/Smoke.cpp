#include "../Helpers/Canvas.hpp"
#include <d3d12sdklayers.h>
#include <iostream>

using namespace Rev::Graphics::DirectX;

static ResourceView source(const char* text) {
    return {reinterpret_cast<const unsigned char*>(text), std::strlen(text)};
}
static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static const char* vertex = R"(
struct Output { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Output main(uint id : SV_VertexID) {
    float2 corners[4] = {float2(-1,1),float2(1,1),float2(1,-1),float2(-1,-1)};
    Output o; o.position=float4(corners[id % 4],0.5,1); o.uv=corners[id % 4]*float2(0.5,-0.5)+0.5; return o;
})";
static const char* fragment = R"(
cbuffer Color : register(b1) { float4 color; };
float4 main() : SV_Target { return color; }
)";

struct Readback {
    ComPtr<ID3D12Resource> buffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    void record(Canvas& canvas) {
        auto& ctx = *canvas.context; auto* texture = canvas.frameBuffer->texture;
        auto desc = texture->id->GetDesc(); UINT rows; UINT64 rowBytes, total;
        ctx.device->gpu->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
        buffer = ctx.device->create(bufferDesc(total), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        ctx.transition(texture->id.Get(), texture->state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource=buffer.Get();
        destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint=footprint;
        D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource=texture->id.Get(); source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        ctx.list->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
        ctx.transition(texture->id.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,texture->state);
        ctx.retain(buffer);
    }
    void pixel(Canvas& canvas, UINT x, UINT y, std::array<unsigned char,4> expected) {
        canvas.context->waitIdle();
        unsigned char* bytes=nullptr;
        check(buffer->Map(0,nullptr,reinterpret_cast<void**>(&bytes)),"Map readback");
        auto* p=bytes+footprint.Offset+y*footprint.Footprint.RowPitch+x*4;
        std::array<unsigned char,4> actual{p[0],p[1],p[2],p[3]};
        D3D12_RANGE empty{0,0}; buffer->Unmap(0,&empty);
        if (actual != expected) {
            std::cerr << "Pixel: " << int(actual[0]) << ',' << int(actual[1]) << ',' << int(actual[2]) << ',' << int(actual[3]) << '\n';
            throw std::runtime_error("Unexpected rendered pixel");
        }
    }
};

int main(int argc, char** argv) try {
    bool debug = !(argc > 1 && std::string(argv[1]) == "--no-debug");
    auto device=std::make_shared<Device>(true,debug);
    ComPtr<ID3D12InfoQueue> messages;
    if (debug) check(device->gpu.As(&messages),"Get debug messages");
    {
        Canvas canvas(nullptr,device);
        Pipeline pipeline(canvas.context,{.directXVert=source(vertex),.directXFrag=source(fragment)});
        UniformBuffer color(canvas.context,16);
        float red[4]={1,0,0,1}, green[4]={0,1,0,1};
        color.set(red); color.upload(); auto original=color.buffer;
        color.upload(); require(original.Get()==color.buffer.Get(),"Unchanged buffer uploaded again");
        void* stable=color.data;
        Readback read;
        canvas.beginFrame(32,32); pipeline.bind(); color.bind(1);
        canvas.drawArrays(Pipeline::TriangleFan,0,4);
        read.record(canvas); canvas.endFrame(); read.pixel(canvas,16,16,{255,0,0,255});

        // Mutate through the stable pointer, then explicitly commit the revision.
        std::memcpy(color.data,green,16); color.markChanged(); color.upload();
        require(color.data==stable,"CPU pointer moved without resize");
        require(color.buffer.Get()!=original.Get(),"Changed buffer reused an in-flight allocation");
        canvas.beginFrame(48,24); pipeline.bind(); color.bind(1);
        canvas.drawArraysInstanced(Pipeline::TriangleFan,0,4,1);
        read.record(canvas); canvas.endFrame(); read.pixel(canvas,24,12,{0,255,0,255});

        // Two revisions in ONE submission must not change the earlier draw's data.
        canvas.beginFrame(32,32); pipeline.bind();
        D3D12_RECT left{0,0,16,32}, right{16,0,32,32};
        canvas.context->list->RSSetScissorRects(1,&left);
        color.set(red); color.bind(1); canvas.drawArrays(Pipeline::TriangleFan,0,4);
        canvas.context->list->RSSetScissorRects(1,&right);
        color.set(green); color.bind(1); canvas.drawArrays(Pipeline::TriangleFan,0,4);
        read.record(canvas); canvas.endFrame();
        read.pixel(canvas,8,16,{255,0,0,255}); read.pixel(canvas,24,16,{0,255,0,255});

        // Stencil reference 1 fails against clear value 0, so the target stays white.
        canvas.beginFrame(32,32); pipeline.bind(); color.bind(1); canvas.stencilDepth(1);
        canvas.drawArrays(Pipeline::TriangleFan,0,4);
        read.record(canvas); canvas.endFrame(); read.pixel(canvas,16,16,{255,255,255,255});

        // Replace stencil with one, without color writes, then draw through that mask.
        canvas.beginFrame(32,32); pipeline.bind(); color.bind(1);
        canvas.colorWrite(false); canvas.stencilSet(0); canvas.stencilWrite(true);
        canvas.stencilPush(0); canvas.drawArrays(Pipeline::TriangleFan,0,4);
        canvas.colorWrite(true); canvas.stencilWrite(false); canvas.stencilDepth(1);
        canvas.depthTest(true); canvas.depthWrite(true); canvas.depthBias(0,0);
        canvas.drawArrays(Pipeline::TriangleFan,0,4);
        read.record(canvas); canvas.endFrame(); read.pixel(canvas,16,16,{0,255,0,255});

        // Texture upload, R8 swizzle and sampling. Destroy the wrapper before submission.
        Pipeline textured(canvas.context,{.directXVert=source(vertex),.directXFrag=source(R"(
Texture2D image : register(t0); SamplerState sampling : register(s0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return image.Sample(sampling,uv); }
)")});
        unsigned char white=255;
        auto texture=std::make_unique<Texture>(canvas.context,Texture::Params{&white,1,1,1});
        canvas.beginFrame(32,32); textured.bind(); texture->bind(); canvas.drawArrays(Pipeline::TriangleFan,0,4);
        texture.reset(); read.record(canvas); canvas.endFrame(); read.pixel(canvas,16,16,{255,255,255,255});

        // Typed float2 SRV used by the fast-line path.
        Pipeline buffered(canvas.context,{.directXVert=source(vertex),.directXFrag=source(R"(
Buffer<float2> points : register(t0);
float4 main() : SV_Target { return float4(points.Load(0),0,1); }
)")});
        TextureBuffer points(canvas.context); float xy[2]={1,0}; points.set(xy,1);
        canvas.beginFrame(32,32); buffered.bind(); points.bind(); canvas.drawArrays(Pipeline::TriangleFan,0,4);
        read.record(canvas); canvas.endFrame(); read.pixel(canvas,16,16,{255,0,0,255});

        // Vertex input layout and per-instance input rates.
        Pipeline attributed(canvas.context,{.attribs={2},.directXVert=source(R"(
float4 main(float2 position : ATTR0) : SV_Position { return float4(position,0.5,1); }
)"),.directXFrag=source(fragment)});
        VertexBuffer vertices(canvas.context,{.num=3,.attribs={2}});
        float positions[6]={-1,-1,-1,3,3,-1}; std::memcpy(vertices.data,positions,sizeof(positions)); vertices.markChanged();
        canvas.beginFrame(32,32); attributed.bind(); vertices.bind(); color.bind(1);
        canvas.drawArrays(Pipeline::TriangleList,0,3);
        read.record(canvas); canvas.endFrame(); read.pixel(canvas,16,16,{0,255,0,255});

        // Shared resources bind against the active canvas, not the construction canvas.
        Canvas second(nullptr,device);
        second.beginFrame(16,16); pipeline.bind(); color.bind(1); second.drawArrays(Pipeline::TriangleFan,0,4);
        read.record(second); second.endFrame(); read.pixel(second,8,8,{0,255,0,255});

        unsigned char rgb[3]={255,0,0};
        Texture sharedTexture(canvas.context,{rgb,1,1,3});
        canvas.beginFrame(32,32); textured.bind(); sharedTexture.bind();
        canvas.drawArrays(Pipeline::TriangleFan,0,4); canvas.endFrame();
        second.beginFrame(16,16); textured.bind(); sharedTexture.bind();
        second.drawArrays(Pipeline::TriangleFan,0,4);
        read.record(second); second.endFrame(); read.pixel(second,8,8,{255,0,0,255});

        // Exercise allocator reuse without waiting on every frame and resource destruction.
        for (int i=0;i<8;++i) {
            canvas.beginFrame(32,32); pipeline.bind();
            { UniformBuffer temporary(canvas.context,16); temporary.set(red); temporary.bind(1); canvas.drawArrays(Pipeline::TriangleFan,0,4); }
            canvas.endFrame();
        }
        canvas.context->waitIdle();

        // Hidden HWND exercises actual DXGI swap-chain creation, present and resize.
        HWND window=CreateWindowExW(0,L"STATIC",L"Rev DirectX smoke",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window!=nullptr,"Create smoke window failed");
        {
            Canvas presented(window,device);
            for (int i=0;i<3;++i) {
                presented.beginFrame(32+i*8,32+i*8); pipeline.bind(); color.bind(1);
                presented.drawArrays(Pipeline::TriangleFan,0,4); presented.endFrame();
            }
            presented.context->waitIdle();
        }
        DestroyWindow(window);
    }
    if (messages) {
        for (UINT64 i=0;i<messages->GetNumStoredMessages();++i) {
            SIZE_T size=0; messages->GetMessage(i,nullptr,&size); std::vector<unsigned char> bytes(size);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data()); check(messages->GetMessage(i,message,&size),"Read debug message");
            if (message->Severity<=D3D12_MESSAGE_SEVERITY_WARNING) {
                std::cerr << message->pDescription << '\n';
                throw std::runtime_error("D3D12 debug layer reported a warning or error");
            }
        }
    }
    std::cout << "DirectX smoke passed: revision snapshots, pixels, stencil, depth, texture, buffer SRV, vertex input, multiple canvases, lifetime, resize/present" << std::endl;
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << std::endl; return 1;
}

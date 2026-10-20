#pragma once
#include "Context.hpp"
#include <map>
#include <tuple>

namespace Rev::Graphics::DirectX {
struct ResourceView {
    const unsigned char* data = nullptr;
    size_t size = 0;
    ResourceView() = default;
    ResourceView(const unsigned char* data, size_t size) : data(data), size(size) {}
    template<class Resource> ResourceView(const Resource& source) : data(source.data), size(source.size) {}
};
struct Shader {
    enum Stage { Vertex, Fragment };
    ComPtr<ID3DBlob> shader;
    Shader(ResourceView source, Stage stage, std::string definitions = "", const char* entry = "main") {
        if (!source.data || !source.size) throw std::invalid_argument("[DirectX] Missing HLSL shader resource; GLSL is not accepted");
        std::string text(reinterpret_cast<const char*>(source.data), source.size);
        auto position = text.find("DEFINITIONS");
        if (position != std::string::npos) text.replace(position, 11, definitions);
        else text = definitions + "\n" + text;
        ComPtr<ID3DBlob> errors;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR;
#ifndef NDEBUG
        // Retain symbols, but allow basic optimization. FXC's fully unoptimized
        // procedural fast-line shader causes device removal on Windows WARP.
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_OPTIMIZATION_LEVEL1;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
        auto result = D3DCompile(text.data(), text.size(), "Rev HLSL", nullptr, nullptr,
            entry, stage == Vertex ? "vs_5_1" : "ps_5_1", flags, 0, &shader, &errors);
        if (FAILED(result) && errors) throw std::runtime_error(std::string("[DirectX] HLSL: ") +
            std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
        check(result, "Compile HLSL shader");
    }
};

struct Pipeline {
    enum Topology { TriangleFan, TriangleList, LineList };
    struct Params {
        bool instanced = true;
        std::vector<size_t> attribs;
        std::string definitions;
        ResourceView openGlVert, openGlFrag, metalUniversal, vulkanVert, vulkanFrag;
        ResourceView directXVert, directXFrag;
        std::string vertexEntry = "main", fragmentEntry = "main";
    };
    void* context;
    std::shared_ptr<Device> device;
    Params params;
    Shader vert, frag;
    using Key = std::tuple<bool, bool, bool, bool, int, float, INT, int, UINT>;
    std::map<Key, ComPtr<ID3D12PipelineState>> states;
    Pipeline(void* context, Params params) : context(context), device(deviceFrom(context)), params(std::move(params)),
        vert(this->params.directXVert, Shader::Vertex, this->params.definitions, this->params.vertexEntry.c_str()),
        frag(this->params.directXFrag, Shader::Fragment, this->params.definitions, this->params.fragmentEntry.c_str()) {}
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    void bind() { Context::active(device).pipeline = this; }
    void apply(Context& ctx, Topology topology) {
        const auto& s = ctx.state;
        if (ctx.vertexAttribs != params.attribs && !params.attribs.empty())
            throw std::logic_error("[DirectX] Bound vertex layout differs from pipeline");
        Key key{s.color, s.stencilWrite, s.depth, s.depthWrite, int(s.stencilOp),
            s.biasFactor, s.biasUnits, int(topology), ctx.vertexDivisor};
        auto it = states.find(key);
        if (it == states.end()) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = device->root.Get();
            desc.VS = {vert.shader->GetBufferPointer(), vert.shader->GetBufferSize()};
            desc.PS = {frag.shader->GetBufferPointer(), frag.shader->GetBufferSize()};
            desc.SampleMask = UINT_MAX; desc.SampleDesc.Count = 1;
            desc.NumRenderTargets = 1; desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
            auto& blend = desc.BlendState.RenderTarget[0];
            blend.BlendEnable = TRUE;
            blend.SrcBlend = D3D12_BLEND_SRC_ALPHA; blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOp = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA; blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOpAlpha = D3D12_BLEND_OP_ADD; blend.LogicOp = D3D12_LOGIC_OP_NOOP;
            blend.RenderTargetWriteMask = s.color ? D3D12_COLOR_WRITE_ENABLE_ALL : 0;
            desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            desc.RasterizerState.DepthClipEnable = TRUE;
            desc.RasterizerState.DepthBias = s.biasUnits;
            desc.RasterizerState.SlopeScaledDepthBias = s.biasFactor;
            auto& depth = desc.DepthStencilState;
            depth.DepthEnable = s.depth; depth.DepthWriteMask = s.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            depth.StencilEnable = TRUE; depth.StencilReadMask = 255; depth.StencilWriteMask = s.stencilWrite ? 255 : 0;
            depth.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, s.stencilOp, D3D12_COMPARISON_FUNC_LESS_EQUAL};
            depth.BackFace = depth.FrontFace;
            desc.PrimitiveTopologyType = topology == LineList ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            std::vector<D3D12_INPUT_ELEMENT_DESC> inputs;
            const DXGI_FORMAT formats[] = {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT,
                DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT};
            UINT offset = 0;
            for (size_t i = 0; i < params.attribs.size(); ++i) {
                auto count = params.attribs[i];
                if (!count || count > 4) throw std::invalid_argument("[DirectX] Invalid pipeline attribute");
                inputs.push_back({"ATTR", checkedUInt(i), formats[count], 0, offset,
                    ctx.vertexDivisor ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                    ctx.vertexDivisor});
                offset += checkedUInt(count * sizeof(float));
            }
            desc.InputLayout = {inputs.data(), checkedUInt(inputs.size())};
            ComPtr<ID3D12PipelineState> state;
            check(device->gpu->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&state)), "Create graphics pipeline");
            it = states.emplace(key, std::move(state)).first;
        }
        ctx.list->SetPipelineState(it->second.Get()); ctx.retain(it->second);
        ctx.list->OMSetStencilRef(s.stencilRef);
        ctx.list->IASetPrimitiveTopology(topology == LineList ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
};
}

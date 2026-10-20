#pragma once
#include "Buffers.hpp"
#include "Pipeline.hpp"
#include "Texture.hpp"
#include <cmath>

namespace Rev::Graphics::DirectX {
struct Canvas {
    struct Flags { bool resize = true, record = true, stencil = false, color = true; } flags;
    struct Details { size_t width = 0, height = 0; float scale = 1; } details;
    std::unique_ptr<Context> ownedContext;
    Context* context;
    std::unique_ptr<UniformBuffer> ownedTransform;
    UniformBuffer* transform;
    std::unique_ptr<FrameBuffer> ownedFrameBuffer;
    FrameBuffer* frameBuffer;
    explicit Canvas(HWND window = nullptr, std::shared_ptr<Device> device = Device::shared())
        : ownedContext(std::make_unique<Context>(window, std::move(device))), context(ownedContext.get()),
          ownedTransform(std::make_unique<UniformBuffer>(context, sizeof(float) * 16)), transform(ownedTransform.get()),
          ownedFrameBuffer(std::make_unique<FrameBuffer>(context, FrameBuffer::Params{1, 1, 4})), frameBuffer(ownedFrameBuffer.get()) {}
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    bool isCurrent() const { return context->owner == std::this_thread::get_id(); }
    void requireCurrent(const char*) const { context->requireOwner(); }
    void beginFrame(size_t width, size_t height, float scale = 1) {
        width = std::max<size_t>(width, 1); height = std::max<size_t>(height, 1);
        if (flags.resize || details.width != width || details.height != height || details.scale != scale) {
            context->resize(checkedUInt(width), checkedUInt(height)); frameBuffer->resize(width, height);
            details = {width, height, scale};
            // Preserve Rev's OpenGL projection exactly. HLSL vertex ports convert
            // clip Z at their output boundary, including shaders using 3D matrices.
            const float projection[16] = {2.0f / (float(width) / scale),0,0,0, 0,-2.0f / (float(height) / scale),0,0,
                0,0,-1,0, -1,1,0,1};
            transform->set(projection); flags.resize = false;
        }
        context->begin(); frameBuffer->bind(); flags.color = true; flags.stencil = false;
        const float clear[4] = {1,1,1,1};
        context->list->ClearRenderTargetView(context->rtv, clear, 0, nullptr);
        context->list->ClearDepthStencilView(context->dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1, 0, 0, nullptr);
        transform->bind(0);
    }
    void endFrame() {
        context->requireRecording();
        if (context->swapchain) {
            auto& back = context->backbuffers[context->frameIndex];
            context->transition(frameBuffer->texture->id.Get(), frameBuffer->texture->state, D3D12_RESOURCE_STATE_COPY_SOURCE);
            frameBuffer->texture->state = D3D12_RESOURCE_STATE_COPY_SOURCE;
            context->transition(back.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
            context->list->CopyResource(back.Get(), frameBuffer->texture->id.Get());
            context->transition(back.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
            context->retain(back);
        }
        frameBuffer->finish(); context->submit();
    }
    void colorWrite(bool enable) { flags.color = enable; context->state.color = enable; }
    void stencilWrite(bool enable) { flags.stencil = enable; context->state.stencilWrite = enable; }
    void stencilDepth(size_t depth) {
        if (depth > 255) throw std::out_of_range("[DirectX] Stencil depth exceeds 8 bits");
        context->state.stencilRef = static_cast<UINT>(depth);
    }
    void stencilFill(size_t depth = 0) {
        if (depth > 255) throw std::out_of_range("[DirectX] Stencil depth exceeds 8 bits");
        context->requireRecording();
        context->list->ClearDepthStencilView(context->dsv, D3D12_CLEAR_FLAG_STENCIL, 1, static_cast<UINT8>(depth), 0, nullptr);
    }
    void stencilClear() { stencilFill(0); }
    void stencilReset(size_t depth) { stencilFill(depth); stencilDepth(depth); stencilWrite(false); }
    void stencilPush(size_t depth) { stencilDepth(depth); context->state.stencilOp = D3D12_STENCIL_OP_INCR_SAT; }
    void stencilPop(size_t depth) { stencilDepth(depth); context->state.stencilOp = D3D12_STENCIL_OP_DECR_SAT; }
    void stencilSet(size_t depth) { stencilDepth(depth); context->state.stencilOp = D3D12_STENCIL_OP_REPLACE; }
    void depthTest(bool enable) { context->state.depth = enable; }
    void depthWrite(bool enable) { context->state.depthWrite = enable; }
    void depthBias(float factor, float units) {
        if (!std::isfinite(factor) || !std::isfinite(units) || double(units) < INT_MIN || double(units) > INT_MAX)
            throw std::invalid_argument("[DirectX] Invalid depth bias");
        context->state.biasFactor = factor; context->state.biasUnits = static_cast<INT>(units);
    }
    void clearDepth() {
        context->requireRecording();
        context->list->ClearDepthStencilView(context->dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
    }
    void drawArrays(Pipeline::Topology topology, size_t start, size_t count) { drawArraysInstanced(topology, start, count, 1); }
    void drawArraysInstanced(Pipeline::Topology topology, size_t start, size_t count, size_t instances) {
        context->requireRecording();
        if (!count || !instances) return;
        if (!context->pipeline) throw std::logic_error("[DirectX] Draw requires a pipeline");
        if (topology != Pipeline::TriangleList && topology != Pipeline::TriangleFan && topology != Pipeline::LineList)
            throw std::invalid_argument("[DirectX] Unknown topology");
        checkedUInt(start); checkedUInt(count); checkedUInt(instances);
        context->pipeline->apply(*context, topology);
        if (topology == Pipeline::TriangleFan) {
            if (count < 3) return;
            if (start > INT_MAX) throw std::out_of_range("[DirectX] Fan base vertex");
            // Indexed conversion preserves original vertex IDs, including procedural quads.
            // Reuse index snapshots by count; they are retained independently by each frame.
            auto it = fans.find(count);
            if (it == fans.end()) {
                size_t indices = product(count - 2, 3); checkedUInt(product(indices, sizeof(uint32_t)));
                auto buffer = std::make_unique<Buffer>(context); buffer->resizeBytes(indices * sizeof(uint32_t));
                auto* values = static_cast<uint32_t*>(buffer->data);
                for (size_t i = 0; i < count - 2; ++i) { values[i*3] = 0; values[i*3+1] = checkedUInt(i+1); values[i*3+2] = checkedUInt(i+2); }
                buffer->upload(); it = fans.emplace(count, std::move(buffer)).first;
            }
            auto& buffer = *it->second; context->retain(buffer.buffer);
            D3D12_INDEX_BUFFER_VIEW view{buffer.buffer->GetGPUVirtualAddress(), checkedUInt(buffer.size), DXGI_FORMAT_R32_UINT};
            context->list->IASetIndexBuffer(&view);
            context->list->DrawIndexedInstanced(checkedUInt((count - 2) * 3), checkedUInt(instances), 0, static_cast<INT>(start), 0);
        } else context->list->DrawInstanced(checkedUInt(count), checkedUInt(instances), checkedUInt(start), 0);
    }
    std::map<size_t, std::unique_ptr<Buffer>> fans;
};
}

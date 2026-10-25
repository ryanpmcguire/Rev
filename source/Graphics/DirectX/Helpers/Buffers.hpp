#pragma once
#include "Context.hpp"
#include <numeric>

namespace Rev::Graphics::DirectX {
// Stable, CPU-readable/writable data. Call markChanged() after direct writes in compute().
// Each revision has immutable GPU storage, so earlier frames can keep reading it.
struct Buffer {
    void* context = nullptr;
    std::shared_ptr<Device> device;
    std::vector<std::max_align_t> storage;
    void* data = nullptr;
    size_t size = 0;
    uint64_t revision = 1, uploadedRevision = 0;
    ComPtr<ID3D12Resource> buffer;
    explicit Buffer(void* context) : context(context), device(deviceFrom(context)) {}
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    void markChanged() { ++revision; }
    void resizeBytes(size_t bytes) {
        if (size == bytes) return;
        if (bytes > SIZE_MAX - sizeof(std::max_align_t)) throw std::overflow_error("[DirectX] Buffer too large");
        storage.resize((bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
        size = bytes; data = bytes ? storage.data() : nullptr; markChanged();
    }
    void set(const void* value) {
        if (size && !value) throw std::invalid_argument("[DirectX] Null buffer source");
        if (size) std::memcpy(data, value, size);
        markChanged();
    }
    // Can be called explicitly after compute, outside a frame. bind() is also a
    // revision-gated fallback, never an unconditional upload on every draw.
    void upload() {
        if (uploadedRevision == revision) return;
        if (!size) { buffer.Reset(); uploadedRevision = revision; return; }
        if (size > SIZE_MAX - 255) throw std::overflow_error("[DirectX] Buffer too large");
        size_t aligned = (size + 255) & ~size_t(255);
        auto next = device->create(bufferDesc(aligned), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void* mapped = nullptr; D3D12_RANGE empty{0, 0};
        check(next->Map(0, &empty, &mapped), "Map buffer snapshot");
        std::memcpy(mapped, data, size);
        std::memset(static_cast<unsigned char*>(mapped) + size, 0, aligned - size);
        next->Unmap(0, nullptr);
        buffer = std::move(next); uploadedRevision = revision;
    }
};

struct UniformBuffer : Buffer {
    UniformBuffer(void* context, size_t bytes) : Buffer(context) {
        if (!bytes || bytes > 65536) throw std::invalid_argument("[DirectX] Constant buffer must be 1..65536 bytes");
        resizeBytes(bytes);
    }
    void bind(size_t position) {
        if (position >= Device::uniformSlots) throw std::out_of_range("[DirectX] Constant buffer slot");
        auto& ctx = Context::active(device); upload(); ctx.retain(buffer);
        ctx.list->SetGraphicsRootConstantBufferView(static_cast<UINT>(position), buffer->GetGPUVirtualAddress());
    }
    void unbind() {} // Bindings persist until replaced, as with the other Rev backends.
};

struct VertexBuffer : Buffer {
    struct Params { size_t num = 0, divisor = 0; std::vector<size_t> attribs; };
    Params params;
    size_t stride = 0;
    VertexBuffer(void* context, Params options) : Buffer(context), params(std::move(options)) {
        for (auto attrib : params.attribs) {
            if (attrib < 1 || attrib > 4) throw std::invalid_argument("[DirectX] Vertex attributes must contain 1..4 floats");
            stride += attrib * sizeof(float);
        }
        checkedUInt(params.divisor); checkedUInt(stride);
        auto count = params.num; params.num = 0; resize(count);
    }
    void resize(size_t count) {
        auto bytes = product(count, stride); checkedUInt(bytes);
        resizeBytes(bytes); params.num = count;
    }
    template<class T> void set(const std::vector<T>& values) {
        if (sizeof(T) != stride) throw std::invalid_argument("[DirectX] Vertex stride mismatch");
        resize(values.size()); Buffer::set(values.data());
    }
    template<class T> void set3(const std::vector<T>& values) { set(values); }
    void bind() {
        auto& ctx = Context::active(device); upload(); ctx.retain(buffer);
        D3D12_VERTEX_BUFFER_VIEW view{};
        if (buffer) view = {buffer->GetGPUVirtualAddress(), checkedUInt(size), checkedUInt(stride)};
        ctx.list->IASetVertexBuffers(0, 1, &view);
        ctx.vertexAttribs = params.attribs; ctx.vertexDivisor = checkedUInt(params.divisor);
    }
    void unbind() {
        auto& ctx = Context::active(device); ctx.list->IASetVertexBuffers(0, 0, nullptr);
        ctx.vertexAttribs.clear(); ctx.vertexDivisor = 0;
    }
};

struct TextureBuffer : Buffer {
    size_t count = 0;
    explicit TextureBuffer(void* context) : Buffer(context) {}
    void resize(size_t elements) { checkedUInt(elements); resizeBytes(product(elements, sizeof(float) * 2)); count = elements; }
    void set(const float* xy, size_t elements) { resize(elements); Buffer::set(xy); }
    float* floats() { return static_cast<float*>(data); }
    void bind(size_t unit = 0) {
        if (unit >= Device::textureSlots) throw std::out_of_range("[DirectX] Texture buffer slot");
        auto& ctx = Context::active(device); upload(); ctx.retain(buffer);
        auto descriptor = ctx.descriptor();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R32G32_FLOAT; srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Buffer.NumElements = std::max(checkedUInt(count), 1u);
        device->gpu->CreateShaderResourceView(buffer.Get(), &srv, descriptor.cpu);
        ctx.list->SetGraphicsRootDescriptorTable(Device::uniformSlots + static_cast<UINT>(unit), descriptor.gpu);
    }
    void unbind(size_t unit = 0) {
        if (unit >= Device::textureSlots) throw std::out_of_range("[DirectX] Texture buffer slot");
        auto& ctx = Context::active(device); auto descriptor = ctx.descriptor();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R32G32_FLOAT; srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Buffer.NumElements = 1;
        device->gpu->CreateShaderResourceView(nullptr, &srv, descriptor.cpu);
        ctx.list->SetGraphicsRootDescriptorTable(Device::uniformSlots + static_cast<UINT>(unit), descriptor.gpu);
    }
};
}

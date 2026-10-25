#pragma once
#include "Context.hpp"

namespace Rev::Graphics::DirectX {
struct Texture {
    enum class Filter { Nearest, Bilinear, NearestMipmapNearest, BilinearMipmapNearest, NearestMipmapBilinear, Trilinear };
    struct Params {
        unsigned char* data = nullptr;
        size_t width = 0, height = 0, channels = 4;
        Filter filter = Filter::Nearest;
    };
    void* context = nullptr;
    std::shared_ptr<Device> device;
    unsigned char* data = nullptr;
    size_t width = 0, height = 0, channels = 4, size = 0;
    Filter filter = Filter::Nearest;
    std::vector<unsigned char> pixels;
    uint64_t revision = 1, uploadedRevision = 0;
    ComPtr<ID3D12Resource> id;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    bool renderTarget = false;
    Texture() = default;
    Texture(void* context, Params params) : context(context), device(deviceFrom(context)),
        width(params.width), height(params.height), channels(params.channels), filter(params.filter) {
        if (!width || !height || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            throw std::invalid_argument("[DirectX] Invalid texture dimensions");
        if (channels != 1 && channels != 3 && channels != 4) throw std::invalid_argument("[DirectX] Unsupported channel count");
        // Explicit rejection rather than silently changing a requested filter.
        if (usesMipmaps(filter)) throw std::invalid_argument("[DirectX] Mipmap generation is not implemented yet");
        size = product(product(width, height), channels); pixels.resize(size); data = pixels.data();
        if (params.data) std::memcpy(data, params.data, size);
    }
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    static Texture* createInCurrentContext(void* context, Params params) { return new Texture(context, params); }
    static void destroyInCurrentContext(Texture* texture) { delete texture; }
    static bool usesMipmaps(Filter filter) { return filter != Filter::Nearest && filter != Filter::Bilinear; }
    void markChanged() { ++revision; }
    void update(const unsigned char* source) {
        if (!source) throw std::invalid_argument("[DirectX] Null texture pixels");
        if (renderTarget) throw std::logic_error("[DirectX] Cannot CPU-update a render target");
        std::memcpy(data, source, size); markChanged();
    }
    void upload() {
        auto& ctx = Context::active(device);
        if (renderTarget || uploadedRevision == revision) return;
        DXGI_FORMAT format = channels == 1 ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
        auto desc = textureDesc(checkedUInt(width), checkedUInt(height), format);
        auto next = device->create(desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT rows; UINT64 rowBytes, total;
        device->gpu->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
        auto staging = device->create(bufferDesc(total), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        unsigned char* mapped = nullptr; D3D12_RANGE empty{0, 0};
        check(staging->Map(0, &empty, reinterpret_cast<void**>(&mapped)), "Map texture staging");
        for (size_t y = 0; y < height; ++y) {
            auto* destination = mapped + footprint.Offset + y * footprint.Footprint.RowPitch;
            const auto* source = data + y * width * channels;
            if (channels == 3) {
                for (size_t x = 0; x < width; ++x) {
                    std::memcpy(destination + x * 4, source + x * 3, 3); destination[x * 4 + 3] = 255;
                }
            } else std::memcpy(destination, source, width * channels);
        }
        staging->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource = next.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = staging.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; source.PlacedFootprint = footprint;
        ctx.list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        // COMMON permits read-only promotion on any canvas queue after completion.
        ctx.transition(next.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        ctx.retain(staging); ctx.retain(next);
        id = std::move(next); state = D3D12_RESOURCE_STATE_COMMON; uploadedRevision = revision;
        // Other canvases sharing this texture must wait for its upload submission.
        uploadFence = ctx.nextFence;
        uploadQueue = ctx.queue; uploadCompletion = ctx.fence;
    }
    UINT64 uploadFence = 0;
    ComPtr<ID3D12CommandQueue> uploadQueue;
    ComPtr<ID3D12Fence> uploadCompletion;
    void bind(size_t unit = 0) {
        if (unit >= Device::textureSlots) throw std::out_of_range("[DirectX] Texture slot");
        auto& ctx = Context::active(device); upload();
        if (uploadQueue && uploadQueue.Get() != ctx.queue.Get()) {
            // A different canvas cannot be recording concurrently on this thread.
            check(ctx.queue->Wait(uploadCompletion.Get(), uploadFence), "Wait for shared texture upload");
        }
        if (renderTarget && state == D3D12_RESOURCE_STATE_RENDER_TARGET)
            throw std::logic_error("[DirectX] Finish rendering to the framebuffer before sampling it");
        ctx.retain(id);
        auto descriptor = ctx.descriptor();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = channels == 1 ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MipLevels = 1;
        srv.Shader4ComponentMapping = channels == 1 ? D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0,0,0,0) : D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        device->gpu->CreateShaderResourceView(id.Get(), &srv, descriptor.cpu);
        ctx.list->SetGraphicsRootDescriptorTable(Device::uniformSlots + static_cast<UINT>(unit), descriptor.gpu);
        auto sampler = ctx.descriptor(true);
        D3D12_SAMPLER_DESC sampling{};
        sampling.Filter = filter == Filter::Nearest ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampling.AddressU = sampling.AddressV = sampling.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampling.MaxAnisotropy = 1; sampling.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampling.MaxLOD = 0;
        device->gpu->CreateSampler(&sampling, sampler.cpu);
        ctx.list->SetGraphicsRootDescriptorTable(Device::uniformSlots + Device::textureSlots + static_cast<UINT>(unit), sampler.gpu);
    }
    void unbind(size_t unit = 0) {
        if (unit >= Device::textureSlots) throw std::out_of_range("[DirectX] Texture slot");
        auto& ctx = Context::active(device); auto descriptor = ctx.descriptor();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        device->gpu->CreateShaderResourceView(nullptr, &srv, descriptor.cpu);
        ctx.list->SetGraphicsRootDescriptorTable(Device::uniformSlots + static_cast<UINT>(unit), descriptor.gpu);
    }
};

struct FrameBuffer {
    struct Params { size_t width = 0, height = 0, colorChannels = 4; };
    void* context;
    std::shared_ptr<Device> device;
    Params params;
    std::unique_ptr<Texture> ownedTexture;
    Texture* texture = nullptr;
    ComPtr<ID3D12Resource> stencil;
    ComPtr<ID3D12DescriptorHeap> colorHeap, depthHeap;
    FrameBuffer(void* context, Params params) : context(context), device(deviceFrom(context)), params(params) {
        if (params.colorChannels != 4) throw std::invalid_argument("[DirectX] Framebuffers require RGBA");
        resize(params.width, params.height);
    }
    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;
    void resize(size_t width, size_t height) {
        width = std::max<size_t>(width, 1); height = std::max<size_t>(height, 1);
        if (texture && params.width == width && params.height == height) return;
        auto next = std::make_unique<Texture>(context, Texture::Params{nullptr, width, height, 4});
        next->renderTarget = true;
        auto desc = textureDesc(checkedUInt(width), checkedUInt(height), DXGI_FORMAT_R8G8B8A8_UNORM);
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{}; clear.Format = desc.Format;
        std::fill(std::begin(clear.Color), std::end(clear.Color), 1.0f);
        next->id = device->create(desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, &clear);
        desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        clear.Format = desc.Format; clear.DepthStencil = {1.0f, 0};
        auto depth = device->create(desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear);
        auto rtv = device->heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
        auto dsv = device->heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
        device->gpu->CreateRenderTargetView(next->id.Get(), nullptr, rtv->GetCPUDescriptorHandleForHeapStart());
        device->gpu->CreateDepthStencilView(depth.Get(), nullptr, dsv->GetCPUDescriptorHandleForHeapStart());
        ownedTexture = std::move(next); texture = ownedTexture.get(); stencil = std::move(depth);
        colorHeap = std::move(rtv); depthHeap = std::move(dsv); params.width = width; params.height = height;
    }
    void bind() {
        auto& ctx = Context::active(device);
        if (&ctx != context) throw std::logic_error("[DirectX] Framebuffers belong to their creating canvas");
        ctx.transition(texture->id.Get(), texture->state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        texture->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ctx.rtv = colorHeap->GetCPUDescriptorHandleForHeapStart(); ctx.dsv = depthHeap->GetCPUDescriptorHandleForHeapStart();
        ctx.list->OMSetRenderTargets(1, &ctx.rtv, FALSE, &ctx.dsv);
        D3D12_VIEWPORT viewport{0, 0, float(params.width), float(params.height), 0, 1};
        D3D12_RECT rect{0, 0, static_cast<LONG>(params.width), static_cast<LONG>(params.height)};
        ctx.list->RSSetViewports(1, &viewport); ctx.list->RSSetScissorRects(1, &rect);
        ctx.retain(texture->id); ctx.retain(stencil); ctx.retain(colorHeap); ctx.retain(depthHeap);
    }
    void finish() {
        auto& ctx = Context::active(device);
        if (&ctx != context) throw std::logic_error("[DirectX] Framebuffers belong to their creating canvas");
        ctx.transition(texture->id.Get(), texture->state, D3D12_RESOURCE_STATE_COMMON);
        texture->state = D3D12_RESOURCE_STATE_COMMON;
        texture->uploadQueue = ctx.queue; texture->uploadCompletion = ctx.fence; texture->uploadFence = ctx.nextFence;
    }
};
}

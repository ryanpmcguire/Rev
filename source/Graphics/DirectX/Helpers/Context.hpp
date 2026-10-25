#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace Rev::Graphics::DirectX {
using Microsoft::WRL::ComPtr;

inline void check(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(std::string("[DirectX] ") + operation +
        " (HRESULT " + std::to_string(static_cast<unsigned long>(result)) + ")");
}
inline UINT checkedUInt(size_t value) {
    if (value > UINT_MAX) throw std::overflow_error("[DirectX] Value exceeds UINT");
    return static_cast<UINT>(value);
}
inline size_t product(size_t a, size_t b) {
    if (b && a > SIZE_MAX / b) throw std::overflow_error("[DirectX] Resource size overflow");
    return a * b;
}
inline D3D12_RESOURCE_DESC bufferDesc(UINT64 bytes) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = std::max<UINT64>(bytes, 1);
    desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}
inline D3D12_RESOURCE_DESC textureDesc(UINT width, UINT height, DXGI_FORMAT format) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1;
    desc.MipLevels = 1; desc.Format = format; desc.SampleDesc.Count = 1;
    return desc;
}

// One device/root signature across windows, matching Rev's static primitive resources.
struct Device {
    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Device> gpu;
    ComPtr<ID3D12RootSignature> root;
    static constexpr UINT uniformSlots = 8, textureSlots = 8;

    explicit Device(bool warp = false, bool debug = false) {
        if (debug) {
            ComPtr<ID3D12Debug> layer;
            check(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)), "Enable debug layer");
            layer->EnableDebugLayer();
        }
        check(CreateDXGIFactory2(debug ? DXGI_CREATE_FACTORY_DEBUG : 0,
            IID_PPV_ARGS(&factory)), "Create DXGI factory");
        ComPtr<IDXGIAdapter> adapter;
        if (warp) check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "Find WARP adapter");
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&gpu)), "Create D3D12 device");

        // b0..b7, independent t0..t7 and s0..s7 tables, visible to both stages.
        std::array<D3D12_ROOT_PARAMETER, uniformSlots + textureSlots * 2> parameters{};
        std::array<D3D12_DESCRIPTOR_RANGE, textureSlots * 2> ranges{};
        for (UINT i = 0; i < uniformSlots; ++i) {
            parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameters[i].Descriptor.ShaderRegister = i;
        }
        for (UINT i = 0; i < textureSlots * 2; ++i) {
            ranges[i].RangeType = i < textureSlots ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            ranges[i].NumDescriptors = 1;
            ranges[i].BaseShaderRegister = i % textureSlots;
            parameters[uniformSlots + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[uniformSlots + i].DescriptorTable = {1, &ranges[i]};
        }
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = static_cast<UINT>(parameters.size()); desc.pParameters = parameters.data();
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> bytes, errors;
        HRESULT result = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &bytes, &errors);
        if (FAILED(result) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
        check(result, "Serialize root signature");
        check(gpu->CreateRootSignature(0, bytes->GetBufferPointer(), bytes->GetBufferSize(),
            IID_PPV_ARGS(&root)), "Create root signature");
    }
    static std::shared_ptr<Device> shared() {
        static auto instance = std::make_shared<Device>();
        return instance;
    }
    ComPtr<ID3D12Resource> create(const D3D12_RESOURCE_DESC& desc, D3D12_HEAP_TYPE type,
        D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear = nullptr) {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = type;
        ComPtr<ID3D12Resource> resource;
        check(gpu->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state,
            clear, IID_PPV_ARGS(&resource)), "Create resource");
        return resource;
    }
    ComPtr<ID3D12DescriptorHeap> heap(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, bool visible = false) {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = type; desc.NumDescriptors = count;
        desc.Flags = visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ComPtr<ID3D12DescriptorHeap> result;
        check(gpu->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&result)), "Create descriptor heap");
        return result;
    }
};

struct Pipeline;
struct RenderState {
    bool color = true, stencilWrite = false, depth = false, depthWrite = true;
    D3D12_STENCIL_OP stencilOp = D3D12_STENCIL_OP_KEEP;
    UINT stencilRef = 0;
    float biasFactor = 0;
    INT biasUnits = 0;
};

struct Context {
    static constexpr UINT frameCount = 2, srvCapacity = 16384, samplerCapacity = 2048;
    struct Frame {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12DescriptorHeap> srvs, samplers;
        UINT srvUsed = 0, samplerUsed = 0;
        UINT64 fence = 0;
        std::vector<ComPtr<IUnknown>> retained;
    };
    std::shared_ptr<Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<IDXGISwapChain3> swapchain;
    std::array<ComPtr<ID3D12Resource>, frameCount> backbuffers;
    std::array<Frame, frameCount> frames;
    HANDLE event = nullptr;
    UINT64 nextFence = 1;
    UINT frameIndex = 0, width = 0, height = 0;
    bool recording = false;
    std::thread::id owner = std::this_thread::get_id();
    RenderState state;
    Pipeline* pipeline = nullptr;
    std::vector<size_t> vertexAttribs;
    UINT vertexDivisor = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{}, dsv{};
    inline static thread_local Context* current = nullptr;

    explicit Context(HWND window = nullptr, std::shared_ptr<Device> dev = Device::shared()) : device(std::move(dev)) {
        D3D12_COMMAND_QUEUE_DESC desc{}; desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->gpu->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "Create command queue");
        check(device->gpu->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Create fence");
        for (auto& frame : frames) {
            check(device->gpu->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&frame.allocator)), "Create command allocator");
            frame.srvs = device->heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srvCapacity, true);
            frame.samplers = device->heap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, samplerCapacity, true);
        }
        check(device->gpu->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            frames[0].allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Create command list");
        check(list->Close(), "Close initial command list");
        if (window) {
            DXGI_SWAP_CHAIN_DESC1 swap{};
            swap.Width = 1; swap.Height = 1; swap.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swap.SampleDesc.Count = 1; swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swap.BufferCount = frameCount; swap.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            ComPtr<IDXGISwapChain1> base;
            check(device->factory->CreateSwapChainForHwnd(queue.Get(), window, &swap,
                nullptr, nullptr, &base), "Create swap chain");
            check(base.As(&swapchain), "Query swap chain");
            check(device->factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER), "Set window association");
        }
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) check(HRESULT_FROM_WIN32(GetLastError()), "Create fence event");
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    ~Context() {
        try { waitIdle(); } catch (...) { /* Device removal must not throw from teardown. */ }
        if (current == this) current = nullptr;
        if (event) CloseHandle(event);
    }
    void requireOwner() const {
        if (owner != std::this_thread::get_id()) throw std::logic_error("[DirectX] Rendering must run on the context's owner thread");
    }
    void requireRecording() const {
        requireOwner();
        if (!recording || current != this) throw std::logic_error("[DirectX] No active command recording");
    }
    static Context& active(const std::shared_ptr<Device>& dev) {
        if (!current || current->device != dev) throw std::logic_error("[DirectX] No compatible active canvas");
        current->requireRecording(); return *current;
    }
    void wait(UINT64 value) {
        if (fence->GetCompletedValue() == UINT64_MAX) check(device->gpu->GetDeviceRemovedReason(), "Device removed");
        if (fence->GetCompletedValue() >= value) return;
        check(fence->SetEventOnCompletion(value, event), "Set fence event");
        if (WaitForSingleObject(event, INFINITE) != WAIT_OBJECT_0) throw std::runtime_error("[DirectX] Fence wait failed");
    }
    void waitIdle() {
        if (!queue || !event) return;
        auto value = nextFence++;
        check(queue->Signal(fence.Get(), value), "Signal idle fence"); wait(value);
    }
    void resize(UINT w, UINT h) {
        requireOwner();
        if (recording) throw std::logic_error("[DirectX] Resize during frame");
        w = std::max(w, 1u); h = std::max(h, 1u);
        if (w == width && h == height) return;
        waitIdle();
        if (swapchain) {
            for (auto& frame : frames) frame.retained.clear();
            for (auto& buffer : backbuffers) buffer.Reset();
            check(swapchain->ResizeBuffers(frameCount, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0), "Resize swap chain");
            for (UINT i = 0; i < frameCount; ++i)
                check(swapchain->GetBuffer(i, IID_PPV_ARGS(&backbuffers[i])), "Get back buffer");
        }
        width = w; height = h;
    }
    void begin() {
        requireOwner();
        if (recording || current) throw std::logic_error("[DirectX] Nested canvas recording");
        if (swapchain) frameIndex = swapchain->GetCurrentBackBufferIndex();
        auto& frame = frames[frameIndex]; wait(frame.fence);
        frame.retained.clear(); frame.srvUsed = frame.samplerUsed = 0;
        check(frame.allocator->Reset(), "Reset allocator");
        check(list->Reset(frame.allocator.Get(), nullptr), "Reset command list");
        ID3D12DescriptorHeap* heaps[] = {frame.srvs.Get(), frame.samplers.Get()};
        list->SetDescriptorHeaps(2, heaps); list->SetGraphicsRootSignature(device->root.Get());
        state = {}; pipeline = nullptr; vertexAttribs.clear(); vertexDivisor = 0;
        recording = true; current = this;
    }
    template<class T> void retain(const ComPtr<T>& object) {
        if (!object) return;
        ComPtr<IUnknown> ref; check(object.As(&ref), "Retain GPU object");
        frames[frameIndex].retained.push_back(std::move(ref));
    }
    void transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        requireRecording(); if (before == after) return;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
        list->ResourceBarrier(1, &barrier);
    }
    struct Descriptor { D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu; };
    Descriptor descriptor(bool sampler = false) {
        requireRecording(); auto& frame = frames[frameIndex];
        UINT& used = sampler ? frame.samplerUsed : frame.srvUsed;
        if (used >= (sampler ? samplerCapacity : srvCapacity)) throw std::runtime_error("[DirectX] Per-frame descriptor capacity exceeded");
        auto* heap = sampler ? frame.samplers.Get() : frame.srvs.Get();
        UINT stride = device->gpu->GetDescriptorHandleIncrementSize(sampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart(); auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
        cpu.ptr += SIZE_T(used) * stride; gpu.ptr += UINT64(used++) * stride; return {cpu, gpu};
    }
    void submit() {
        requireRecording(); check(list->Close(), "Close frame");
        ID3D12CommandList* commands[] = {list.Get()}; queue->ExecuteCommandLists(1, commands);
        auto value = nextFence++;
        check(queue->Signal(fence.Get(), value), "Signal frame fence"); frames[frameIndex].fence = value;
        recording = false; current = nullptr;
        if (swapchain) check(swapchain->Present(1, 0), "Present");
        else frameIndex = (frameIndex + 1) % frameCount;
    }
};

inline std::shared_ptr<Device> deviceFrom(void* context) {
    if (!context) throw std::invalid_argument("[DirectX] Null context");
    return static_cast<Context*>(context)->device;
}
} // namespace Rev::Graphics::DirectX

#include "device_d3d12.hpp"

#include <nvrhi/d3d12.h>
#include <nvrhi/validation.h>

#include <SDL3/SDL_log.h>

using Microsoft::WRL::ComPtr;

namespace od {

namespace {

struct LogCallback final : nvrhi::IMessageCallback {
    void message(nvrhi::MessageSeverity severity, const char* text) override {
        static const char* kNames[] = {"info", "warning", "error", "fatal"};
        if (severity == nvrhi::MessageSeverity::Info) return;
        SDL_Log("nvrhi %s: %s", kNames[int(severity)], text);
    }
};
LogCallback g_log;

} // namespace

DeviceD3D12::~DeviceD3D12() {
    if (device_) device_->waitForIdle();
    backbuffers_.clear();
    device_ = nullptr;
    if (fenceEvent_) CloseHandle(fenceEvent_);
}

bool DeviceD3D12::init(HWND hwnd, int w, int h, bool debug) {
    width_ = w > 0 ? w : 1;
    height_ = h > 0 ? h : 1;
    if (debug) {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer();
    }
    // the debug factory needs the Windows "Graphics Tools" feature: without it, run with the plain factory
    if (debug && FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory_))))
        SDL_Log("d3d12: DXGI debug factory unavailable (Graphics Tools not installed?), continuing without it");
    if (!factory_ && FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)))) {
        SDL_Log("d3d12: CreateDXGIFactory2 failed");
        return false;
    }
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory_->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))) ||
        FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3d_)))) {
        SDL_Log("d3d12: no feature level 12_0 device");
        return false;
    }
    DXGI_ADAPTER_DESC1 ad{};
    adapter->GetDesc1(&ad);
    SDL_Log("d3d12: %ls", ad.Description);
    // the shaders are DXIL (SM 6.0; the ray-query scene shader 6.5). A runtime that does not know the asked model
    // answers E_INVALIDARG, so ask from the newest down
    for (D3D_SHADER_MODEL sm : {D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5, D3D_SHADER_MODEL_6_4,
                                D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2, D3D_SHADER_MODEL_6_1, D3D_SHADER_MODEL_6_0}) {
        D3D12_FEATURE_DATA_SHADER_MODEL q{sm};
        if (SUCCEEDED(d3d_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &q, sizeof(q)))) {
            shaderModel_ = q.HighestShaderModel;
            break;
        }
    }
    SDL_Log("d3d12: shader model %d.%d", shaderModel_ >> 4, shaderModel_ & 0xf);
    if (shaderModel_ < D3D_SHADER_MODEL_6_0) {
        SDL_Log("d3d12: shader model 6.0 required (update the graphics driver)");
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(d3d_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)))) return false;

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = UINT(width_);
    sd.Height = UINT(height_);
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kBackBuffers;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    // A flip-model swapchain's Present(0) still waits for the refresh in a window / borderless unless it may tear
    BOOL tearing = FALSE;
    if (SUCCEEDED(factory_->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof tearing)) &&
        tearing)
        swapFlags_ = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    sd.Flags = swapFlags_;
    ComPtr<IDXGISwapChain1> sc;
    if (FAILED(factory_->CreateSwapChainForHwnd(queue_.Get(), hwnd, &sd, nullptr, nullptr, &sc)) ||
        FAILED(sc.As(&swap_))) {
        SDL_Log("d3d12: swapchain creation failed");
        return false;
    }
    factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    if (FAILED(d3d_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) return false;
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    nvrhi::d3d12::DeviceDesc dd;
    dd.errorCB = &g_log;
    dd.pDevice = d3d_.Get();
    dd.pGraphicsCommandQueue = queue_.Get();
    device_ = nvrhi::d3d12::createDevice(dd);
    if (!device_) return false;
    if (debug) device_ = nvrhi::validation::createValidationLayer(device_);
    return createBackbuffers();
}

bool DeviceD3D12::createBackbuffers() {
    backbuffers_.clear();
    for (UINT i = 0; i < kBackBuffers; ++i) {
        ComPtr<ID3D12Resource> res;
        if (FAILED(swap_->GetBuffer(i, IID_PPV_ARGS(&res)))) return false;
        nvrhi::TextureDesc td;
        td.width = uint32_t(width_);
        td.height = uint32_t(height_);
        td.format = nvrhi::Format::RGBA8_UNORM;
        td.debugName = "backbuffer";
        td.isRenderTarget = true;
        td.initialState = nvrhi::ResourceStates::Present;
        td.keepInitialState = true;
        backbuffers_.push_back(
            device_->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(res.Get()), td));
    }
    return true;
}

void DeviceD3D12::resize(int w, int h) {
    if (w <= 0 || h <= 0 || (w == width_ && h == height_)) return;
    device_->waitForIdle();
    backbuffers_.clear();
    device_->runGarbageCollection();
    if (FAILED(swap_->ResizeBuffers(0, UINT(w), UINT(h), DXGI_FORMAT_UNKNOWN, swapFlags_))) {
        SDL_Log("d3d12: ResizeBuffers failed");
        createBackbuffers(); // keep presenting at the old size
        return;
    }
    width_ = w;
    height_ = h;
    createBackbuffers();
}

void DeviceD3D12::present(bool vsync) {
    swap_->Present(vsync ? 1 : 0, !vsync && swapFlags_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
    queue_->Signal(fence_.Get(), ++fenceValue_);
    if (fenceValue_ > 1 && fence_->GetCompletedValue() < fenceValue_ - 1) { // two frames in flight
        fence_->SetEventOnCompletion(fenceValue_ - 1, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
    device_->runGarbageCollection();
}

} // namespace od

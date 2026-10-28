// D3D12 device + flip-model swapchain wrapped in an NVRHI device (ADR 0004). Two frames in flight.
#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <nvrhi/nvrhi.h>
#include <wrl/client.h>

#include <vector>

namespace od {

class DeviceD3D12 {
public:
    static constexpr UINT kBackBuffers = 3;

    ~DeviceD3D12();
    bool init(HWND hwnd, int w, int h, bool debug);
    void resize(int w, int h); // waits idle and recreates the backbuffer handles; 0x0 (minimised) is ignored
    nvrhi::IDevice* device() const { return device_.Get(); } // validation-wrapped in debug builds
    nvrhi::ITexture* backbuffer() const { return backbuffers_[swap_->GetCurrentBackBufferIndex()].Get(); }
    int width() const { return width_; }
    int height() const { return height_; }
    void present(bool vsync); // Present, then wait until at most one frame is still in flight
    ID3D12Device* native() const { return d3d_.Get(); } // vendor SDK init only
    D3D_SHADER_MODEL shaderModel() const { return shaderModel_; } // the highest the runtime + driver accept
    bool rayQueryShaders() const { return shaderModel_ >= D3D_SHADER_MODEL_6_5; } // the ray-query scene shader is SM 6.5

private:
    bool createBackbuffers();

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory_;
    Microsoft::WRL::ComPtr<ID3D12Device> d3d_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    UINT64 fenceValue_ = 0;
    nvrhi::DeviceHandle device_;
    std::vector<nvrhi::TextureHandle> backbuffers_;
    int width_ = 1, height_ = 1;
    D3D_SHADER_MODEL shaderModel_ = D3D_SHADER_MODEL_5_1;
    UINT swapFlags_ = 0; // DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING when the system has it (vsync off uncapped)
};

} // namespace od

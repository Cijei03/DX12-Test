#define GLFW_EXPOSE_NATIVE_WIN32
 
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
 
#include <iostream>
#include <cassert>
 
using namespace Microsoft::WRL;
 
GLFWwindow* wnd{};
 
ComPtr<ID3D12Debug> DebugController;
void InitDebugLayer()
{
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&DebugController))))
    {
        DebugController->EnableDebugLayer();
    }
}
 
ComPtr<IDXGIFactory4> dxgiFactory{};
void InitDXGIFactory()
{
    const auto result = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&dxgiFactory));
    assert(result == S_OK);
}
ComPtr<IDXGIAdapter1> dxgiAdapter{};
void InitDXGIAdapter()
{
    for (UINT i = 0; SUCCEEDED(dxgiFactory->EnumAdapters1(i, &dxgiAdapter)); i++)
    {
        DXGI_ADAPTER_DESC1 adapterInfo;
 
        dxgiAdapter->GetDesc1(&adapterInfo);
 
        if (adapterInfo.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;
        }
        else
        {
            return;
        }
    }
 
    std::cerr << "Couldnt find D3D12 Device" << std::endl;
    exit(0);
}
 
 
ComPtr<ID3D12Device> dvc{};
void InitDevice()
{
    const auto result = D3D12CreateDevice(dxgiAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dvc));
 
    assert(result == S_OK);
}
 
ComPtr<ID3D12CommandQueue> commandBuffer{};
void InitCommandBuffer()
{
    D3D12_COMMAND_QUEUE_DESC creationInfo{};
    creationInfo.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
 
    assert(dvc->CreateCommandQueue(&creationInfo, IID_PPV_ARGS(&commandBuffer)) == S_OK);
}
 
ComPtr<ID3D12CommandAllocator> commandAllocator{};
void InitCommandAllocator()
{
    const auto result = dvc->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    assert(result == S_OK);
}
 
ComPtr<ID3D12GraphicsCommandList> commandList{};
void InitCommandList()
{
    const auto result = dvc->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
    assert(result == S_OK);
 
    commandList->Close();
}
 
ComPtr<IDXGISwapChain3> swp{};
void InitSwapchain()
{
    DXGI_SWAP_CHAIN_DESC1 creationInfo{};
    creationInfo.BufferCount = 2;
    creationInfo.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    creationInfo.Flags = 0;
    creationInfo.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    creationInfo.SampleDesc.Count = 1;
    creationInfo.SampleDesc.Quality = 0;
    creationInfo.Width = 1280;
    creationInfo.Height = 720;
    creationInfo.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> localSwp;
    const auto result = dxgiFactory->CreateSwapChainForHwnd(commandBuffer.Get(), glfwGetWin32Window(wnd), &creationInfo, nullptr, nullptr, &localSwp);
    assert(result == S_OK);
 
    localSwp.As(&swp);
}
 
ComPtr<ID3D12DescriptorHeap> RenderTargetViewHeap;
D3D12_CPU_DESCRIPTOR_HANDLE RTV_CPU_Handles[2];
ComPtr<ID3D12Resource> SwapchainSurface[2];
D3D12_CPU_DESCRIPTOR_HANDLE GetHeapOffset(SIZE_T StartHeapPtr, INT offsetInDescriptors, UINT descriptorIncrementSize) noexcept
{
    D3D12_CPU_DESCRIPTOR_HANDLE tempDesc{};
    tempDesc.ptr = SIZE_T(INT64(StartHeapPtr) + INT64(offsetInDescriptors) * INT64(descriptorIncrementSize));
    return tempDesc;
}
void InitRenderTargetViews()
{
    D3D12_DESCRIPTOR_HEAP_DESC creationInfo{};
    creationInfo.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    creationInfo.NumDescriptors = 2;
    creationInfo.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    creationInfo.NodeMask = 0;
 
    
    assert(dvc->CreateDescriptorHeap(&creationInfo, IID_PPV_ARGS(&RenderTargetViewHeap)) == S_OK);
 
    auto RTV_CPU_Handle = RenderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
 
    const auto RTVSize = dvc->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
 
    for (uint32_t i = 0; i < 2; i++)
    {
        assert(swp->GetBuffer(0, IID_PPV_ARGS(&SwapchainSurface[i])) == S_OK);
        swp->GetBuffer(0, IID_PPV_ARGS(&SwapchainSurface[i]));
        RTV_CPU_Handles[i] = GetHeapOffset(RTV_CPU_Handle.ptr, i, RTVSize);
        dvc->CreateRenderTargetView(SwapchainSurface[i].Get(), nullptr, RTV_CPU_Handles[i]);
    }
    
}
 
ComPtr<ID3D12Fence> Fence{};
HANDLE FenceEventHandler{};
void InitFence()
{
    const auto result = dvc->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence));
    assert(result == S_OK);
    FenceEventHandler = CreateEvent(nullptr, false, false, nullptr);
    assert(FenceEventHandler);
}
 
D3D12_CPU_DESCRIPTOR_HANDLE Current_RTV_CPU_Handle{};
uint32_t CurrentBufferIndex;
void ConfigBuffers()
{
    // Setup backbuffer barrier.
    {
        D3D12_RESOURCE_BARRIER barrierInfo{};
        barrierInfo.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierInfo.Transition.pResource = SwapchainSurface[CurrentBufferIndex].Get();
        barrierInfo.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrierInfo.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrierInfo.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
 
        commandList->ResourceBarrier(1, &barrierInfo);
    }
    Current_RTV_CPU_Handle = RTV_CPU_Handles[CurrentBufferIndex];
 
    float ClearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
 
    commandList->ClearRenderTargetView(Current_RTV_CPU_Handle, ClearColor, 0, nullptr);
 
    // Setup frontbuffer barrier.
    {
        D3D12_RESOURCE_BARRIER barrierInfo{};
        barrierInfo.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierInfo.Transition.pResource = SwapchainSurface[CurrentBufferIndex].Get();
        barrierInfo.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrierInfo.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        barrierInfo.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
 
        commandList->ResourceBarrier(1, &barrierInfo);
    }
}
 
int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    wnd = glfwCreateWindow(1280, 720, "D3D12 Tutorial", nullptr, nullptr);
    
    InitDebugLayer();
    InitDXGIFactory();
    InitDXGIAdapter();
    InitDevice();
    InitCommandBuffer();
    InitCommandAllocator();
    InitFence();
    InitCommandList();
    InitSwapchain();
    InitRenderTargetViews();
 
    CurrentBufferIndex = swp->GetCurrentBackBufferIndex();
 
    int x = 0;
 
    while (!glfwWindowShouldClose(wnd) && x < 2)
    {
        glfwPollEvents();
 
        commandAllocator->Reset();
 
        commandList->Reset(commandAllocator.Get(), nullptr);
 
        ConfigBuffers();
 
        //commandList->OMSetRenderTargets(1, &Current_RTV_CPU_Handle, true, nullptr);
 
        commandList->Close();
 
        ID3D12CommandList* Cmds[] =
        {
            commandList.Get()
        };
 
        commandBuffer->ExecuteCommandLists(1, Cmds);
 
        swp->Present(1, 0);
 
        commandBuffer->Signal(Fence.Get(), 1);
 
        if (Fence->GetCompletedValue() != 1)
        {
            Fence->SetEventOnCompletion(1, FenceEventHandler);
            WaitForSingleObject(FenceEventHandler, INFINITE);
        }
        CurrentBufferIndex = swp->GetCurrentBackBufferIndex();
        x++;
    }
 
    glfwTerminate();
}

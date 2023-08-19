#define GLFW_EXPOSE_NATIVE_WIN32
 
#include <glfw3.h>
#include <glfw3native.h>
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
 
ComPtr<ID3D12CommandQueue>      commandBuffer{};
_CONSTEVAL auto                 COMMAND_LIST_TYPE  = D3D12_COMMAND_LIST_TYPE_DIRECT;
void InitCommandBuffer()
{
    D3D12_COMMAND_QUEUE_DESC creationInfo{};
    creationInfo.Type = COMMAND_LIST_TYPE;
 
    assert(dvc->CreateCommandQueue(&creationInfo, IID_PPV_ARGS(&commandBuffer)) == S_OK);
}
 
ComPtr<ID3D12CommandAllocator> commandAllocator{};
void InitCommandAllocator()
{
    const auto result = dvc->CreateCommandAllocator(COMMAND_LIST_TYPE, IID_PPV_ARGS(&commandAllocator));
    assert(result == S_OK);
    commandAllocator->Reset();
}
 
ComPtr<ID3D12GraphicsCommandList> commandList{};
void InitCommandList()
{
    const auto result = dvc->CreateCommandList(0, COMMAND_LIST_TYPE, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
    assert(result == S_OK);
 
    commandList->Close();
}
 
ComPtr<IDXGISwapChain3>     swp{};
_CONSTEVAL UINT32           BACK_BUFFER_COUNT = 2;
void InitSwapchain()
{
    DXGI_SWAP_CHAIN_DESC1 creationInfo{};
    creationInfo.BufferCount = BACK_BUFFER_COUNT;
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
 
ComPtr<ID3D12DescriptorHeap>        RenderTargetViewHeap;
D3D12_CPU_DESCRIPTOR_HANDLE         RTV_CPU_Handles[BACK_BUFFER_COUNT];
ComPtr<ID3D12Resource>              SwapchainSurface[BACK_BUFFER_COUNT];
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
    creationInfo.NumDescriptors = BACK_BUFFER_COUNT;
    creationInfo.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    creationInfo.NodeMask = 0;
 
    
    assert(dvc->CreateDescriptorHeap(&creationInfo, IID_PPV_ARGS(&RenderTargetViewHeap)) == S_OK);

    const auto RTV_CPU_Handle   = RenderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
    const auto RTVSize      = dvc->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
 
    for (uint32_t i = 0; i < BACK_BUFFER_COUNT; i++)
    {
        //assert(swp->GetBuffer(0, IID_PPV_ARGS(&SwapchainSurface[i])) == S_OK);
        //NOTE We should iterate over all of BACK_BUFFER_COUNT in swap chain !!!
        swp->GetBuffer(i, IID_PPV_ARGS(&SwapchainSurface[i]));
        RTV_CPU_Handles[i] = GetHeapOffset(RTV_CPU_Handle.ptr, i, RTVSize);
        dvc->CreateRenderTargetView(SwapchainSurface[i].Get(), nullptr, RTV_CPU_Handles[i]);
    }
}
 
ComPtr<ID3D12Fence>     Fence{};
HANDLE                  FenceEventHandler{};
void InitFence()
{
    const auto result = dvc->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence));
    assert(result == S_OK);
    FenceEventHandler = CreateEvent(nullptr, false, false, nullptr);
    assert(FenceEventHandler);
}
 
D3D12_CPU_DESCRIPTOR_HANDLE     Current_RTV_CPU_Handle{};
uint32_t                        CurrentBufferIndex{};
void ConfigBuffers()
{
    constexpr auto STATE_PRESENT        = D3D12_RESOURCE_STATE_PRESENT;
    constexpr auto STATE_RENDER_TARGET  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Current_RTV_CPU_Handle = RTV_CPU_Handles[CurrentBufferIndex];

    // Setup backbuffer barrier.
    {
        D3D12_RESOURCE_BARRIER barrierInfo{};
        barrierInfo.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierInfo.Transition.pResource = SwapchainSurface[CurrentBufferIndex].Get();
        barrierInfo.Transition.StateBefore = STATE_PRESENT;
        barrierInfo.Transition.StateAfter = STATE_RENDER_TARGET;
        barrierInfo.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
 
        commandList->ResourceBarrier(1, &barrierInfo);
    }

    constexpr float ClearColor[] = { 0.5f, 0.5f, 0.0f, 1.0f };
    commandList->OMSetRenderTargets(1, &Current_RTV_CPU_Handle, true, nullptr);
    commandList->ClearRenderTargetView(Current_RTV_CPU_Handle, ClearColor, 0, nullptr);

    // Setup frontbuffer barrier.
    {
        D3D12_RESOURCE_BARRIER barrierInfo{};
        barrierInfo.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierInfo.Transition.pResource = SwapchainSurface[CurrentBufferIndex].Get();
        barrierInfo.Transition.StateBefore = STATE_RENDER_TARGET;
        barrierInfo.Transition.StateAfter = STATE_PRESENT;
        barrierInfo.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
 
        commandList->ResourceBarrier(1, &barrierInfo);
    }

    // After all commands recording, close command list
    commandList->Close();
}
 
int main(int argc, char * argv[])
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

    UINT64 fenceValue = 1; // fence has been initialized with value 0, so on CPU side we start from 1,
	// when GPU will signal end of command list work submission, completed fence value will be 1 as well and so on.
    UINT64 swapChainBufferIdx = 0;
    while (!glfwWindowShouldClose(wnd))
    {
        glfwPollEvents();


        // Prepare Command Allocator and Command list for new commands recording
        commandAllocator->Reset();
        commandList->Reset(commandAllocator.Get(), nullptr);


		// Configure and populate all of the buffers needed for rendering a frame
        ConfigBuffers();


        // Execute command lists
        ID3D12CommandList* Cmds[] =
        {
            commandList.Get()
        };
        commandBuffer->ExecuteCommandLists(_countof(Cmds), Cmds);


        // Preset the back buffer
        swp->Present(1, 0);


        // Synchronization point (fence)
        commandBuffer->Signal(Fence.Get(), fenceValue);
        if (Fence->GetCompletedValue() < fenceValue)
        {
            Fence->SetEventOnCompletion(fenceValue, FenceEventHandler);
            WaitForSingleObject(FenceEventHandler, INFINITE);
        }

        CurrentBufferIndex = swp->GetCurrentBackBufferIndex();
        //CurrentBufferIndex = swp->GetCurrentBackBufferIndex() % BACK_BUFFER_COUNT; // modulo is not needed, just go to know and note it
        ++fenceValue;
    }
 
    glfwTerminate();

    return EXIT_SUCCESS;
}
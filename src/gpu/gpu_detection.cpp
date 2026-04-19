#include "gpu_detection.h"

#include <Windows.h>
#include <dxgi.h>
#include <d3d11.h>
#include <sstream>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

bool DetectDisplayGPU(GpuState& outState)
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
        return false;

    // Primary monitor handle
    POINT pt = { 0, 0 };
    HMONITOR hPrimaryMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        if (FAILED(adapter->GetDesc1(&desc)))
        {
            adapter->Release();
            continue;
        }

        // Skip software adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            adapter->Release();
            continue;
        }

        IDXGIOutput* output = nullptr;
        for (UINT j = 0; adapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND; ++j)
        {
            DXGI_OUTPUT_DESC outDesc;
            if (SUCCEEDED(output->GetDesc(&outDesc)))
            {
                if (outDesc.Monitor == hPrimaryMonitor)
                {
                    outState.name   = desc.Description;
                    outState.vendor = desc.VendorId;

                    output->Release();
                    adapter->Release();
                    factory->Release();
                    return true;
                }
            }
            output->Release();
        }

        adapter->Release();
    }

    factory->Release();
    return false;
}

bool ActivateRenderGPU()
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
        return false;

    // Find adapter with most dedicated VRAM (assume dGPU)
    IDXGIAdapter1* bestAdapter = nullptr;
    SIZE_T bestVram = 0;

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
            !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
        {
            if (desc.DedicatedVideoMemory > bestVram)
            {
                bestVram = desc.DedicatedVideoMemory;
                if (bestAdapter) bestAdapter->Release();
                bestAdapter = adapter;
                continue; // keep bestAdapter
            }
        }
        adapter->Release();
    }

    factory->Release();

    if (!bestAdapter)
        return false;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    HRESULT hr = D3D11CreateDevice(
        bestAdapter,
        D3D_DRIVER_TYPE_UNKNOWN, // required when passing explicit adapter
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device,
        nullptr,
        &context
    );

    if (context) context->Release();
    if (device)  device->Release();
    bestAdapter->Release();

    return SUCCEEDED(hr);
}

std::wstring BuildGpuTooltip(const GpuState& display)
{
    std::wstringstream ss;
    ss << L"Display GPU: "
       << (display.vendor ? display.name : L"<unknown>");
    return ss.str();
}

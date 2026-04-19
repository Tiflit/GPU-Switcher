#include "gpu_detection.h"

#include <dxgi.h>
#include <d3d11.h>
#include <sstream>

#include "../util/logging.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

bool DetectDisplayGPU(GpuState& outState)
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
    {
        LogError(L"Failed to create DXGI factory for display GPU detection.");
        return false;
    }

    IDXGIAdapter1* adapter = nullptr;
    bool found = false;

    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        IDXGIOutput* output = nullptr;
        if (adapter->EnumOutputs(0, &output) == S_OK)
        {
            DXGI_ADAPTER_DESC1 desc;
            if (SUCCEEDED(adapter->GetDesc1(&desc)))
            {
                outState.name   = desc.Description;
                outState.vendor = desc.VendorId;
                found = true;
            }

            output->Release();
            adapter->Release();
            break;
        }
        adapter->Release();
    }

    factory->Release();

    if (!found)
    {
        LogError(L"No display GPU detected.");
    }

    return found;
}

bool DetectRenderGPU(GpuState& outState)
{
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // default adapter
        D3D_DRIVER_TYPE_HARDWARE,   // hardware GPU
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device,
        nullptr,
        &context
    );

    if (FAILED(hr))
    {
        LogError(L"Failed to create D3D11 device for render GPU detection.");
        return false;
    }

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter   = nullptr;

    hr = device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (SUCCEEDED(hr))
        dxgiDevice->GetAdapter(&adapter);

    if (context)    context->Release();
    if (device)     device->Release();
    if (dxgiDevice) dxgiDevice->Release();

    if (!adapter)
    {
        LogError(L"Failed to get DXGI adapter from D3D11 device.");
        return false;
    }

    DXGI_ADAPTER_DESC desc;
    if (FAILED(adapter->GetDesc(&desc)))
    {
        adapter->Release();
        LogError(L"Failed to get DXGI adapter description for render GPU.");
        return false;
    }

    adapter->Release();

    outState.name   = desc.Description;
    outState.vendor = desc.VendorId;

    return true;
}

std::wstring BuildGpuTooltip(const GpuState& display, const GpuState& render)
{
    std::wstringstream ss;

    if (display.vendor != 0)
        ss << L"Display GPU: " << display.name << L"\n";
    else
        ss << L"Display GPU: <unknown>\n";

    if (render.vendor != 0)
        ss << L"Render GPU:  " << render.name << L"\n";
    else
        ss << L"Render GPU:  <unknown>\n";

    // If both are known and different, highlight the active renderer.
    if (display.vendor != 0 && render.vendor != 0 && display.vendor != render.vendor)
        ss << L"Active desktop rendering: " << render.name << L"\n";

    return ss.str();
}

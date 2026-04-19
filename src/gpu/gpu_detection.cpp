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
        return false;

    IDXGIAdapter1* adapter = nullptr;

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
            }

            output->Release();
            adapter->Release();
            factory->Release();
            return true;
        }
        adapter->Release();
    }

    factory->Release();
    return false;
}

bool DetectRenderGPU(GpuState& outState)
{
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
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
        return false;

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter   = nullptr;

    hr = device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (SUCCEEDED(hr))
        dxgiDevice->GetAdapter(&adapter);

    if (context)    context->Release();
    if (device)     device->Release();
    if (dxgiDevice) dxgiDevice->Release();

    if (!adapter)
        return false;

    DXGI_ADAPTER_DESC desc;
    if (FAILED(adapter->GetDesc(&desc)))
    {
        adapter->Release();
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

    ss << L"Display GPU: "
       << (display.vendor ? display.name : L"<unknown>") << L"\n";

    ss << L"Render GPU:  "
       << (render.vendor ? render.name : L"<unknown>") << L"\n";

    if (display.vendor && render.vendor && display.vendor != render.vendor)
        ss << L"Active desktop rendering: " << render.name << L"\n";

    return ss.str();
}

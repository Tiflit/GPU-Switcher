#include "gpu_detection.h"

#include <dxgi.h>
#include "../util/logging.h"

#pragma comment(lib, "dxgi.lib")

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

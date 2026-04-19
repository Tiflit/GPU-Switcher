#include "gpu_render_probe.h"

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

bool CreateDummyRenderDevice()
{
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // default adapter
        D3D_DRIVER_TYPE_HARDWARE,   // force hardware GPU
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

    return SUCCEEDED(hr);
}

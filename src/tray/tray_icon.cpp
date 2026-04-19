#include "tray_icon.h"

#include "../gpu/gpu_vendor.h"
#include "../resource.h"

HICON LoadDisplayIcon(UINT vendor, HINSTANCE hInst)
{
    if (vendor == 0)
    {
        // Detection failed → warning icon
        return LoadIconW(hInst, MAKEINTRESOURCEW(IDI_ICON_WARNING));
    }

    switch (static_cast<GpuVendor>(vendor))
    {
    case GpuVendor::Intel:
        return LoadIconW(hInst, MAKEINTRESOURCEW(IDI_ICON_INTEL));
    case GpuVendor::Nvidia:
        return LoadIconW(hInst, MAKEINTRESOURCEW(IDI_ICON_NVIDIA));
    case GpuVendor::AMD:
        return LoadIconW(hInst, MAKEINTRESOURCEW(IDI_ICON_AMD));
    default:
        return LoadIconW(hInst, MAKEINTRESOURCEW(IDI_ICON_UNKNOWN));
    }
}

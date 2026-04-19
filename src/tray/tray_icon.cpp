#include "tray_icon.h"
#include "resource.h"

HICON LoadDisplayIcon(UINT vendor, HINSTANCE hInst)
{
    int iconId = IDI_ICON_UNKNOWN;

    switch (vendor)
    {
    case static_cast<UINT>(GpuVendor::Intel):
        iconId = IDI_ICON_INTEL;
        break;

    case static_cast<UINT>(GpuVendor::Nvidia):
        iconId = IDI_ICON_NVIDIA;
        break;

    case static_cast<UINT>(GpuVendor::AMD):
        iconId = IDI_ICON_AMD;
        break;

    default:
        iconId = IDI_ICON_UNKNOWN;
        break;
    }

    return static_cast<HICON>(LoadImageW(
        hInst,
        MAKEINTRESOURCEW(iconId),
        IMAGE_ICON,
        0, 0,
        LR_DEFAULTSIZE | LR_SHARED
    ));
}

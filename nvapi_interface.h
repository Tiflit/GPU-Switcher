/*
 * nvapi_interface.h  — minimal stub
 * We load nvapi64.dll entirely at runtime via nvapi_QueryInterface().
 * No NVAPI SDK headers or .lib files are needed; this file just
 * provides the few typedefs that main.cpp references by name.
 *
 * All function pointers are resolved dynamically in NvapiLoad().
 */
#pragma once
#include <Windows.h>

// NVAPI status: 0 == NVAPI_OK
typedef int NvAPI_Status;

// Opaque handle types (we never dereference them)
typedef void* NvPhysicalGpuHandle;
typedef void* NvDisplayHandle;

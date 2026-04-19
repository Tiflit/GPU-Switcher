#pragma once

// Creates a dummy D3D11 device to force GPU driver recognition.
// Returns true if the device was created successfully.
bool CreateDummyRenderDevice();

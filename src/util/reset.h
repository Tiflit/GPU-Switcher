#pragma once

// Performs a full disable/enable cycle on physical PCI display adapters,
// skipping virtual and software display devices.
// Must be called from an elevated process.
// Blocks until all target adapters are re-enabled.
void CycleAllDisplayAdapters();

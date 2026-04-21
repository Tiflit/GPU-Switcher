#pragma once

// Performs a full disable/enable cycle on all display adapters.
// Must be called from an elevated process.
// Blocks until all adapters are re-enabled.
void CycleAllDisplayAdapters();

#pragma once

// Returns true if the app is registered to run at user logon.
bool IsStartupEnabled();

// Enables or disables run-at-startup registration.
void SetStartup(bool enable);

#include "UpdateChecker.h"
#include <xlog/xlog.h>

// Stub: there's no Ridgeline update server yet. Reimplement to query it once
// one exists. For now: always reports "no update."
bool UpdateChecker::CheckForUpdates()
{
    xlog::info("Update checker stub: no update server configured for Ridgeline yet");
    return false;
}

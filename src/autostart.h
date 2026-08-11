// CRTBender - "start with Windows" via the per-user Run key.
//
// HKCU is used deliberately: no elevation needed, and the correction is a
// per-user display preference rather than a machine-wide service.
#pragma once

namespace crtb {

bool IsAutostartEnabled();
// Writes or removes the Run entry. Returns true when the registry now matches
// the requested state.
bool SetAutostartEnabled(bool enabled);

} // namespace crtb

// The driver's IServerTrackedDeviceProvider as the legacy server sees it.
#pragma once

namespace shim
{

// Wraps the provider returned by the real driver's factory:
//   Init                  hands the driver a wrapped IVRDriverContext
//   GetInterfaceVersions  reports the component versions the legacy server speaks
void *WrapProvider(void *realProvider, const char *name);

} // namespace shim

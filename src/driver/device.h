// The legacy server's view of the driver's devices.
//
// IVRServerDriverHost_006 is the same on both sides, so calls pass through. The shim only
// intercepts TrackedDeviceAdded to hook each device's GetComponent, where the server's component
// versions are mapped onto the ones the driver implements.
#pragma once

#include "core/proxy.h"

namespace shim
{

Proxy *WrapServerDriverHost(void *realHost, const char *name);

} // namespace shim

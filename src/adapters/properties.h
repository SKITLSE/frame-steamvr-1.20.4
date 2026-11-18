// IVRProperties_001 is the same on both sides, so it is passed through. The one thing the shim
// does here is watch for the driver publishing the headset's chaperone JSON (property 2095) and
// give the legacy server the file-path property (2048) it understands instead.
#pragma once

#include "core/proxy.h"

namespace shim
{

Proxy *WrapProperties(void *realProperties, const char *name);

} // namespace shim

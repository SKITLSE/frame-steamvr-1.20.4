// IVRServerInternal_XXX: private interface of a current vrserver that the Frame driver calls.
// A legacy server has nothing like it. Only the methods the driver cannot do without are
// implemented; every other slot is a logging stub (see docs/NOTES.md for what they are).
#pragma once

#include "core/proxy.h"

namespace shim
{

Proxy *MakeServerInternal(const char *requestedName);

} // namespace shim

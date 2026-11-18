// The IVRDriverContext the Frame driver sees. Every interface the driver asks the server for
// goes through here, which is where missing versions get substituted.
#pragma once

#include "core/openvr_abi.h"

namespace shim
{

abi::IVRDriverContext *WrapDriverContext(abi::IVRDriverContext *legacyContext);

} // namespace shim

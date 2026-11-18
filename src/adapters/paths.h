// IVRPaths: a key/value store the driver publishes state through.
//
// The legacy server's IVRPaths_001 knows none of the paths a current driver uses, and nothing in
// a legacy SteamVR reads them, so the shim keeps the whole store itself. Two paths matter:
//   /steam/is_running           the driver reads it; answered with true
//   /steam/vr_connection_ready  the driver writes it; forwarded to Steam (see steam_handoff.h)
#pragma once

#include "core/proxy.h"

namespace shim
{

// A proxy that answers the IVRPaths version the driver asked for.
Proxy *MakePathStore(const char *requestedName);

} // namespace shim

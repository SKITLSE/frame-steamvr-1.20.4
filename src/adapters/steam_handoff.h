// Tells the Steam client how the headset should connect.
//
// The Frame driver publishes its connection parameters and a current vrserver passes them to
// Steam, which relays them to the headset; only then does the headset connect to the driver.
// A legacy vrserver has no such step, so the shim does it.
#pragma once

#include <string>

namespace shim
{

// Hands the parameters to Steam on a worker thread. The value is a secret: it is never logged
// and the copies made here are wiped afterwards.
void SendVrConnectionReady(const std::string &connectParams);

} // namespace shim

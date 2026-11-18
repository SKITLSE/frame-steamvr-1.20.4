// Settings from vrlink_shim.ini next to the DLL. Read once, when SteamVR loads the driver.
#pragma once

namespace shim
{

struct Config
{
	// Milliseconds ahead that a frame's head pose is reported as predicted (0..100).
	int predictMs = 22;
	// Honour recentering done on the headset by moving the bounds instead of the origin. Experimental.
	bool recenter = false;
	// Log the headset pose every two seconds. Diagnostics.
	bool poseLog = false;
};

// Reads the ini; writes one with the defaults and comments if it does not exist yet.
void LoadConfig();

const Config &GetConfig();

} // namespace shim

// Play area and floor level.
//
// The headset sends its own chaperone (bounds, standing origin, floor) as a chaperone_info JSON.
// A current server takes it from property 2095; a legacy server only understands 2048, a path
// to a .vrchap file holding the same JSON (the mechanism the Oculus driver used). This module
// turns the JSON into such a file, adjusted to what the legacy parser and chaperone model expect.
#pragma once

#include <string>

namespace shim
{

// One origin of a .vrchap universe: standing = R(yaw) * raw + translation.
struct ChaperoneOrigin
{
	double translation[3] = {0, 0, 0};
	double yaw = 0;
	bool valid = false;
};

struct ChaperoneSnapshot
{
	ChaperoneOrigin standing;	// follows recentering on the headset
	ChaperoneOrigin setup;		// "setup_standing2": fixed to the room, the bounds are relative to it
	double boundsBox[4] = {0, 0, 0, 0}; // min x, max x, min z, max z
};

// Writes frame_chaperone.vrchap next to the DLL (and an untouched per-universe copy) and returns
// its UTF-8 path, or an empty string if the file could not be written.
std::string SaveHeadsetChaperone(std::string json);

// What the last SaveHeadsetChaperone saw; used by the pose diagnostics.
ChaperoneSnapshot GetChaperoneSnapshot();

} // namespace shim

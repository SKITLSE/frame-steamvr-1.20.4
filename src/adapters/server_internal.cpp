#include "adapters/server_internal.h"

#include "core/log.h"
#include "core/openvr_abi.h"

#include <mutex>

namespace shim
{
namespace
{

// Feature masks. The driver sets 2 and 3 and reads 0 and 1, which in a current SteamVR are set
// by other components; here they stay zero, so the driver treats optional features as absent.
const uint32_t kFeatureMaskCount = 4;
std::mutex g_featureMutex;
uint64_t g_featureMasks[kFeatureMaskCount];

// bool SetFeatureMask(uint32 which, const uint64* value): true when the value changed.
bool SetFeatureMask(Proxy *, uint32_t which, const uint64_t *value)
{
	if (which >= kFeatureMaskCount || !value)
		return false;
	std::lock_guard<std::mutex> lock(g_featureMutex);
	bool changed = g_featureMasks[which] != *value;
	g_featureMasks[which] = *value;
	if (changed)
		Log("srv   SetFeatureMask[%u] = %llx", which, (unsigned long long)*value);
	return changed;
}

// uint64 GetFeatureMask(uint32 which), returned through a hidden pointer. 4..6 are intersections.
uint64_t *GetFeatureMask(Proxy *, uint64_t *out, int which)
{
	std::lock_guard<std::mutex> lock(g_featureMutex);
	const uint64_t *m = g_featureMasks;
	switch (which)
	{
	case 0: case 1: case 2: case 3:
		*out = m[which];
		break;
	case 4:
		*out = m[1] & m[0];
		break;
	case 5:
		*out = m[2] & m[0];
		break;
	case 6:
		*out = m[3] & m[1];
		break;
	default:
		*out = 0;
	}
	return out;
}

struct Float4
{
	float v[4];
};

// float[4] GetPerEyeScale(int eye), returned through a hidden pointer. The real server answers
// {1,1,1,1} whenever the compositor has not published anything, which is always the case here.
Float4 *GetPerEyeScale(Proxy *, Float4 *out, int eye)
{
	static volatile long calls = 0;
	if (InterlockedIncrement(&calls) <= 2)
		Log("srv   GetPerEyeScale(eye=%d) -> 1,1,1,1", eye);
	out->v[0] = out->v[1] = out->v[2] = out->v[3] = 1.0f;
	return out;
}

} // namespace

Proxy *MakeServerInternal(const char *requestedName)
{
	Proxy *proxy = MakeProxy(nullptr, requestedName, "(nothing)", 0);
	SetHook(proxy, abi::slot::kServerInternalSetFeatureMask, (void *)&SetFeatureMask);
	SetHook(proxy, abi::slot::kServerInternalGetFeatureMask, (void *)&GetFeatureMask);
	SetHook(proxy, abi::slot::kServerInternalGetPerEyeScale, (void *)&GetPerEyeScale);
	return proxy;
}

} // namespace shim

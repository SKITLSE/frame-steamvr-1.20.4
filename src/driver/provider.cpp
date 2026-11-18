#include "driver/provider.h"

#include "core/log.h"
#include "core/openvr_abi.h"
#include "core/proxy.h"
#include "driver/driver_context.h"

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace shim
{
namespace
{

// Component versions the driver declares, and what the legacy server is told instead.
// GetComponent in device.cpp maps the requests back onto the driver's versions.
struct VersionRename
{
	const char *drivers;
	const char *legacy;
};
const VersionRename kVersionRenames[] = {
	{"IVRDisplayComponent_003", "IVRDisplayComponent_002"},
	{"IVRDriverDirectModeComponent_009", "IVRDriverDirectModeComponent_007"},
};

int Init(Proxy *self, abi::IVRDriverContext *legacyContext)
{
	Log("prov  Init(ctx=%p)", legacyContext);
	int result = VCall<int, abi::IVRDriverContext *>(self->target, abi::slot::kProviderInit, WrapDriverContext(legacyContext));
	Log("prov  Init -> %d", result);
	return result;
}

// The legacy server asks each device for exactly the versions listed here and silently ignores
// versions it does not know, so the list has to name the ones it speaks.
const char *const *GetInterfaceVersions(Proxy *self)
{
	static std::mutex mutex;
	static std::vector<const char *> patched;
	std::lock_guard<std::mutex> lock(mutex);
	if (!patched.empty())
		return patched.data();

	const char *const *declared = VCall<const char *const *>(self->target, abi::slot::kProviderGetInterfaceVersions);
	std::string listing;
	for (int i = 0; declared && declared[i]; i++)
	{
		const char *version = declared[i];
		for (const VersionRename &rename : kVersionRenames)
			if (strcmp(version, rename.drivers) == 0)
				version = rename.legacy;
		patched.push_back(version);
		listing += version;
		listing += ' ';
	}
	patched.push_back(nullptr);
	Log("prov  GetInterfaceVersions -> %s", listing.c_str());
	return patched.data();
}

} // namespace

void *WrapProvider(void *realProvider, const char *name)
{
	int counted = CountSlots(realProvider);
	Proxy *proxy = MakeProxy(realProvider, name, name, counted < abi::slot::kProviderCount ? abi::slot::kProviderCount : counted);
	SetHook(proxy, abi::slot::kProviderInit, (void *)&Init);
	SetHook(proxy, abi::slot::kProviderGetInterfaceVersions, (void *)&GetInterfaceVersions);
	Log("factory provider wrapped (%d slots)", counted);
	return proxy;
}

} // namespace shim

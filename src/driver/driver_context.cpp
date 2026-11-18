#include "driver/driver_context.h"

#include "adapters/paths.h"
#include "adapters/properties.h"
#include "adapters/server_internal.h"
#include "core/log.h"
#include "core/proxy.h"
#include "driver/device.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

namespace shim
{
namespace
{

// Method counts of public interfaces as SteamVR 1.20 ships them. Used where the vtable scan
// would run on into an adjacent table.
int KnownSlotCount(const char *interfaceVersion, int scanned)
{
	if (strcmp(interfaceVersion, "IVRPaths_001") == 0)
		return abi::slot::kPathsCount;
	if (strcmp(interfaceVersion, "IVRDriverInput_003") == 0)
		return abi::slot::kDriverInput003Count;
	return scanned;
}

class ShimContext : public abi::IVRDriverContext
{
  public:
	explicit ShimContext(abi::IVRDriverContext *legacy) : m_legacy(legacy) {}

	void *GetGenericInterface(const char *name, int *error) override
	{
		if (!name)
			return m_legacy->GetGenericInterface(name, error);

		std::lock_guard<std::mutex> lock(m_mutex);
		auto cached = m_cache.find(name);
		void *result = cached != m_cache.end() ? cached->second : Resolve(name);
		m_cache[name] = result;
		if (error)
			*error = abi::kInitErrorNone;
		return result;
	}

	uint64_t GetDriverHandle() override
	{
		return m_legacy->GetDriverHandle();
	}

  private:
	// Never returns null: the driver dereferences most interfaces without checking.
	void *Resolve(const char *name)
	{
		int legacyError = 0;
		if (void *native = m_legacy->GetGenericInterface(name, &legacyError))
			return WrapNative(name, native);

		std::string base;
		int version = 0;
		bool versioned = SplitInterfaceVersion(name, base, version);

		if (versioned && base == "IVRPaths")
		{
			Log("iface %s MISSING -> shim path store", name);
			return MakePathStore(name);
		}
		if (versioned)
		{
			// the newest older version the legacy server does have
			for (int older = version - 1; older >= 1; older--)
			{
				char olderName[128];
				snprintf(olderName, sizeof(olderName), "%s_%03d", base.c_str(), older);
				int ignored = 0;
				void *object = m_legacy->GetGenericInterface(olderName, &ignored);
				if (!object)
					continue;
				int slots = KnownSlotCount(olderName, CountSlots(object));
				Log("iface %s MISSING -> proxy over %s %p (%d forwarded slots, rest stubbed)", name, olderName, object, slots);
				return MakeProxy(object, name, olderName, slots);
			}
		}
		Log("iface %s MISSING, no older version either -> all-stub proxy (err was %d)", name, legacyError);
		if (strcmp(name, "IVRServerInternal_XXX") == 0)
			return MakeServerInternal(name);
		return MakeProxy(nullptr, name, "(nothing)", 0);
	}

	// Interfaces both sides agree on. Two of them are wrapped to watch specific calls.
	void *WrapNative(const char *name, void *native)
	{
		if (strcmp(name, "IVRServerDriverHost_006") == 0)
		{
			Log("iface %s -> native %p, wrapped", name, native);
			return WrapServerDriverHost(native, name);
		}
		if (strcmp(name, "IVRProperties_001") == 0)
		{
			Log("iface %s -> native %p, wrapped for chaperone hand-off", name, native);
			return WrapProperties(native, name);
		}
		Log("iface %s -> native %p", name, native);
		return native;
	}

	abi::IVRDriverContext *m_legacy;
	std::mutex m_mutex;
	std::map<std::string, void *> m_cache;
};

} // namespace

abi::IVRDriverContext *WrapDriverContext(abi::IVRDriverContext *legacyContext)
{
	return new ShimContext(legacyContext); // lives as long as the driver stays loaded
}

} // namespace shim

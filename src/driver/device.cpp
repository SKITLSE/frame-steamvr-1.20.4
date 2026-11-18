#include "driver/device.h"

#include "adapters/chaperone.h"
#include "adapters/direct_mode.h"
#include "core/config.h"
#include "core/log.h"
#include "core/openvr_abi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

namespace shim
{
namespace
{

using GetComponentFn = void *(*)(void *device, const char *componentNameAndVersion);

struct HookedDevice
{
	GetComponentFn originalGetComponent = nullptr;
	std::string serial;
};

std::mutex g_devicesMutex;
std::map<void *, HookedDevice> g_devices;

const int kNewerVersionsToTry = 8;
const int kMaxDeviceVtableSlots = 512;

// ---- GetComponent ---------------------------------------------------------------------------------
// The legacy server asks for the versions it speaks. If the driver has that exact version, pass
// it through; otherwise look for a newer one and adapt it.
void *GetComponent(void *device, const char *name)
{
	HookedDevice hooked;
	{
		std::lock_guard<std::mutex> lock(g_devicesMutex);
		auto found = g_devices.find(device);
		if (found == g_devices.end())
			return nullptr;
		hooked = found->second;
	}
	const char *serial = hooked.serial.c_str();

	if (void *native = hooked.originalGetComponent(device, name))
	{
		Log("comp  [%s] %s -> native %p", serial, name, native);
		return native;
	}

	std::string base;
	int version = 0;
	if (SplitInterfaceVersion(name, base, version))
	{
		for (int newer = version + 1; newer <= version + kNewerVersionsToTry; newer++)
		{
			char newerName[128];
			snprintf(newerName, sizeof(newerName), "%s_%03d", base.c_str(), newer);
			void *component = hooked.originalGetComponent(device, newerName);
			if (!component)
				continue;
			// IVRDisplayComponent only ever appended methods, so the newer object answers older calls.
			if (base == "IVRDisplayComponent")
			{
				Log("comp  [%s] %s -> driver's %s %p (prefix-compatible)", serial, name, newerName, component);
				return component;
			}
			if (base == "IVRDriverDirectModeComponent" && version == 7 && newer == 9)
			{
				void *adapter = GetDirectMode007(component);
				Log("comp  [%s] %s -> adapter %p over driver's %s %p", serial, name, adapter, newerName, component);
				return adapter;
			}
			Log("comp  [%s] %s -> null (driver only has %s, no adapter yet)", serial, name, newerName);
			return nullptr;
		}
	}
	Log("comp  [%s] %s -> null", serial, name);
	return nullptr;
}

// Swaps the device's vtable for a copy whose GetComponent is ours. The device object stays the
// driver's own, so every other method runs natively. The copy carries the whole table: the
// driver's class appends its own virtuals after the six interface slots and calls them through
// the object.
void HookDevice(void *device, const char *serial)
{
	std::lock_guard<std::mutex> lock(g_devicesMutex);
	if (g_devices.count(device))
		return;
	void **vtable = *(void ***)device;
	int slots = abi::slot::kDeviceCount;
	while (slots < kMaxDeviceVtableSlots && IsReadable(&vtable[slots]))
		slots++;
	void **copy = (void **)malloc(sizeof(void *) * (slots + 1));
	copy[0] = vtable[-1]; // RTTI locator
	for (int i = 0; i < slots; i++)
		copy[1 + i] = vtable[i];

	HookedDevice hooked;
	hooked.originalGetComponent = (GetComponentFn)vtable[abi::slot::kDeviceGetComponent];
	hooked.serial = serial ? serial : "?";
	copy[1 + abi::slot::kDeviceGetComponent] = (void *)&GetComponent;
	g_devices[device] = hooked;
	*(void ***)device = copy + 1;
}

// ---- IVRServerDriverHost hooks --------------------------------------------------------------------
bool TrackedDeviceAdded(Proxy *self, const char *serial, int deviceClass, void *device)
{
	Log("host  TrackedDeviceAdded serial='%s' class=%d drv=%p", serial ? serial : "(null)", deviceClass, device);
	if (device)
		HookDevice(device, serial);
	bool added = VCall<bool, const char *, int, void *>(self->target, abi::slot::kHostTrackedDeviceAdded, serial, deviceClass, device);
	Log("host  TrackedDeviceAdded -> %d", (int)added);
	return added;
}

void Rotate(const double *q, const double v[3], double out[3]) // q is w, x, y, z
{
	double w = q[0], x = q[1], y = q[2], z = q[3];
	out[0] = (1 - 2 * (y * y + z * z)) * v[0] + 2 * (x * y - w * z) * v[1] + 2 * (x * z + w * y) * v[2];
	out[1] = 2 * (x * y + w * z) * v[0] + (1 - 2 * (x * x + z * z)) * v[1] + 2 * (y * z - w * x) * v[2];
	out[2] = 2 * (x * z - w * y) * v[0] + 2 * (y * z + w * x) * v[1] + (1 - 2 * (x * x + y * y)) * v[2];
}

// Diagnostic, installed only with pose_log=1: every two seconds, where the driver says the
// headset is in its own (raw) space, next to the chaperone origin and bounds in effect.
void TrackedDevicePoseUpdated(Proxy *self, uint32_t deviceIndex, const double *pose, uint32_t poseSize)
{
	const uint32_t kHmdIndex = 0;
	const LONGLONG kIntervalMs = 2000;
	static volatile LONGLONG lastLogged = 0;
	if (deviceIndex == kHmdIndex && pose && poseSize >= abi::kPoseSize)
	{
		LONGLONG now = (LONGLONG)GetTickCount64();
		LONGLONG previous = lastLogged;
		if (now - previous >= kIntervalMs && InterlockedCompareExchange64(&lastLogged, now, previous) == previous)
		{
			const double *worldRotation = pose + abi::kPoseWorldFromDriverRotation;
			const double *worldTranslation = pose + abi::kPoseWorldFromDriverTranslation;
			double position[3], forwardInDriver[3], forward[3];
			const double negativeZ[3] = {0, 0, -1};
			Rotate(worldRotation, pose + abi::kPosePosition, position);
			Rotate(pose + abi::kPoseRotation, negativeZ, forwardInDriver);
			Rotate(worldRotation, forwardInDriver, forward);
			const unsigned char *bytes = (const unsigned char *)pose;
			ChaperoneSnapshot chaperone = GetChaperoneSnapshot();
			const ChaperoneOrigin &origin = chaperone.setup.valid ? chaperone.setup : chaperone.standing;
			Log("pose  hmd raw=(%.2f %.2f %.2f) fwd=(%.2f %.2f %.2f) worldT=(%.2f %.2f %.2f) valid=%d connected=%d result=%d | "
				"origin t=(%.2f %.2f %.2f) yaw=%.3f | box x[%.2f..%.2f] z[%.2f..%.2f]",
				position[0] + worldTranslation[0], position[1] + worldTranslation[1], position[2] + worldTranslation[2], forward[0],
				forward[1], forward[2], worldTranslation[0], worldTranslation[1], worldTranslation[2],
				(int)bytes[abi::kPoseIsValidByteOffset], (int)bytes[abi::kPoseIsConnectedByteOffset],
				*(const int *)(bytes + abi::kPoseResultByteOffset), origin.translation[0], origin.translation[1], origin.translation[2],
				origin.yaw, chaperone.boundsBox[0], chaperone.boundsBox[1], chaperone.boundsBox[2], chaperone.boundsBox[3]);
		}
	}
	VCall<void, uint32_t, const double *, uint32_t>(self->target, abi::slot::kHostTrackedDevicePoseUpdated, deviceIndex, pose, poseSize);
}

} // namespace

Proxy *WrapServerDriverHost(void *realHost, const char *name)
{
	int counted = CountSlots(realHost);
	Proxy *proxy = MakeProxy(realHost, name, name, counted < abi::slot::kHostCount ? abi::slot::kHostCount : counted);
	SetHook(proxy, abi::slot::kHostTrackedDeviceAdded, (void *)&TrackedDeviceAdded);
	if (GetConfig().poseLog)
		SetHook(proxy, abi::slot::kHostTrackedDevicePoseUpdated, (void *)&TrackedDevicePoseUpdated);
	return proxy;
}

} // namespace shim

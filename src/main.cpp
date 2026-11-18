// Steam Frame support for SteamVR 1.20.4.
//
// This DLL is installed as the "vrlink" driver of a legacy SteamVR. It loads Valve's real Frame
// streaming driver (driver_vrlink.dll from a current SteamVR, kept next to it as
// driver_vrlink_real.dll) and sits between that driver and the legacy vrserver:
//
//   legacy vrserver  <->  this shim  <->  driver_vrlink_real.dll  <->  headset
//
//   driver/    what the legacy server sees: the provider, the driver context, the devices
//   adapters/  one module per thing the two sides disagree on
//   core/      logging, settings, the generic interface proxy, ABI declarations
#include "core/config.h"
#include "core/log.h"
#include "core/openvr_abi.h"
#include "driver/provider.h"

#include <map>
#include <mutex>
#include <string>

namespace
{

using FactoryFn = void *(*)(const char *interfaceName, int *returnCode);

const wchar_t kRealDriverName[] = L"driver_vrlink_real.dll";
const char kProviderInterface[] = "IServerTrackedDeviceProvider_004";

std::mutex g_mutex;
FactoryFn g_realFactory = nullptr;
std::map<std::string, void *> g_factoryResults;

bool LoadRealDriverLocked()
{
	if (g_realFactory)
		return true;
	shim::LoadConfig();
	const shim::Config &config = shim::GetConfig();
	shim::Log("config predict_ms=%d recenter=%d pose_log=%d", config.predictMs, (int)config.recenter, (int)config.poseLog);

	wchar_t path[MAX_PATH];
	swprintf(path, MAX_PATH, L"%s\\%s", shim::ModuleDir(), kRealDriverName);
	HMODULE module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!module)
	{
		shim::Log("FATAL cannot load %ls, error %lu", kRealDriverName, GetLastError());
		return false;
	}
	g_realFactory = (FactoryFn)GetProcAddress(module, "HmdDriverFactory");
	shim::Log("real driver loaded at %p, factory %p", module, g_realFactory);
	return g_realFactory != nullptr;
}

} // namespace

// The one entry point SteamVR looks for in a driver DLL.
extern "C" __declspec(dllexport) void *HmdDriverFactory(const char *interfaceName, int *returnCode)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!LoadRealDriverLocked())
	{
		if (returnCode)
			*returnCode = shim::abi::kInitErrorInterfaceNotFound;
		return nullptr;
	}
	std::string key = interfaceName ? interfaceName : "";
	auto cached = g_factoryResults.find(key);
	if (cached != g_factoryResults.end())
		return cached->second;

	void *object = g_realFactory(interfaceName, returnCode);
	shim::Log("factory %s -> %p (rc=%d)", key.c_str(), object, returnCode ? *returnCode : -1);
	if (object && key == kProviderInterface)
		object = shim::WrapProvider(object, kProviderInterface);
	if (object)
		g_factoryResults[key] = object;
	return object;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(module);
		shim::InitModule(module);
	}
	return TRUE;
}

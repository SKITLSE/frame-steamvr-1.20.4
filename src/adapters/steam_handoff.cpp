#include "adapters/steam_handoff.h"

#include "core/log.h"

#include <objbase.h>
#include <shellapi.h>

namespace shim
{
namespace
{

// Same URL the 2.x server falls back to when Steam's private SteamVR interface is unavailable.
const char kUrlPrefix[] = "steam://vr_connection_ready/";

// Starts steam.exe with the URL the way the steam:// protocol handler does: "steam.exe" -- "url".
bool LaunchSteamWithUrl(const std::string &url)
{
	wchar_t exe[MAX_PATH] = L"";
	DWORD size = sizeof(exe);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamExe", RRF_RT_REG_SZ, nullptr, exe, &size) != ERROR_SUCCESS ||
		!exe[0])
		return false;
	for (wchar_t *c = exe; *c; c++)
		if (*c == L'/')
			*c = L'\\';
	std::wstring command = L"\"";
	command += exe;
	command += L"\" -- \"";
	command.append(url.begin(), url.end());
	command += L"\"";
	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process = {};
	BOOL started = CreateProcessW(exe, &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
	SecureZeroMemory(&command[0], command.size() * sizeof(wchar_t));
	if (!started)
		return false;
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return true;
}

DWORD WINAPI SendThread(LPVOID parameter)
{
	std::string *url = (std::string *)parameter;
	size_t paramChars = url->size() - (sizeof(kUrlPrefix) - 1);

	// ShellExecute is the route verified with a headset actually connecting; starting steam.exe
	// directly is the fallback.
	HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	HINSTANCE result = ShellExecuteA(nullptr, "open", url->c_str(), nullptr, nullptr, SW_SHOWNOACTIVATE);
	if (SUCCEEDED(com))
		CoUninitialize();

	long long code = (long long)(intptr_t)result;
	if (code > 32)
		Log("steam vr_connection_ready handed to Steam (%zu chars), ShellExecute -> %lld", paramChars, code);
	else if (LaunchSteamWithUrl(*url))
		Log("steam vr_connection_ready handed to Steam (%zu chars) via steam.exe (ShellExecute gave %lld)", paramChars, code);
	else
		Log("steam vr_connection_ready could NOT be handed to Steam (ShellExecute gave %lld)", code);

	SecureZeroMemory(&(*url)[0], url->size());
	delete url;
	return 0;
}

} // namespace

void SendVrConnectionReady(const std::string &connectParams)
{
	std::string *url = new std::string(kUrlPrefix + connectParams);
	HANDLE thread = CreateThread(nullptr, 0, SendThread, url, 0, nullptr);
	if (thread)
		CloseHandle(thread);
	else
		SendThread(url);
}

} // namespace shim

#include "core/log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <share.h>

namespace shim
{
namespace
{

const DWORD kMaxLogBytes = 4u << 20; // past this the log is rotated to vrlink_shim.previous.log

std::mutex g_mutex;
FILE *g_file = nullptr;
wchar_t g_dir[MAX_PATH];
char g_host[64] = "?";

FILE *OpenLogLocked()
{
	wchar_t path[MAX_PATH];
	swprintf(path, MAX_PATH, L"%s\\vrlink_shim.log", g_dir);
	WIN32_FILE_ATTRIBUTE_DATA info;
	if (GetFileAttributesExW(path, GetFileExInfoStandard, &info) && (info.nFileSizeHigh || info.nFileSizeLow > kMaxLogBytes))
	{
		wchar_t previous[MAX_PATH];
		swprintf(previous, MAX_PATH, L"%s\\vrlink_shim.previous.log", g_dir);
		MoveFileExW(path, previous, MOVEFILE_REPLACE_EXISTING);
	}
	// vrserver, vrcompositor and steam.exe can all have the DLL loaded, so share the file
	return _wfsopen(path, L"a", _SH_DENYNO);
}

} // namespace

void InitModule(HINSTANCE module)
{
	GetModuleFileNameW(module, g_dir, MAX_PATH);
	if (wchar_t *slash = wcsrchr(g_dir, L'\\'))
		*slash = 0;
	wchar_t exe[MAX_PATH];
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	const wchar_t *name = wcsrchr(exe, L'\\');
	snprintf(g_host, sizeof(g_host), "%ls", name ? name + 1 : exe);
}

const wchar_t *ModuleDir()
{
	return g_dir;
}

bool ShouldLog(long occurrence)
{
	return occurrence <= 3 || occurrence == 100 || occurrence == 10000;
}

void Log(const char *fmt, ...)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!g_file && !(g_file = OpenLogLocked()))
		return;
	SYSTEMTIME now;
	GetLocalTime(&now);
	fprintf(g_file, "%02d:%02d:%02d.%03d %s[%lu] ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, g_host,
			GetCurrentThreadId());
	va_list args;
	va_start(args, fmt);
	vfprintf(g_file, fmt, args);
	va_end(args);
	fputc('\n', g_file);
	fflush(g_file);
}

} // namespace shim

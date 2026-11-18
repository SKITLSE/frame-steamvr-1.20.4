// Logging to vrlink_shim.log next to the DLL, plus where the DLL lives.
#pragma once

#include <windows.h>

namespace shim
{

// Remembers the directory of this DLL and the name of the process that loaded it.
void InitModule(HINSTANCE module);

// Directory of this DLL, without a trailing backslash.
const wchar_t *ModuleDir();

// printf-style line, prefixed with time, host process and thread id.
void Log(const char *fmt, ...);

// Rate limit for events that repeat every frame: the first three, the 100th and the 10000th.
bool ShouldLog(long occurrence);

} // namespace shim

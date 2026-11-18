#include "core/config.h"

#include "core/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace shim
{
namespace
{

Config g_config;

const char kDefaultIni[] =
	"# vrlink shim settings. Read once when SteamVR starts, so restart SteamVR after editing.\n"
	"\n"
	"# Milliseconds ahead the frame's head pose is assumed to be predicted (0..100).\n"
	"# Raise or lower in steps of 5 if the picture swims or lags when you turn your head.\n"
	"predict_ms=22\n"
	"\n"
	"# 0: bounds and centre stay where the play area was set up on the headset (verified).\n"
	"# 1: recentering on the headset moves the centre, bounds stay in the room (experimental).\n"
	"recenter=0\n"
	"\n"
	"# 1: write the headset position to vrlink_shim.log every two seconds (diagnostics).\n"
	"pose_log=0\n";

int Clamp(int value, int low, int high)
{
	return value < low ? low : (value > high ? high : value);
}

} // namespace

const Config &GetConfig()
{
	return g_config;
}

void LoadConfig()
{
	wchar_t path[MAX_PATH];
	swprintf(path, MAX_PATH, L"%s\\vrlink_shim.ini", ModuleDir());
	FILE *file = _wfopen(path, L"r");
	if (!file)
	{
		if ((file = _wfopen(path, L"w")) != nullptr)
		{
			fputs(kDefaultIni, file);
			fclose(file);
		}
		return;
	}
	char line[256];
	while (fgets(line, sizeof(line), file))
	{
		if (line[0] == '#' || line[0] == ';')
			continue;
		char *equals = strchr(line, '=');
		if (!equals)
			continue;
		*equals = 0;
		std::string key(line);
		while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
			key.pop_back();
		int value = atoi(equals + 1);
		if (key == "predict_ms")
			g_config.predictMs = Clamp(value, 0, 100);
		else if (key == "recenter")
			g_config.recenter = value != 0;
		else if (key == "pose_log")
			g_config.poseLog = value != 0;
	}
	fclose(file);
}

} // namespace shim

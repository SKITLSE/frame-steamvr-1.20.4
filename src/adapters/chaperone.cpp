#include "adapters/chaperone.h"

#include "core/config.h"
#include "core/log.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace shim
{
namespace
{

const int kLegacyChaperoneVersion = 5; // newest .vrchap version the 1.20 parser accepts
const double kOriginEpsilon = 1e-4;

std::mutex g_mutex;
ChaperoneSnapshot g_snapshot;

// ---- minimal JSON picking: the document is machine-written and small, so plain scanning does --
// Finds the bracketed value that follows a quoted key; [begin, end) covers the brackets.
bool FindValue(const std::string &json, const char *quotedKey, char open, char close, size_t &begin, size_t &end)
{
	size_t key = json.find(quotedKey);
	if (key == std::string::npos)
		return false;
	size_t start = json.find(open, key + strlen(quotedKey));
	if (start == std::string::npos)
		return false;
	int depth = 0;
	for (size_t i = start; i < json.size(); i++)
	{
		if (json[i] == open)
			depth++;
		else if (json[i] == close && --depth == 0)
		{
			begin = start;
			end = i + 1;
			return true;
		}
	}
	return false;
}

std::vector<double> NumbersIn(const std::string &json, size_t begin, size_t end)
{
	std::vector<double> numbers;
	const char *p = json.c_str() + begin;
	const char *stop = json.c_str() + end;
	while (p < stop)
	{
		if ((*p >= '0' && *p <= '9') || *p == '-')
		{
			char *next = nullptr;
			double value = strtod(p, &next);
			if (next == p) // a stray '-' that starts no number
			{
				p++;
				continue;
			}
			numbers.push_back(value);
			p = next;
		}
		else
			p++;
	}
	return numbers;
}

ChaperoneOrigin ParseOrigin(const std::string &object)
{
	ChaperoneOrigin origin;
	size_t begin, end;
	if (!FindValue(object, "\"translation\"", '[', ']', begin, end))
		return origin;
	std::vector<double> translation = NumbersIn(object, begin, end);
	if (translation.size() != 3)
		return origin;
	for (int i = 0; i < 3; i++)
		origin.translation[i] = translation[i];
	size_t yaw = object.find("\"yaw\"");
	size_t colon = yaw == std::string::npos ? yaw : object.find(':', yaw);
	if (colon == std::string::npos)
		return origin;
	origin.yaw = strtod(object.c_str() + colon + 1, nullptr);
	origin.valid = true;
	return origin;
}

bool OriginsDiffer(const ChaperoneOrigin &a, const ChaperoneOrigin &b)
{
	if (!a.valid || !b.valid)
		return false;
	for (int i = 0; i < 3; i++)
		if (fabs(a.translation[i] - b.translation[i]) > kOriginEpsilon)
			return true;
	return fabs(a.yaw - b.yaw) > kOriginEpsilon;
}

void BoundsBox(const std::vector<double> &bounds, double box[4])
{
	box[0] = box[2] = 1e9;
	box[1] = box[3] = -1e9;
	for (size_t i = 0; i + 2 < bounds.size(); i += 3)
	{
		if (bounds[i] < box[0]) box[0] = bounds[i];
		if (bounds[i] > box[1]) box[1] = bounds[i];
		if (bounds[i + 2] < box[2]) box[2] = bounds[i + 2];
		if (bounds[i + 2] > box[3]) box[3] = bounds[i + 2];
	}
}

std::string UniverseId(const std::string &json)
{
	size_t key = json.find("\"universeID\"");
	if (key == std::string::npos)
		return "unknown";
	size_t digits = json.find_first_of("0123456789", key + 12);
	if (digits == std::string::npos)
		return "unknown";
	return json.substr(digits, json.find_first_not_of("0123456789", digits) - digits);
}

bool WriteFile(const wchar_t *path, const std::string &content)
{
	FILE *file = _wfopen(path, L"wb");
	if (!file)
		return false;
	fwrite(content.data(), 1, content.size(), file);
	fclose(file);
	return true;
}

// ---- steps ---------------------------------------------------------------------------------------
// Rewrites collision_bounds from the setup origin's frame into the recentered origin's frame:
// b' = R(yaw_s - yaw_u) * (b - t_u) + t_s. Returns false if the bounds are not whole quads.
bool MoveBoundsToRecenteredFrame(std::string &json, const ChaperoneOrigin &setup, const ChaperoneOrigin &standing)
{
	size_t begin, end;
	if (!FindValue(json, "\"collision_bounds\"", '[', ']', begin, end))
		return false;
	std::vector<double> n = NumbersIn(json, begin, end);
	const size_t kNumbersPerQuad = 12; // four corners, three coordinates each
	if (n.empty() || n.size() % kNumbersPerQuad != 0)
		return false;
	double angle = standing.yaw - setup.yaw, c = cos(angle), s = sin(angle);
	std::string out = "[";
	for (size_t i = 0; i < n.size(); i += 3)
	{
		double x = n[i] - setup.translation[0];
		double y = n[i + 1] - setup.translation[1] + standing.translation[1];
		double z = n[i + 2] - setup.translation[2];
		char corner[96];
		snprintf(corner, sizeof(corner), "[%.6f,%.6f,%.6f]", x * c + z * s + standing.translation[0], y,
				 -x * s + z * c + standing.translation[2]);
		if (i % kNumbersPerQuad == 0)
			out += i ? ",[" : "[";
		else
			out += ",";
		out += corner;
		if (i % kNumbersPerQuad == kNumbersPerQuad - 3)
			out += "]";
	}
	out += "]";
	json.replace(begin, end - begin, out);
	return true;
}

// A current SteamVR keeps two origins: "standing" follows recentering on the headset, while
// "setup_standing2" stays fixed to the room and is what the bounds are drawn against. The legacy
// server has one origin for both, so left alone the bounds land wherever the last recenter put
// the standing origin. Default: give the legacy server the room-fixed origin.
void ReconcileOrigins(std::string &json)
{
	size_t standingBegin, standingEnd, setupBegin, setupEnd, boundsBegin, boundsEnd;
	ChaperoneSnapshot snapshot;
	if (FindValue(json, "\"standing\"", '{', '}', standingBegin, standingEnd))
		snapshot.standing = ParseOrigin(json.substr(standingBegin, standingEnd - standingBegin));
	if (FindValue(json, "\"setup_standing2\"", '{', '}', setupBegin, setupEnd))
		snapshot.setup = ParseOrigin(json.substr(setupBegin, setupEnd - setupBegin));
	if (FindValue(json, "\"collision_bounds\"", '[', ']', boundsBegin, boundsEnd))
		BoundsBox(NumbersIn(json, boundsBegin, boundsEnd), snapshot.boundsBox);

	const ChaperoneOrigin &st = snapshot.standing, &su = snapshot.setup;
	Log("chap  standing: t=(%.3f %.3f %.3f) yaw=%.4f ok=%d | setup_standing2: t=(%.3f %.3f %.3f) yaw=%.4f ok=%d", st.translation[0],
		st.translation[1], st.translation[2], st.yaw, (int)st.valid, su.translation[0], su.translation[1], su.translation[2], su.yaw,
		(int)su.valid);
	Log("chap  bounds box: x[%.2f .. %.2f] z[%.2f .. %.2f]", snapshot.boundsBox[0], snapshot.boundsBox[1], snapshot.boundsBox[2],
		snapshot.boundsBox[3]);

	if (OriginsDiffer(st, su))
	{
		if (!GetConfig().recenter)
		{
			json.replace(standingBegin, standingEnd - standingBegin, json.substr(setupBegin, setupEnd - setupBegin));
			Log("chap  origins differ: legacy server gets setup_standing2 as its standing origin");
		}
		else if (MoveBoundsToRecenteredFrame(json, su, st))
			Log("chap  origins differ: recenter mode, bounds moved into the recentered frame");
		else
			Log("chap  recenter mode: could not read the bounds, leaving the file as sent");
	}

	std::lock_guard<std::mutex> lock(g_mutex);
	g_snapshot = snapshot;
}

// The legacy parser insists on jsonid == chaperone_info and a version it knows.
void NormalizeForLegacyParser(std::string &json, int &versionSeen, bool &hadJsonId)
{
	versionSeen = -1;
	size_t key = json.find("\"version\"");
	if (key != std::string::npos)
	{
		size_t digits = json.find_first_of("0123456789", key + 9);
		if (digits != std::string::npos)
		{
			size_t stop = json.find_first_not_of("0123456789", digits);
			versionSeen = atoi(json.substr(digits, stop - digits).c_str());
			if (versionSeen > kLegacyChaperoneVersion)
				json.replace(digits, stop - digits, std::to_string(kLegacyChaperoneVersion));
		}
	}
	hadJsonId = json.find("\"jsonid\"") != std::string::npos;
	size_t brace = json.find('{');
	if (brace == std::string::npos)
		return;
	if (!hadJsonId)
		json.insert(brace + 1, "\"jsonid\":\"chaperone_info\",");
	if (key == std::string::npos)
		json.insert(brace + 1, "\"version\":" + std::to_string(kLegacyChaperoneVersion) + ",");
}

} // namespace

ChaperoneSnapshot GetChaperoneSnapshot()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_snapshot;
}

std::string SaveHeadsetChaperone(std::string json)
{
	// an untouched copy per universe, for inspection
	wchar_t rawPath[MAX_PATH];
	swprintf(rawPath, MAX_PATH, L"%s\\frame_chaperone_raw_%hs.json", ModuleDir(), UniverseId(json).c_str());
	WriteFile(rawPath, json);

	ReconcileOrigins(json);
	int versionSeen = -1;
	bool hadJsonId = false;
	NormalizeForLegacyParser(json, versionSeen, hadJsonId);

	wchar_t path[MAX_PATH];
	swprintf(path, MAX_PATH, L"%s\\frame_chaperone.vrchap", ModuleDir());
	if (!WriteFile(path, json))
	{
		Log("chap  cannot write chaperone file, error %d", errno);
		return std::string();
	}
	Log("chap  headset chaperone saved: %zu bytes, version %d%s, jsonid %s, bounds %s, standing %s", json.size(), versionSeen,
		versionSeen > kLegacyChaperoneVersion ? " (rewritten to 5)" : "", hadJsonId ? "present" : "added",
		json.find("\"collision_bounds\"") != std::string::npos ? "yes" : "NO",
		json.find("\"standing\"") != std::string::npos ? "yes" : "NO");

	char utf8[MAX_PATH * 3];
	WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
	return utf8;
}

} // namespace shim

#include "adapters/paths.h"

#include "adapters/steam_handoff.h"
#include "core/log.h"
#include "core/openvr_abi.h"

#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace shim
{
namespace
{

const char kIsRunningPath[] = "/steam/is_running";
const char kConnectionReadyPath[] = "/steam/vr_connection_ready";
const uint64_t kFirstHandle = 0x5348000001ULL; // arbitrary, just recognisable in logs
const long kMaxLoggedWrites = 200;

struct Value
{
	uint32_t tag = 0;
	std::string bytes;
};

std::mutex g_mutex;
std::map<std::string, uint64_t> g_handles;
std::map<uint64_t, std::string> g_names;
std::map<std::pair<uint64_t, uint64_t>, Value> g_values; // (root container, path handle) -> value

uint64_t HandleForLocked(const char *path)
{
	auto found = g_handles.find(path);
	if (found != g_handles.end())
		return found->second;
	uint64_t handle = kFirstHandle + g_handles.size();
	g_handles[path] = handle;
	g_names[handle] = path;
	return handle;
}

template <typename Entry>
uint64_t EntryHandleLocked(const Entry &entry)
{
	if (entry.path)
		return entry.path;
	return entry.pathString ? HandleForLocked(entry.pathString) : 0;
}

void SeedDefaults()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!g_values.empty())
		return;
	Value &running = g_values[{abi::kSteamPathRoot, HandleForLocked(kIsRunningPath)}];
	running.tag = abi::kTagBool;
	running.bytes.assign(1, '\x01');
}

// ---- IVRPaths slots ------------------------------------------------------------------------------
int ReadPathBatch(Proxy *, uint64_t root, abi::PathRead *batch, uint32_t count)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	int result = abi::kPropSuccess;
	for (uint32_t i = 0; i < count; i++)
	{
		abi::PathRead &read = batch[i];
		auto found = g_values.find({root, EntryHandleLocked(read)});
		if (found == g_values.end())
		{
			read.tag = 0;
			read.requiredBufferSize = 0;
			read.error = result = abi::kPropUnknownProperty;
			continue;
		}
		const Value &value = found->second;
		read.tag = value.tag;
		read.requiredBufferSize = (uint32_t)value.bytes.size();
		if (read.buffer && read.bufferSize >= value.bytes.size())
		{
			memcpy(read.buffer, value.bytes.data(), value.bytes.size());
			read.error = abi::kPropSuccess;
		}
		else
			read.error = result = abi::kPropBufferTooSmall;
	}
	return result;
}

int WritePathBatch(Proxy *, uint64_t root, abi::PathWrite *batch, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++)
	{
		abi::PathWrite &write = batch[i];
		std::string name;
		std::string connectParams;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			uint64_t handle = EntryHandleLocked(write);
			auto known = g_names.find(handle);
			name = known != g_names.end() ? known->second : "?";
			if (write.writeType == abi::kWriteSet && write.buffer)
			{
				Value &value = g_values[{root, handle}];
				value.tag = write.tag;
				value.bytes.assign((const char *)write.buffer, write.bufferSize);
				if (name == kConnectionReadyPath && write.tag == abi::kTagString && write.bufferSize > 1)
					connectParams.assign((const char *)write.buffer, strnlen((const char *)write.buffer, write.bufferSize));
			}
			else
				g_values.erase({root, handle});
			write.error = abi::kPropSuccess;
		}

		if (name == kConnectionReadyPath)
			Log("paths write %s type=%d tag=%u size=%u (value not logged)", name.c_str(), write.writeType, write.tag, write.bufferSize);
		else
		{
			static volatile long logged = 0;
			if (InterlockedIncrement(&logged) <= kMaxLoggedWrites)
			{
				char peek[260] = "";
				if (write.bufferSize <= 64)
					DescribeArg(peek, sizeof(peek), (uint64_t)write.buffer);
				Log("paths write %s root=%llx type=%d tag=%u size=%u %s", name.c_str(), (unsigned long long)root, write.writeType,
					write.tag, write.bufferSize, peek);
			}
		}

		if (!connectParams.empty())
		{
			SendVrConnectionReady(connectParams);
			SecureZeroMemory(&connectParams[0], connectParams.size());
		}
	}
	return abi::kPropSuccess;
}

int StringToHandle(Proxy *, uint64_t *handle, const char *path)
{
	if (!handle || !path)
		return abi::kPropUnknownProperty;
	std::lock_guard<std::mutex> lock(g_mutex);
	bool isNew = g_handles.find(path) == g_handles.end();
	*handle = HandleForLocked(path);
	if (isNew)
		Log("paths handle %s -> %llx", path, (unsigned long long)*handle);
	return abi::kPropSuccess;
}

int HandleToString(Proxy *, uint64_t handle, char *buffer, uint32_t capacity, uint32_t *used)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	auto found = g_names.find(handle);
	if (found == g_names.end())
		return abi::kPropUnknownProperty;
	const std::string &name = found->second;
	if (used)
		*used = (uint32_t)name.size() + 1;
	if (!buffer || capacity < name.size() + 1)
		return abi::kPropBufferTooSmall;
	memcpy(buffer, name.c_str(), name.size() + 1);
	return abi::kPropSuccess;
}

} // namespace

Proxy *MakePathStore(const char *requestedName)
{
	SeedDefaults();
	Proxy *proxy = MakeProxy(nullptr, requestedName, "(shim path store)", 0);
	SetHook(proxy, abi::slot::kPathsReadBatch, (void *)&ReadPathBatch);
	SetHook(proxy, abi::slot::kPathsWriteBatch, (void *)&WritePathBatch);
	SetHook(proxy, abi::slot::kPathsStringToHandle, (void *)&StringToHandle);
	SetHook(proxy, abi::slot::kPathsHandleToString, (void *)&HandleToString);
	return proxy;
}

} // namespace shim

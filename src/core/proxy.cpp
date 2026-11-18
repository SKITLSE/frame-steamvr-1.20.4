#include "core/proxy.h"

#include "core/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <utility>

namespace shim
{
namespace
{

// ---- stubs: one function per slot so the log can say which slot was hit ------------------------
template <int Slot>
uint64_t Stub(Proxy *self, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4)
{
	long count = InterlockedIncrement(&self->calls[Slot]);
	if (ShouldLog(count))
	{
		char text[4][260];
		DescribeArg(text[0], sizeof(text[0]), a1);
		DescribeArg(text[1], sizeof(text[1]), a2);
		DescribeArg(text[2], sizeof(text[2]), a3);
		DescribeArg(text[3], sizeof(text[3]), a4);
		Log("STUB  %s[%d] (n=%ld) args: %s | %s | %s | %s", self->name, Slot, count, text[0], text[1], text[2], text[3]);
	}
	return 0;
}

void *g_stubs[kMaxSlots];

template <size_t... Slot>
void FillStubs(std::index_sequence<Slot...>)
{
	((g_stubs[Slot] = (void *)&Stub<(int)Slot>), ...);
}

// ---- forwarding thunks --------------------------------------------------------------------------
void LogForwardedCall(Proxy *proxy, int slot)
{
	long count = InterlockedIncrement(&proxy->calls[slot]);
	if (ShouldLog(count))
		Log("call  %s[%d] -> %s (n=%ld)", proxy->name, slot, proxy->targetName, count);
}

std::mutex g_codeMutex;
uint8_t *g_code = nullptr;
size_t g_codeUsed = 0;
const size_t kCodeSize = 1 << 18;
void *g_forwardThunks[kMaxSlots];

void *EmitLocked(const uint8_t *bytes, size_t size)
{
	if (!g_code)
		g_code = (uint8_t *)VirtualAlloc(nullptr, kCodeSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!g_code || g_codeUsed + size + 16 > kCodeSize)
		return nullptr;
	uint8_t *at = g_code + g_codeUsed;
	memcpy(at, bytes, size);
	g_codeUsed += (size + 15) & ~size_t(15);
	FlushInstructionCache(GetCurrentProcess(), at, size);
	return at;
}

// Saves the argument registers, calls LogForwardedCall(proxy, slot), restores them, swaps rcx
// from the proxy to the real object and tail-jumps to that object's vtable[slot]. The thunk
// depends only on the slot, so one is shared by every proxy.
void *ForwardThunk(int slot)
{
	std::lock_guard<std::mutex> lock(g_codeMutex);
	if (g_forwardThunks[slot])
		return g_forwardThunks[slot];
	uint8_t code[128];
	size_t size = 0;
	auto put = [&](std::initializer_list<uint8_t> bytes) {
		for (uint8_t b : bytes)
			code[size++] = b;
	};
	put({0x51, 0x52, 0x41, 0x50, 0x41, 0x51}); // push rcx, rdx, r8, r9
	put({0x48, 0x83, 0xEC, 0x68});			   // sub rsp, 68h
	put({0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20}); // movdqu [rsp+20h], xmm0
	put({0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30}); // movdqu [rsp+30h], xmm1
	put({0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40}); // movdqu [rsp+40h], xmm2
	put({0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50}); // movdqu [rsp+50h], xmm3
	put({0xBA});							   // mov edx, slot
	memcpy(code + size, &slot, 4);
	size += 4;
	put({0x48, 0xB8}); // mov rax, &LogForwardedCall
	void *logger = (void *)&LogForwardedCall;
	memcpy(code + size, &logger, 8);
	size += 8;
	put({0xFF, 0xD0});						   // call rax
	put({0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20}); // movdqu xmm0, [rsp+20h]
	put({0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30}); // movdqu xmm1, [rsp+30h]
	put({0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40}); // movdqu xmm2, [rsp+40h]
	put({0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50}); // movdqu xmm3, [rsp+50h]
	put({0x48, 0x83, 0xC4, 0x68});			   // add rsp, 68h
	put({0x41, 0x59, 0x41, 0x58, 0x5A, 0x59}); // pop r9, r8, rdx, rcx
	put({0x48, 0x8B, 0x49, 0x08});			   // mov rcx, [rcx+8]   ; Proxy::target
	put({0x48, 0x8B, 0x01});				   // mov rax, [rcx]     ; its vtable
	put({0xFF, 0xA0});						   // jmp [rax + slot*8]
	int32_t displacement = slot * 8;
	memcpy(code + size, &displacement, 4);
	size += 4;
	g_forwardThunks[slot] = EmitLocked(code, size);
	return g_forwardThunks[slot];
}

bool IsExecutable(const void *address)
{
	MEMORY_BASIC_INFORMATION info;
	if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT)
		return false;
	return (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

} // namespace

bool IsReadable(const void *address)
{
	MEMORY_BASIC_INFORMATION info;
	if (!VirtualQuery(address, &info, sizeof(info)))
		return false;
	return info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

int CountSlots(void *object)
{
	void **vtable = *(void ***)object;
	int count = 0;
	while (count < kMaxSlots && IsReadable(&vtable[count]) && IsExecutable(vtable[count]))
		count++;
	return count;
}

Proxy *MakeProxy(void *target, const char *name, const char *targetName, int forwardedSlots)
{
	static std::once_flag stubsReady;
	std::call_once(stubsReady, [] { FillStubs(std::make_index_sequence<kMaxSlots>{}); });

	Proxy *proxy = (Proxy *)calloc(1, sizeof(Proxy));
	proxy->target = target;
	proxy->name = _strdup(name);
	proxy->targetName = _strdup(targetName);
	proxy->forwardedSlots = forwardedSlots;
	for (int slot = 0; slot < kMaxSlots; slot++)
		proxy->vtable[1 + slot] = (target && slot < forwardedSlots) ? ForwardThunk(slot) : g_stubs[slot];
	proxy->vptr = &proxy->vtable[1];
	return proxy;
}

void DescribeArg(char *out, size_t capacity, uint64_t value)
{
	uint8_t bytes[64];
	SIZE_T got = 0;
	bool looksLikePointer = value >= 0x10000 && (value >> 47) == 0;
	if (!looksLikePointer || !ReadProcessMemory(GetCurrentProcess(), (void *)value, bytes, sizeof(bytes), &got) || got < 8)
	{
		snprintf(out, capacity, "%llx", (unsigned long long)value);
		return;
	}
	char hex[3 * 32 + 1] = "";
	char ascii[65] = "";
	size_t hexCount = got < 32 ? got : 32;
	for (size_t i = 0; i < hexCount; i++)
		snprintf(hex + 3 * i, 4, "%02x ", bytes[i]);
	for (size_t i = 0; i < got; i++)
		ascii[i] = (bytes[i] >= 32 && bytes[i] < 127) ? (char)bytes[i] : '.';
	ascii[got] = 0;
	snprintf(out, capacity, "%llx->[%s|%s]", (unsigned long long)value, hex, ascii);
}

bool SplitInterfaceVersion(const char *name, std::string &base, int &version)
{
	const char *underscore = strrchr(name, '_');
	if (!underscore || strlen(underscore + 1) != 3)
		return false;
	base.assign(name, underscore - name);
	version = atoi(underscore + 1);
	return version > 0;
}

} // namespace shim

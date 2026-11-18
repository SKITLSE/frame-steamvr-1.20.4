// Generic stand-in for a COM-style interface object (a pointer to a vtable).
//
// A Proxy is handed out wherever one side asks for an interface the other side does not have
// in that version. Per slot it does one of three things:
//   forward - tail-jump to the same slot of a real object (after logging the call),
//   hook    - call a function of ours that receives the Proxy as `this`,
//   stub    - log the first four arguments and return 0.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace shim
{

enum { kMaxSlots = 160 };

struct Proxy
{
	void **vptr;			// what callers see as the object's vtable pointer
	void *target;			// real object the forwarded slots go to; null for an all-stub proxy
	const char *name;		// interface version that was asked for
	const char *targetName; // interface version that actually answers
	int forwardedSlots;
	volatile long calls[kMaxSlots];
	void *vtable[kMaxSlots + 1]; // [0] stands in for the RTTI slot that precedes a vtable
};

// The forwarding thunks are machine code that reads `target` at this offset.
static_assert(offsetof(Proxy, target) == 8, "forwarding thunks expect Proxy::target at +8");

// Slots [0, forwardedSlots) forward to `target`, the rest are stubs. Proxies are never freed.
Proxy *MakeProxy(void *target, const char *name, const char *targetName, int forwardedSlots);

// Replaces one slot with a function taking the Proxy as its first argument.
inline void SetHook(Proxy *proxy, int slot, void *function)
{
	proxy->vtable[1 + slot] = function;
}

// Number of leading vtable entries that point at code. A vtable is followed by data (the next
// table's RTTI locator), so this is the interface's method count unless tables sit back to back.
int CountSlots(void *object);

// Calls slot `slot` of a real object with the given arguments.
template <typename Result, typename... Args>
Result VCall(void *object, int slot, Args... args)
{
	auto function = (Result (*)(void *, Args...))(*(void ***)object)[slot];
	return function(object, args...);
}

bool IsReadable(const void *address);

// Text for one raw argument: the value, plus a peek at the memory if it is a readable pointer.
void DescribeArg(char *out, size_t capacity, uint64_t value);

// "IVRPaths_002" -> ("IVRPaths", 2). False if the name does not end in _NNN.
bool SplitInterfaceVersion(const char *name, std::string &base, int &version);

} // namespace shim

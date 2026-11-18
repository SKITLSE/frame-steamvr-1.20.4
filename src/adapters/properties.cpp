#include "adapters/properties.h"

#include "adapters/chaperone.h"
#include "core/log.h"
#include "core/openvr_abi.h"

#include <cstring>
#include <string>

namespace shim
{
namespace
{

bool IsChaperoneJsonWrite(const abi::PropertyWrite &write)
{
	return write.prop == abi::kPropDriverProvidedChaperoneJson && write.writeType == abi::kWriteSet && write.buffer &&
		   write.bufferSize >= 3;
}

int WritePropertyBatch(Proxy *self, uint64_t container, abi::PropertyWrite *batch, uint32_t count)
{
	for (uint32_t i = 0; batch && i < count; i++)
	{
		const abi::PropertyWrite &write = batch[i];
		if (!IsChaperoneJsonWrite(write))
			continue;
		std::string json((const char *)write.buffer, strnlen((const char *)write.buffer, write.bufferSize));
		std::string path = SaveHeadsetChaperone(json);
		if (path.empty())
			continue;
		// Set the path before the batch goes through: the driver writes the universe id right
		// after the JSON, and that is what makes the server (re)load the chaperone.
		abi::PropertyWrite pathWrite = {};
		pathWrite.prop = abi::kPropDriverProvidedChaperonePath;
		pathWrite.writeType = abi::kWriteSet;
		pathWrite.buffer = (void *)path.c_str();
		pathWrite.bufferSize = (uint32_t)path.size() + 1;
		pathWrite.tag = abi::kTagString;
		int result = VCall<int, uint64_t, abi::PropertyWrite *, uint32_t>(self->target, abi::slot::kPropertiesWriteBatch, container,
																			 &pathWrite, 1u);
		Log("chap  set Prop_DriverProvidedChaperonePath on container %llx -> rc=%d eError=%d", (unsigned long long)container, result,
			pathWrite.error);
	}
	return VCall<int, uint64_t, abi::PropertyWrite *, uint32_t>(self->target, abi::slot::kPropertiesWriteBatch, container, batch, count);
}

} // namespace

Proxy *WrapProperties(void *realProperties, const char *name)
{
	Proxy *proxy = MakeProxy(realProperties, name, name, abi::slot::kPropertiesCount);
	SetHook(proxy, abi::slot::kPropertiesWriteBatch, (void *)&WritePropertyBatch);
	return proxy;
}

} // namespace shim

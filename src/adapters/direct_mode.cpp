#include "adapters/direct_mode.h"

#include "core/config.h"
#include "core/log.h"
#include "core/openvr_abi.h"
#include "core/proxy.h"

#include <cstring>
#include <map>
#include <mutex>

namespace shim
{
namespace
{

using namespace abi;

// Virtual methods in the order of IVRDriverDirectModeComponent_007: the legacy server calls this
// object through its vtable.
class DirectMode007
{
  public:
	explicit DirectMode007(void *real) : m_real(real) {}

	virtual void CreateSwapTextureSet(uint32_t pid, const SwapTextureSetDesc *desc, SwapTextureSet *out)
	{
		VCall<void, uint32_t, const SwapTextureSetDesc *, SwapTextureSet *>(m_real, slot::kDirectModeCreateSwapTextureSet, pid, desc, out);
		Log("dm    CreateSwapTextureSet pid=%u %ux%u fmt=%u samples=%u -> handles %llx %llx %llx flags=%u", pid, desc ? desc->width : 0,
			desc ? desc->height : 0, desc ? desc->format : 0, desc ? desc->sampleCount : 0, out ? out->sharedTextureHandles[0] : 0,
			out ? out->sharedTextureHandles[1] : 0, out ? out->sharedTextureHandles[2] : 0, out ? out->textureFlags : 0);
	}

	virtual void DestroySwapTextureSet(uint64_t sharedTextureHandle)
	{
		Log("dm    DestroySwapTextureSet %llx", sharedTextureHandle);
		VCall<void, uint64_t>(m_real, slot::kDirectModeDestroySwapTextureSet, sharedTextureHandle);
	}

	virtual void DestroyAllSwapTextureSets(uint32_t pid)
	{
		Log("dm    DestroyAllSwapTextureSets pid=%u", pid);
		VCall<void, uint32_t>(m_real, slot::kDirectModeDestroyAllSwapTextureSets, pid);
	}

	virtual void GetNextSwapTextureSetIndex(uint64_t sharedTextureHandles[2], uint32_t (*indices)[2])
	{
		VCall<void, uint64_t *, uint32_t (*)[2]>(m_real, slot::kDirectModeGetNextSwapTextureSetIndex, sharedTextureHandles, indices);
	}

	virtual void SubmitLayer(const SubmitLayerPerEye007 (&perEye)[2])
	{
		// The legacy compositor does not say how far ahead the pose was predicted; report the
		// configured constant.
		float predictSeconds = GetConfig().predictMs / 1000.0f;
		SubmitLayerPerEye009 converted[2];
		for (int eye = 0; eye < 2; eye++)
		{
			memcpy(&converted[eye], &perEye[eye], sizeof(SubmitLayerPerEye007));
			converted[eye].hmdPosePredictionTimeInSecondsFromNow = predictSeconds;
		}
		long count = InterlockedIncrement(&m_submits);
		if (ShouldLog(count))
			Log("dm    SubmitLayer tex=%llx/%llx depth=%llx (n=%ld)", perEye[0].texture, perEye[1].texture, perEye[0].depthTexture, count);
		VCall<void, const SubmitLayerPerEye009 *>(m_real, slot::kDirectModeSubmitLayer, converted);
	}

	virtual void Present(uint64_t syncTexture)
	{
		long count = InterlockedIncrement(&m_presents);
		if (ShouldLog(count))
			Log("dm    Present sync=%llx (n=%ld)", syncTexture, count);
		VCall<void, uint64_t>(m_real, slot::kDirectModePresent, syncTexture);
	}

	virtual void PostPresent()
	{
		Throttling009 none = {0, 0};
		VCall<void, const Throttling009 *>(m_real, slot::kDirectModePostPresent, &none);
	}

	virtual void GetFrameTiming(void *frameTiming) // DriverDirectMode_FrameTiming, sized by its own m_nSize
	{
		VCall<void, void *>(m_real, slot::kDirectModeGetFrameTiming, frameTiming);
	}

  private:
	void *m_real;
	volatile long m_submits = 0;
	volatile long m_presents = 0;
};

} // namespace

void *GetDirectMode007(void *directMode009)
{
	static std::mutex mutex;
	static std::map<void *, DirectMode007 *> adapters;
	std::lock_guard<std::mutex> lock(mutex);
	DirectMode007 *&adapter = adapters[directMode009];
	if (!adapter)
		adapter = new DirectMode007(directMode009);
	return adapter;
}

} // namespace shim

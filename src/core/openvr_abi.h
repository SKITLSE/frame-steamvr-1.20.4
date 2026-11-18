// The slices of the OpenVR driver ABI the shim touches: struct layouts, property ids, type tags
// and vtable slot numbers. Declared here instead of pulling in openvr_driver.h because the shim
// has to speak two versions of several interfaces at once (the legacy server's and the driver's).
//
// Sources: OpenVR SDK 1.16.8 headers (what SteamVR 1.20 speaks), the current SDK headers (what
// the Frame driver speaks), and vrserver.exe 2.18.4 for the private IVRServerInternal interface.
#pragma once

#include <cstdint>

namespace shim::abi
{

// ---- IVRDriverContext: what a driver gets in IServerTrackedDeviceProvider::Init ----------------
struct IVRDriverContext
{
	virtual void *GetGenericInterface(const char *interfaceVersion, int *error) = 0;
	virtual uint64_t GetDriverHandle() = 0;
};

const int kInitErrorNone = 0;
const int kInitErrorInterfaceNotFound = 105; // VRInitError_Init_InterfaceNotFound

// ---- property and path values ------------------------------------------------------------------
enum PropertyTypeTag : uint32_t
{
	kTagFloat = 1,
	kTagInt32 = 2,
	kTagUint64 = 3,
	kTagBool = 4,
	kTagString = 5,
};

enum PropertyError : int32_t
{
	kPropSuccess = 0,
	kPropBufferTooSmall = 3,
	kPropUnknownProperty = 4,
};

enum PropertyWriteType : int32_t
{
	kWriteSet = 0,
	kWriteErase = 1,
	kWriteSetError = 2,
};

enum TrackedDeviceProperty : int32_t
{
	kPropCurrentUniverseId = 2004,			  // Prop_CurrentUniverseId_Uint64
	kPropDriverProvidedChaperonePath = 2048, // Prop_DriverProvidedChaperonePath_String (legacy servers)
	kPropDriverProvidedChaperoneJson = 2095, // Prop_DriverProvidedChaperoneJson_String (current servers)
};

// Root container the Steam-related paths live under.
const uint64_t kSteamPathRoot = 0x600000000ULL;

#pragma pack(push, 8)

struct PropertyWrite // PropertyWrite_t
{
	int32_t prop;
	int32_t writeType;
	int32_t setError;
	void *buffer;
	uint32_t bufferSize;
	uint32_t tag;
	int32_t error;
};

struct PathRead // PathRead_t
{
	uint64_t path;
	void *buffer;
	uint32_t bufferSize;
	uint32_t tag;
	uint32_t requiredBufferSize;
	int32_t error;
	const char *pathString;
};

// PathWrite_t. IVRPaths_002 appended two flag bytes; the shim only reads the common prefix.
struct PathWrite
{
	uint64_t path;
	int32_t writeType;
	int32_t setError;
	void *buffer;
	uint32_t bufferSize;
	uint32_t tag;
	int32_t error;
	const char *pathString;
};

// ---- IVRDriverDirectModeComponent ---------------------------------------------------------------
struct SwapTextureSetDesc
{
	uint32_t width, height, format, sampleCount;
};

struct SwapTextureSet
{
	uint64_t sharedTextureHandles[3];
	uint32_t textureFlags;
};

struct SubmitLayerPerEye007 // as in IVRDriverDirectModeComponent_007
{
	uint64_t texture, depthTexture;
	float bounds[4];
	float projection[4][4];
	float hmdPose[3][4];
};

struct SubmitLayerPerEye009 // _009 appended the prediction time
{
	uint64_t texture, depthTexture;
	float bounds[4];
	float projection[4][4];
	float hmdPose[3][4];
	float hmdPosePredictionTimeInSecondsFromNow;
};

struct Throttling009 // argument PostPresent gained in _009
{
	uint32_t framesToThrottle;
	uint32_t additionalFramesToPredict;
};

#pragma pack(pop)

static_assert(sizeof(PropertyWrite) == 40, "PropertyWrite_t layout");
static_assert(sizeof(PathRead) == 40, "PathRead_t layout");
static_assert(sizeof(PathWrite) == 48, "PathWrite_t layout");
static_assert(sizeof(SubmitLayerPerEye007) == 144, "SubmitLayerPerEye_t (_007) layout");
static_assert(sizeof(SubmitLayerPerEye009) == 152, "SubmitLayerPerEye_t (_009) layout");

// DriverPose_t is 280 bytes, mostly doubles. Offsets in doubles from the start:
const int kPoseWorldFromDriverRotation = 1;	   // w, x, y, z
const int kPoseWorldFromDriverTranslation = 5; // x, y, z
const int kPosePosition = 15;				   // x, y, z
const int kPoseRotation = 24;				   // w, x, y, z
const int kPoseResultByteOffset = 272;		   // ETrackingResult, int32
const int kPoseIsValidByteOffset = 276;
const int kPoseIsConnectedByteOffset = 279;
const uint32_t kPoseSize = 280;

// ---- vtable slots -------------------------------------------------------------------------------
namespace slot
{

enum Provider // IServerTrackedDeviceProvider_004
{
	kProviderInit = 0,
	kProviderCleanup = 1,
	kProviderGetInterfaceVersions = 2,
	kProviderCount = 7,
};

enum ServerDriverHost // IVRServerDriverHost_006
{
	kHostTrackedDeviceAdded = 0,
	kHostTrackedDevicePoseUpdated = 1,
	kHostCount = 12,
};

enum TrackedDevice // ITrackedDeviceServerDriver_005
{
	kDeviceGetComponent = 3,
	kDeviceCount = 6,
};

enum Properties // IVRProperties_001
{
	kPropertiesReadBatch = 0,
	kPropertiesWriteBatch = 1,
	kPropertiesCount = 4,
};

enum Paths // IVRPaths_001; _002 kept these four
{
	kPathsReadBatch = 0,
	kPathsWriteBatch = 1,
	kPathsStringToHandle = 2,
	kPathsHandleToString = 3,
	kPathsCount = 4,
};

enum DriverInput
{
	kDriverInput003Count = 7, // _004 added the pose components (7, 8), _005 eye tracking (9, 10)
};

enum DirectMode // same eight methods in _007 and _009
{
	kDirectModeCreateSwapTextureSet = 0,
	kDirectModeDestroySwapTextureSet = 1,
	kDirectModeDestroyAllSwapTextureSets = 2,
	kDirectModeGetNextSwapTextureSetIndex = 3,
	kDirectModeSubmitLayer = 4,
	kDirectModePresent = 5,
	kDirectModePostPresent = 6,
	kDirectModeGetFrameTiming = 7,
};

enum ServerInternal // IVRServerInternal_XXX as laid out in vrserver.exe 2.18.4
{
	kServerInternalSetFeatureMask = 52,
	kServerInternalGetFeatureMask = 53,
	kServerInternalGetPerEyeScale = 77,
};

} // namespace slot

} // namespace shim::abi

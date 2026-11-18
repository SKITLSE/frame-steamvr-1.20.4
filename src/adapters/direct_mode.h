// IVRDriverDirectModeComponent: how the compositor hands frames to the driver.
//
// The legacy server speaks _007, the Frame driver implements _009. Both have the same eight
// methods; _009 appended a prediction time to SubmitLayerPerEye_t and gave PostPresent a
// throttling argument.
#pragma once

namespace shim
{

// An object answering _007 calls on top of the driver's _009 component. One adapter is kept per
// component, so repeated requests return the same pointer.
void *GetDirectMode007(void *directMode009);

} // namespace shim

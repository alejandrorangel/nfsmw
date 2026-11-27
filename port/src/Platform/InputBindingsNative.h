// The PC input device's internals for InputNative.cpp's InputDevice (the
// binding API itself is src/Speed/Indep/Src/Input/PCInputBindings.h).
#pragma once

#include "Speed/Indep/Src/Input/PCInputBindings.h"

// sub_628410: the action table's initial data (the first device's ctor).
void PCInputCreateDevice();
int PCInputGetNumDeviceScalars();
const char *PCInputGetDeviceScalarName(int i);
// sub_63F850: the device's Initialize (plus, natively, the saved bindings).
void PCInputInitializeDevice();
// sub_6349B0: the device's PollDevice.
void PCInputPollDevice(float *prev, float *current);

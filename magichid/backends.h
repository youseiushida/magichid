// backends.h -- registry of all compiled-in device backends (the composition root).
#ifndef MH_BACKENDS_H_
#define MH_BACKENDS_H_

#include "device_backend.h"

extern const DeviceBackend *const BACKENDS[];   // index == profile id (see SET_IDENTITY)
extern const uint8_t N_BACKENDS;

#endif // MH_BACKENDS_H_

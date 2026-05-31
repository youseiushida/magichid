// =====================================================================================
//  backends.cpp  --  the ONLY place that lists concrete backends (composition root).
// =====================================================================================
//  To add a target (e.g. a Switch Pro Controller emulator): create backend_xxx.{h,cpp}
//  implementing DeviceBackend, then add one &BACKEND_XXX line below. To remove it,
//  delete its files and its line here -- the core never references it.
//  Array index == profile id carried by SET_IDENTITY.
// =====================================================================================
#include "backends.h"
#include "backend_universal.h"

const DeviceBackend *const BACKENDS[] = {
  &BACKEND_UNIVERSAL,                       // profile 0
  // &BACKEND_PROCON,                       // profile 1 (added in a later step)
};

const uint8_t N_BACKENDS = sizeof(BACKENDS) / sizeof(BACKENDS[0]);

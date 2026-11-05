#pragma once

// SF2Recomp's version is independent of the original game's display version.
// Release tags must be v<SF2RECOMP_VERSION>.
#ifndef SF2RECOMP_VERSION
#define SF2RECOMP_VERSION "0.2.0"
#endif
#include "lan_protocol.h"

#define SF2_STRINGIFY_INNER(x) #x
#define SF2_STRINGIFY(x) SF2_STRINGIFY_INNER(x)
#define SF2RECOMP_LAN_GREETING SF2RECOMP_VERSION "/" SF2_STRINGIFY(SF2RECOMP_LAN_COMPATIBILITY)

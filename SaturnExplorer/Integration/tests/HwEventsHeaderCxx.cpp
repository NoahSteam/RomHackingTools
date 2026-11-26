#include "SeHwEvents.h"

static_assert(sizeof(SeHwEvent) == 32, "SeHwEvent wire record is 32 bytes");

extern "C" unsigned int SeHwEventsCxxSize(void) { return (unsigned int)sizeof(SeHwEvent); }

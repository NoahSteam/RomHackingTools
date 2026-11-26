/* SeHwEvents.h must give the same 32-byte record to C (the exporter and glue) and C++14 (the
 * driver and front end); the C++ half is HwEventsHeaderCxx.cpp, linked into this executable. */
#include "SeHwEvents.h"
#include "SeLiveProtocol.h"
#include <stdio.h>

extern unsigned int SeHwEventsCxxSize(void);

int main(void)
{
    int bad = 0;
    if (sizeof(SeHwEvent) != 32u) { printf("C: sizeof(SeHwEvent) = %u\n", (unsigned)sizeof(SeHwEvent)); bad = 1; }
    if (SeHwEventsCxxSize() != 32u) { printf("C++: sizeof(SeHwEvent) = %u\n", SeHwEventsCxxSize()); bad = 1; }
    if (sizeof(SeHwEvent) != SE_LIVE_HWEV_REC_LEN) { printf("wire record length disagrees with the struct\n"); bad = 1; }
    if (SE_LIVE_HWEV_HDR_LEN != 4u + 4u + 8u + 8u + 8u + 4u + 4u) { printf("section header length\n"); bad = 1; }
    if (SE_LIVE_CAP_HW_EVENTS != 1024u) { printf("capability bit\n"); bad = 1; }
    return bad;
}

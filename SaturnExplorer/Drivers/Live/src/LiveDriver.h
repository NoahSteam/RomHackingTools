/* Saturn Explorer — Live driver (Seam A). Connects to a running, patched Yabause
 * over a local socket (see SeLiveProtocol.h) and serves its VDP state to the core
 * as an se_data_source, refreshed on a background thread. Native only (threads +
 * sockets) — not part of the WASM/web build.
 */
#ifndef SATURNEXPLORER_LIVE_DRIVER_H
#define SATURNEXPLORER_LIVE_DRIVER_H

#include "saturnexplorer/SeDataSource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open a live connection. 'endpoint' is the local socket path (POSIX) or named
 * pipe (Windows); pass NULL for the platform default (SE_LIVE_DEFAULT_*). On
 * success returns SE_OK and fills '*out' (whose 'close' stops the poll thread on
 * se_destroy). Returns SE_ERR_IO if the emulator isn't reachable. */
se_result se_live_open(const char* endpoint, se_data_source* out);

#ifdef __cplusplus
}
#endif

#endif /* SATURNEXPLORER_LIVE_DRIVER_H */

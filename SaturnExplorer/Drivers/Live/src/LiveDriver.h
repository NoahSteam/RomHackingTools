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

/* The live protocol version last reported by the connected server, or 0 if not
 * connected / not a live source. Compare against SE_LIVE_VERSION to detect a
 * client/emulator version mismatch. 'ds' must be an se_data_source filled by
 * se_live_open. */
uint32_t se_live_server_version(const se_data_source* ds);

#ifdef __cplusplus
}
#endif

#endif /* SATURNEXPLORER_LIVE_DRIVER_H */

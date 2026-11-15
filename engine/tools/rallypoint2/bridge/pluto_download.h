/* Bounded streaming HTTPS used by Hyperion's native Pluto runtime installer. */
#ifndef RP_PLUTO_DOWNLOAD_H
#define RP_PLUTO_DOWNLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RP_DOWNLOAD_OK 0
#define RP_DOWNLOAD_ERR_SETUP -300
#define RP_DOWNLOAD_ERR_NETWORK -301
#define RP_DOWNLOAD_ERR_TIMEOUT -302
#define RP_DOWNLOAD_ERR_STATUS -303
#define RP_DOWNLOAD_ERR_REDIRECT -304
#define RP_DOWNLOAD_ERR_SIZE -305
#define RP_DOWNLOAD_ERR_CANCELLED -306
#define RP_DOWNLOAD_ERR_WRITE -307
#define RP_DOWNLOAD_ERR_PANIC -308

typedef bool (*RpDownloadWrite)(void *context, const uint8_t *data, size_t size);
typedef void (*RpDownloadProgress)(void *context, uint64_t received, uint64_t total);
typedef bool (*RpDownloadCancel)(void *context);

/* Synchronous, but bounded: call from the installer's worker thread, never the game thread. */
int32_t rp_https_download(const char *url, uint64_t expected_size, void *context,
                          RpDownloadWrite write, RpDownloadProgress progress,
                          RpDownloadCancel cancel, char *error, size_t error_cap);

#ifdef __cplusplus
}
#endif

#endif /* RP_PLUTO_DOWNLOAD_H */

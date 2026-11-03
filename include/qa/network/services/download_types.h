/* Port of src/network/services/download-types.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_SERVICES_DOWNLOAD_TYPES_H
#define QA_NETWORK_SERVICES_DOWNLOAD_TYPES_H
#include <stdbool.h>
/* Native server downloads serve game assets only, never configuration or scripts that
 * happen to sit in a mounted directory: the TS extension list, case-insensitive. */
bool qa_is_served_download_type(const char *path);
#endif

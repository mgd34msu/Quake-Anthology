/*
 * Port of src/content/user-data.ts. String results are malloc'd;
 * NULL only on OOM. `home` NULL means homedir() ($HOME).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_USER_DATA_H
#define QA_CONTENT_USER_DATA_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/roots.h"

char *qa_default_user_content_root(void);
/* TS defaultAddonContentRoots(home = homedir()). */
bool qa_default_addon_content_roots(const char *home,
                                    qa_nightdive_addon_root **out,
                                    size_t *out_count);
char *qa_user_product_directory(const char *user_content_root,
                                const char *content_directory);

#endif

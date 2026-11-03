/*
 * Port of src/world/session/mod-files.ts: per-selection user file
 * stores and borrowed mod file mounts. Store roots are node
 * path.resolve(root, ".mods", encodeURIComponent(product),
 * encodeURIComponent(id)) on POSIX: normalized, relative roots against
 * the working directory. Stores returned by _for are owned by the
 * ModUserFiles and stay valid until its destroy.
 * Mount identity strings created for a returned mounted content are
 * retained for that instance's lifetime (the TS retains the mount
 * objects; the C mounter borrows their strings).
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_MOD_FILES_H
#define QA_WORLD_SESSION_MOD_FILES_H

#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/contracts/mods.h"
#include "qa/platform/files/writable.h"

typedef struct qa_mod_user_files qa_mod_user_files;

/* Root is copied. NULL only on allocation failure. */
qa_mod_user_files *qa_mod_user_files_create(const char *root);
void qa_mod_user_files_destroy(qa_mod_user_files *files);
const char *qa_mod_user_files_root(const qa_mod_user_files *files);
qa_user_file_store *qa_mod_user_files_for(qa_mod_user_files *files,
                                          qa_mod_selection selection);

/* installed may be NULL (TS undefined). NULL return is OOM. */
qa_content_mounted *qa_borrow_mod_file_mounts(
    qa_mod_selection selection, qa_content_id content,
    qa_content_mounted *installed, qa_user_file_store *writable);

#endif

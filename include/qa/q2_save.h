#ifndef QA_Q2_SAVE_H
#define QA_Q2_SAVE_H

#include "qa/filesystem.h"

/* Original Quake II engine file extents, independent of the GAME DLL ABI. */
enum {
    QA_Q2_SAVE_COMMENT_BYTES = 32,
    QA_Q2_SAVE_MAP_COMMAND_BYTES = 128,
    QA_Q2_SAVE_CVAR_BYTES = 128,
    QA_Q2_SAVE_MAP_BYTES = 64,
    QA_Q2_SAVE_CONFIGSTRINGS = 2080,
    QA_Q2_SAVE_CONFIGSTRING_BYTES = 64,
    QA_Q2_SAVE_AREA_PORTALS = 1024
};

typedef struct qa_q2_save_cvar {
    char name[QA_Q2_SAVE_CVAR_BYTES], value[QA_Q2_SAVE_CVAR_BYTES];
} qa_q2_save_cvar;
typedef struct qa_q2_save_server {
    char comment[QA_Q2_SAVE_COMMENT_BYTES];
    char map_command[QA_Q2_SAVE_MAP_COMMAND_BYTES];
    qa_q2_save_cvar *cvars;
    size_t cvar_count;
} qa_q2_save_server;
typedef struct qa_q2_save_level {
    char name[QA_Q2_SAVE_MAP_BYTES];
    /* Opaque bytes produced and consumed by the actual GAME's Write/ReadLevel. */
    qa_buffer game;
    char configstrings[QA_Q2_SAVE_CONFIGSTRINGS][QA_Q2_SAVE_CONFIGSTRING_BYTES];
    int32_t portal_open[QA_Q2_SAVE_AREA_PORTALS];
} qa_q2_save_level;
typedef struct qa_q2_save_data {
    qa_q2_save_server server;
    /* Opaque bytes produced and consumed by the actual GAME's Write/ReadGame. */
    qa_buffer game;
    qa_q2_save_level *levels;
    size_t level_count;
} qa_q2_save_data;

/* Server fields have original fixed extents. Decoding retains their exact
 * padding and owns the latched cvar rows. Output is unchanged on failure. */
bool qa_q2_save_server_decode(qa_bytes, qa_q2_save_server *, qa_error *);
bool qa_q2_save_server_encode(const qa_q2_save_server *, qa_buffer *, qa_error *);
void qa_q2_save_server_dispose(qa_q2_save_server *);
bool qa_q2_save_server_read(qa_fs_root *, const char *, qa_q2_save_server *, qa_error *);
/* A new-level autosave may have no per-map files. Every admitted level has
 * its real .sav/.sv2 pair. The GAME validates its own opaque files at load. */
bool qa_q2_save_directory_read(qa_fs_root *, const char *, qa_q2_save_data **, qa_error *);
bool qa_q2_save_directory_write(qa_fs_root *, const char *, const qa_q2_save_data *, uint64_t, qa_error *);
void qa_q2_save_destroy(qa_q2_save_data *);

#endif

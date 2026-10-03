#ifndef QA_FRONTEND_H
#define QA_FRONTEND_H

#include "qa/application.h"
#include "qa/display.h"
#include "qa/network.h"
#include <stdio.h>

#define QA_FRONTEND_COMMAND_OWNER UINT64_C(0x716166726f6e7401)

typedef struct qa_frontend qa_frontend;
struct frontend_install_locations;
struct qa_application_persistence_ops;
typedef struct qa_frontend_options {
    qa_application_options application;
    qa_display_options display;
    const char *game, *map_game, *map;
    const char *movement, *character, *character_model;
    const char *font_directory, *font_file;
    const char *network_host, *network_connect;
    const char *native_runtime_root, *native_wine;
    char *native_bootstrap;
    const char **game_paths, **save_game_paths;
    size_t game_path_count, save_game_path_count;
    struct frontend_install_locations *install_locations;
    qa_net_protocol_id network_protocol;
    uint16_t network_port;
    const char **mods;
    size_t mod_count;
    const char **startup;
    size_t startup_count;
    unsigned seats;
    uint64_t frame_limit;
    float gamma;
    bool dedicated, menu, audio, original;
    /* Actual installed backend/file resolver capabilities stay borrowed for
     * the frontend lifetime and any retained restore operation. */
    const struct qa_application_persistence_ops *persistence_services;
} qa_frontend_options;

void qa_frontend_options_default(qa_frontend_options *);
/* argv strings are borrowed until options_destroy; generated command strings
 * and option pointer arrays are owned. Parsing never opens SDL or a session. */
bool qa_frontend_options_parse(int argc, char *const argv[], qa_frontend_options *, qa_error *);
/* Resolves executable-relative and saved search directories after the genuine
 * executable bootstrap path is installed. Owned paths live until destroy. */
bool qa_frontend_options_resolve_locations(qa_frontend_options *,qa_error *);
const char *qa_frontend_options_locations_file(const qa_frontend_options *);
void qa_frontend_options_destroy(qa_frontend_options *);
/* On construction failure, a non-NULL output retains the actual frontend
 * when checked cleanup rejects. Retry qa_frontend_destroy on that owner. */
bool qa_frontend_create(const qa_frontend_options *, qa_frontend **, qa_error *);
/* One frontend owns SDL global lifetime and the only platform event pump.
 * step measures no time: elapsed_ns comes from run or a replay caller. */
bool qa_frontend_step(qa_frontend *, uint64_t elapsed_ns, qa_error *);
/* The driver adopts the current heap owner from slot at each completed frame
 * boundary. Serialized restore requests publish through that same slot. */
bool qa_frontend_run(qa_frontend **slot, qa_error *);
bool qa_frontend_destroy(qa_frontend *, qa_error *);
/* Drives genuine final input waits at returned boundaries. A consumed owner
 * clears slot even when terminal cleanup reports an earlier source failure;
 * rejected cleanup retains the actual slot and its borrowed options. */
bool qa_frontend_shutdown(qa_frontend **slot, qa_error *);
qa_application *qa_frontend_application(qa_frontend *);
/* A rejected construction/cleanup returns its actual application in retained.
 * Keep the borrowed options alive until qa_application_destroy succeeds. */
bool qa_frontend_list_content(const qa_frontend_options *, FILE *, qa_application **retained, qa_error *);

#endif

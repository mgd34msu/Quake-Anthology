#ifndef QA_APPLICATION_Q3_CAMPAIGN_H
#define QA_APPLICATION_Q3_CAMPAIGN_H
#include "qa/application_q3_client.h"
#include "qa/filesystem.h"

typedef struct qa_application_q3_campaign {
    qa_session *session;
    const qa_launch_snapshot *publication;
    const qa_launch_instance *launch;
    const qa_q3_game *source_game;
    qa_q3_host *original_host;
    qa_actor_owner source_owner;
    qa_product_id content_product;
    qa_q3_product product;
    qa_console *console;
    qa_cvars *cvars;
    qa_vfs *content;
    qa_mount_id write_mount;
    qa_fs_root *config_root, *profile_root;
    qa_player_progress *profile;
    const char *map;
    const qa_resource *map_resource;
    uint64_t publication_generation, command_generation, map_revision;
    int32_t source_time, match_start_time, game_type;
    bool native_source;
} qa_application_q3_campaign;

/* Borrows the actual published physical Q3 GAME without a player or
 * chosen-mode inference. Config root is the source's first writable mount;
 * profile root/store are the independent application player-profile owner.
 * Either writable owner may be absent. Native GAME supplies its real source
 * level; an original GAME requires its genuine admitted context and published
 * CS_LEVEL_START_TIME. All borrows require this source cut. */
bool qa_application_q3_campaign_read(qa_application *, qa_actor_owner source_or_zero,
    qa_application_q3_campaign *, qa_error *);
/* Pure short-lived cut check, including the unchanged actual source clock.
 * Refresh the view for a later frame; check before and after callbacks. */
bool qa_application_q3_campaign_current(qa_application *, const qa_application_q3_campaign *);
bool qa_application_q3_campaign_postgame_context(qa_application *,
    const qa_application_q3_campaign *, const qa_command_context *, qa_error *);
/* Missing local human mapping succeeds with found=false. Physical source
 * slot and full actor generation qualify the true roster seat. */
bool qa_application_q3_campaign_local_seat(qa_application *,
    const qa_application_q3_campaign *, uint32_t source_client,
    bool *found, uint32_t *seat, qa_actor_id *, qa_error *);
/* MENU's actual local CGAME recipient must name this exact physical GAME and
 * live source actor; CGAME cvars never replace the returned GAME registry. */
bool qa_application_q3_campaign_menu_read(qa_application *, qa_actor_owner receiver,
    uint32_t seat, qa_application_q3_campaign *, qa_application_q3_client_context *, qa_error *);
#endif

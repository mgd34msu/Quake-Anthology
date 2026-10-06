#ifndef QA_FRONTEND_NATIVE_Q3_CLIENT_H
#define QA_FRONTEND_NATIVE_Q3_CLIENT_H

#include "internal.h"
#include "../../presentation/q3_native/native.h"
#include "../../presentation/q3_native/mission_hud.h"
#include "../../presentation/q3_native/loading.h"
#include "qa/persistence_content.h"
#include "qa/application_equipment.h"
#include "client_registry.h"

typedef struct frontend_native_q3_view {
    uint64_t identity;
    qa_string_id service_owner;
    qa_actor_owner receiver, source_owner;
    uint32_t seat, launch_seat, physical_client;
    qa_q3_product product;
    qa_actor_id actor;
    const qa_launch_instance *source_launch;
    qa_vfs *source_files, *mounts;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_font_library *fonts;
    qa_audio_bank *sounds;
    qa_audio_music *music;
    qa_media_library *movies;
    qa_native_q3_wire_reader *reader;
    qa_native_q3_client_service *client;
    frontend_client_registry *registry;
    qa_cvars *cvars;
    qa_input_seat *input;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    q3n_native *core;
    q3n_mission_hud *mission;
    q3n_loading *loading;
    qa_audio_listener listener;
    bool has_listener, music_attached;
} frontend_native_q3_view;

/* Composition callbacks belong to the actual selected equipment/appearance
 * owners. The adapter supplies world, client, audio and constructor services. */
typedef struct frontend_native_q3_composition {
    void *context;
    bool (*idle)(const void *);
    bool (*destroy)(void *,qa_error *);
    bool (*rebind_ready)(const void *,const qa_frontend *,qa_error *);
    void (*rebind)(void *,qa_frontend *);
    bool (*begin_frame)(void *,const q3n_frame *,qa_error *);
    void (*end_frame)(void *);
    bool (*before_render)(void *,const q3n_frame *,qa_error *);
    bool (*actor_admitted)(const void *, qa_actor_id);
    bool (*weapon_snapshot)(const void *, qa_application_equipment_view *, bool *requested, qa_error *);
    bool (*event)(void *, const q3n_frame *, q3n_entity *, const qa_q3_entity *,
        qa_vec3, bool *suppressed, qa_error *);
    bool (*body_hidden)(void *, const q3n_frame *, const qa_application_native_q3_entity *, bool *, qa_error *);
    bool (*body)(void *, const q3n_frame *, const qa_application_native_q3_entity *, uint32_t,
        const qa_q3_ref_entity *, bool base, bool *consumed, qa_error *);
    bool (*packet)(void *, const q3n_frame *, const qa_application_native_q3_entity *, q3n_entity *,
        const qa_q3_ref_entity *, bool *consumed, qa_error *);
    bool (*view_weapon)(void *, const q3n_frame *, const qa_q3_player *, bool *, qa_error *);
    bool (*held_weapon)(void *, const q3n_frame *, const qa_q3_entity *, const qa_q3_ref_entity *, bool *, qa_error *);
    bool (*weapon_warning)(void *, const q3n_frame *, q3n_weapon_hud *, qa_error *);
    bool (*prepare_view)(void *, const qa_q3_refdef *, qa_q3_scene_options *, qa_error *);
    bool (*submit_view)(void *, const qa_q3_scene_options *, qa_scene_frame *, qa_error *);
    void (*scene_cleared)(void *);
} frontend_native_q3_composition;
typedef struct frontend_native_q3_factory {
    void *context;
    bool (*compose)(void *,frontend_native_q3 *,frontend_native_q3_composition *,qa_error *);
} frontend_native_q3_factory;

bool frontend_native_q3_create(qa_frontend *, const qa_application_native_q3_presentation *,
    uint32_t launch_seat, const frontend_native_q3_factory *, frontend_native_q3 **, qa_error *);
/* Install genuine local recipients after the published world is prepared.
 * An already installed original CGAME keeps its physical display recipient. */
bool frontend_native_q3_round_admit(qa_frontend *, qa_actor_owner source,
    uint32_t launch_seat, uint32_t physical_client, qa_actor_id previous,
    qa_actor_id admitted, qa_error *);
bool frontend_native_q3_sync(qa_frontend *,const frontend_native_q3_factory *,qa_error *);
bool frontend_native_q3_frame(qa_frontend *, uint32_t physical_seat, qa_scene_rect,
    bool *rendered, qa_error *);
bool frontend_native_q3_listener(const qa_frontend *,uint32_t physical_seat,qa_audio_listener *);
bool frontend_native_q3_actor_admitted(const qa_frontend *, uint32_t physical_seat, qa_actor_id);
bool frontend_native_q3_weapon_snapshot(const qa_frontend *, uint32_t physical_seat,
    qa_application_equipment_view *, bool *requested, qa_error *);
bool frontend_native_q3_audio_view(const qa_frontend *,const qa_audio_asset *,qa_vfs **);
bool frontend_native_q3_effect(qa_frontend *, qa_application *, qa_actor_owner receiver,
    uint32_t launch_seat, qa_application_q3_client_effect, const char *, bool *handled, qa_error *);
bool frontend_native_q3_remap(qa_frontend *,const char *,const char *,float,qa_error *);
bool frontend_native_q3_idle(const qa_frontend *);
bool frontend_native_q3_retire_ready(const qa_frontend *, qa_error *);
bool frontend_native_q3_destroy(qa_frontend *, qa_error *);
/* Retire actual client references before their canonical GAME/provider and
 * manager registry roots. Other native source rows keep their own lifetimes. */
bool frontend_native_q3_retire_source(qa_frontend *,qa_actor_owner,const qa_launch_instance *,qa_error *);
/* Replacing the prepared render world retires its real native client rows:
 * registered inline handles belong to that world. Same-map source restart
 * retains these rows and never enters this world replacement path. */
bool frontend_native_q3_retire_world(qa_frontend *,qa_error *);
bool frontend_native_q3_publish_world(qa_frontend *,qa_error *);
size_t frontend_native_q3_count(const qa_frontend *);
frontend_native_q3 *frontend_native_q3_at(const qa_frontend *,size_t);
bool frontend_native_q3_read(const qa_frontend *, size_t, frontend_native_q3_view *, qa_error *);
/* Borrow actual constructor/import heaps before core initialization. This
 * admits the synchronous factory callback without using capture enumerators. */
bool frontend_native_q3_factory_view(const frontend_native_q3 *,frontend_native_q3_view *,qa_error *);
bool frontend_native_q3_recipient(const qa_frontend *, uint32_t physical_seat,
    qa_application_q3_client_context *, uint64_t *action_generation, bool *found, qa_error *);
bool frontend_native_q3_client_cvars_read(const qa_frontend *, uint32_t physical_seat,
    qa_cvars **, qa_actor_owner *receiver, uint32_t *launch_seat, bool *present, qa_error *);
/* A real selected composition owner can hold this row during synchronous
 * frame callbacks. The completed source context is requalified on each use. */
bool frontend_native_q3_borrow(frontend_native_q3 *,qa_application_q3_client_context *,qa_error *);
bool frontend_native_q3_borrow_current(const frontend_native_q3 *,const qa_application_q3_client_context *);
bool frontend_native_q3_context_current(const frontend_native_q3 *,const qa_application_q3_client_context *);
bool frontend_native_q3_installed_context(const frontend_native_q3 *,qa_application_q3_client_context *,qa_error *);
void frontend_native_q3_release(frontend_native_q3 *);
bool frontend_native_q3_animation_holder(const qa_frontend *, size_t row, uint32_t client,
    const qa_resource **, const qa_vfs_acquisition **, qa_error *);
/* Options for the ordinary native asset and renderer constructors. */
bool frontend_native_q3_asset_options(frontend_native_q3 *, qa_q3_presentation_asset_options *, qa_error *);
bool frontend_native_q3_backend_options(frontend_native_q3 *, qa_q3_presentation_options *, qa_error *);
bool frontend_native_q3_rebind_ready(const qa_frontend *, const qa_frontend *, qa_error *);
void frontend_native_q3_rebind(qa_frontend *, qa_frontend *);

#endif

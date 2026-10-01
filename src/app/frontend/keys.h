#ifndef QA_FRONTEND_KEYS_H
#define QA_FRONTEND_KEYS_H
#include "config_scripts.h"
#include "qa/q3_key.h"
#include "qa/q3_product_policy.h"
#include "qa/source_save.h"

typedef struct frontend_keys frontend_keys;
typedef struct frontend_key_profile frontend_key_profile;
typedef struct frontend_key_profile_view {
    const frontend_key_profile *profile;
    const qa_q3_key *state;
    qa_cvars *cvars;
    uint64_t identity;
    qa_product_id product;
    bool demo;
} frontend_key_profile_view;
typedef struct frontend_keys_cvar_refs {
    void *context;
    bool (*encode)(void *,const qa_cvars *,qa_application_console_scope *,qa_error *);
    bool (*provider)(void *,qa_source_save_io *,qa_actor_owner *);
    /* Qualifies the actual registry at source construction, before installing
     * the shared key pointer in UI imports. */
    bool (*qualify)(void *,const qa_application_console_scope *,const qa_cvars *,qa_error *);
} frontend_keys_cvar_refs;
frontend_keys *frontend_keys_create(qa_error *);
bool frontend_keys_destroy(frontend_keys *,qa_error *);
/* Success transfers files to the profile, and returns one caller reference.
 * The caller prepares a real selected-source ConfigStore, independent of HUD
 * and CHARACTER assets. Failure leaves files with the caller. */
bool frontend_keys_prepare(frontend_keys *,frontend_config_files *,qa_cvars *,
                           const qa_q3_product_policy *,bool dedicated,frontend_key_profile **,qa_error *);
bool frontend_key_profile_retain(frontend_key_profile *,qa_error *);
bool frontend_key_profile_release(frontend_key_profile *,qa_error *);
qa_q3_key *frontend_key_profile_state(const frontend_key_profile *);
frontend_config_files *frontend_key_profile_files(const frontend_key_profile *);
uint64_t frontend_key_profile_id(const frontend_key_profile *);
const char *frontend_key_profile_game_directory(const frontend_key_profile *);
frontend_key_profile *frontend_keys_profile(const frontend_keys *,uint64_t);
/* Rebinds only the genuine profile registry. No key file is read. Restored
 * profiles qualify their saved registry identity before creating imports. */
bool frontend_key_profile_bind(frontend_key_profile *,qa_cvars *,const frontend_keys_cvar_refs *,qa_error *);
/* Detaches only the retiring physical registry; the shared byte owner and UI
 * aliases survive source reconstruction. */
bool frontend_key_profile_detach(frontend_key_profile *,const qa_cvars *,qa_error *);
bool frontend_keys_publish(frontend_keys *,frontend_key_profile *,qa_cvars *,qa_error *);
bool frontend_keys_authorization(const frontend_keys *,uint8_t out[33],bool *demo,qa_error *);
/* Pure identity observation for capture, restore admission and native client
 * connections. The enclosing source owner qualifies registry liveness. */
bool frontend_keys_current(const frontend_keys *,frontend_key_profile_view *);
bool frontend_keys_view_current(const frontend_keys *,const frontend_key_profile_view *);
bool frontend_keys_save(frontend_keys *,qa_error *);
bool frontend_keys_visit(const frontend_keys *,const qa_application_content_visitor *,qa_error *);
bool frontend_keys_checkpoint(const frontend_keys *,const qa_application_content_graph *,
                              const frontend_keys_cvar_refs *,qa_buffer *,qa_error *);
bool frontend_keys_restore(qa_application_content_graph *,const qa_q3_product_policy *,
                           const frontend_keys_cvar_refs *,qa_bytes,frontend_keys **,qa_error *);
/* Source aliases and the published source are installed before releasing the
 * temporary ownership of the fully decoded profile roster. */
bool frontend_keys_finish_restore(frontend_keys *,qa_error *);
#endif

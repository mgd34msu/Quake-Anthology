#ifndef QA_Q3_NATIVE_SELECTED_MEDIA_H
#define QA_Q3_NATIVE_SELECTED_MEDIA_H

#include "weapon.h"
#include "qa/vfs.h"

typedef struct q3n_selected_media q3n_selected_media;
typedef struct q3n_selected_media_options {
    qa_vfs *content;
    qa_q3_presentation_assets *assets;
    qa_q3_product product;
} q3n_selected_media_options;
typedef struct q3n_selected_animation {
    qa_vfs *content;
    const qa_resource *resource;
    const qa_vfs_acquisition *receipt;
    const qa_player_animation_config *config;
} q3n_selected_animation;
typedef struct q3n_selected_media_request {
    int32_t weapon;
    bool view_required;
    const q3n_selected_animation *character;
    void *context;
    bool (*current)(void *);
} q3n_selected_media_request;

/* Parse the actual retained animation bytes without acquisition or registry
 * services. The source path participates in authored parser diagnostics. */
bool q3n_selected_animation_parse(qa_bytes, const char *source_path,
    qa_player_animation_config *, qa_error *);

/* The caller owns the actual selected registry and content view through this
 * cache's lifetime. Live admission qualifies its genuine equipment observation
 * before and after source services. This leaf creates no primary CGAME. */
bool q3n_selected_media_create(const q3n_selected_media_options *, q3n_selected_media **, qa_error *);
bool q3n_selected_media_idle(const q3n_selected_media *);
void q3n_selected_media_destroy(q3n_selected_media *);
bool q3n_selected_media_options_read(const q3n_selected_media *, q3n_selected_media_options *, qa_error *);
bool q3n_selected_media_prepare(q3n_selected_media *, const q3n_selected_media_request *,
    q3n_selected_weapon_media *, qa_error *);
bool q3n_selected_media_read(const q3n_selected_media *, int32_t weapon,
    bool view_required, q3n_selected_weapon_media *, qa_error *);
bool q3n_selected_media_animation(const q3n_selected_media *, q3n_selected_animation *,
    bool *character, qa_error *);

typedef struct q3n_selected_media_refs {
    void *context;
    bool (*view_encode)(void *, const qa_vfs *, uint64_t *, qa_error *);
    bool (*view_decode)(void *, uint64_t, qa_vfs **, qa_error *);
    bool (*resource_encode)(void *, const qa_resource *, uint64_t *pool, uint64_t *resource, qa_error *);
    bool (*resource_decode)(void *, uint64_t pool, uint64_t resource, const qa_resource **, qa_error *);
} q3n_selected_media_refs;
/* The genuine selected assets capture lease and imported content/resource
 * dictionary precede these codecs. Restore attaches exact existing handles
 * and animation bytes/config; it never registers, acquires or parses. */
bool q3n_selected_media_checkpoint(const q3n_selected_media *, const q3n_selected_media_refs *, qa_buffer *, qa_error *);
bool q3n_selected_media_restore(q3n_selected_media *, const q3n_selected_media_refs *, qa_bytes, qa_error *);

#endif

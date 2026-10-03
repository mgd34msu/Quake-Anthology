#ifndef QA_FRONTEND_MUSIC_SOURCES_PRIVATE_H
#define QA_FRONTEND_MUSIC_SOURCES_PRIVATE_H
#include "music_sources.h"
typedef struct frontend_music_world {
    qa_launch_instance_lease *metadata;
    qa_resource *map;
    qa_actor_owner provider;
    uint64_t map_revision;
    char *instance;
} frontend_music_world;
typedef struct frontend_music_command {
    struct frontend_music_command *next;
    qa_command_context context;
    char *script;
    char **argv;
    size_t argc;
} frontend_music_command;
struct frontend_music_sources {
    qa_frontend *frontend;
    qa_application *application;
    qa_audio_engine *engine;
    qa_audio_music_controls *controls;
    qa_catalog *menu_catalog;
    qa_product_id menu_product;
    frontend_music_sources **slot;
    frontend_music_policy *policies[2];
    uint64_t buses[2], seed, command_registry;
    frontend_music_world world;
    frontend_music_origin origin;
    qa_launch_instance_lease *origin_metadata;
    char *origin_instance, *origin_product;
    qa_sha256_digest origin_identity;
    bool has_origin, origin_bound, origin_recipe;
    frontend_music_command *commands, *commands_tail;
    frontend_music_slot output;
    bool busy, restoring;
};
bool frontend_music_sources_current(const frontend_music_sources *);
bool frontend_music_sources_origin_checkpoint_current(const frontend_music_sources *);
bool frontend_music_world_current(const frontend_music_sources *);
void frontend_music_world_dispose(frontend_music_world *);
bool frontend_music_world_capture(frontend_music_sources *, qa_error *);
qa_product_id frontend_music_world_fallback(const frontend_music_sources *);
void frontend_music_command_free(frontend_music_command *);
#endif

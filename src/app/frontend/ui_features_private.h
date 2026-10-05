#ifndef QA_FRONTEND_UI_FEATURES_PRIVATE_H
#define QA_FRONTEND_UI_FEATURES_PRIVATE_H
#include "ui_features.h"
#include "qa/persistence_slots.h"
typedef struct frontend_cinematic_captions frontend_cinematic_captions;
typedef struct frontend_shared_ui frontend_shared_ui;
typedef struct frontend_caption_collection {
    qa_arena *arena;
    qa_active_caption *values;
    size_t count;
    qa_error *error;
    bool failed;
} frontend_caption_collection;
void frontend_caption_count(void *,const qa_active_caption *);
void frontend_caption_collect(void *,const qa_active_caption *);
typedef struct frontend_ui_seat_features {
    qa_sound_captions *captions;
    qa_localization *localization;
    char *language;
    char localized[1024]; /* Immediate draw result, never a retained label. */
    qa_save_slot_listing saves;
    qa_ui_row *save_rows;
    char *save_details;
    qa_error *save_qualification;
    char **save_product_keys, **save_product_labels;
    size_t save_product_count, selected_product;
    size_t selected_save;
    uint64_t save_revision;
    char *save_name;
    char save_error[256];
    bool overwrite;
    char *campaign_instance, *shown_instance;
    uint64_t campaign_generation, shown_generation, shown_map_revision, shown_command_generation;
    int32_t shown_intermission;
    int32_t campaign_level, campaign_tier;
    bool campaign_selected, shown_result;
    qa_ui_row *campaign_rows;
    char *campaign_labels;
    size_t campaign_capacity;
    char campaign_status[256], campaign_title[256];
} frontend_ui_seat_features;
struct frontend_ui_features {
    qa_frontend *frontend;
    qa_caption_library *tracks;
    qa_localization_pool *catalogs;
    frontend_cinematic_captions *cinematic_captions;
    frontend_shared_ui *shared_ui;
    frontend_ui_seat_features seats[QA_INPUT_LOCAL_SEATS];
    const qa_font *bold;
    const qa_font *console;
    const qa_font **fallbacks;
    size_t fallback_count, fallback_capacity;
    qa_error audio_error;
    bool handling;
    qa_localization_profile ui_profile;
};
frontend_ui_seat_features *frontend_ui_features_seat(frontend_seat *);
#endif

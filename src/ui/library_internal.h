#ifndef QA_UI_LIBRARY_INTERNAL_H
#define QA_UI_LIBRARY_INTERNAL_H
#include "internal.h"
#include "qa/ui_library.h"
typedef struct library_profile {
    qa_launch_seat seat;
    char *name, *team;
    char *character_model, *character_skin, *character_head_model, *character_head_skin;
} library_profile;
typedef struct library_page {
    struct qa_ui_library *owner;
    qa_ui_id id;
} library_page;
typedef struct library_choice_rows {
    qa_arena storage;
    qa_ui_library_choice *rows;
    size_t count;
    const char *selected, *classname;
} library_choice_rows;
typedef struct library_choice_cache {
    library_choice_rows versions[2];
    unsigned current;
    const qa_launch_draft *draft;
    const qa_catalog *catalog;
    uint64_t draft_revision, catalog_generation, services_revision;
    qa_mode_kind mode;
    qa_game_family family;
    qa_product_edition edition;
    bool valid;
} library_choice_cache;
typedef struct library_arena_rows {
    qa_arena storage;
    qa_ui_row *rows;
    int32_t *numbers, *tiers;
    const char **tier_labels;
    size_t count, tier_count;
} library_arena_rows;
struct qa_ui_library {
    qa_ui *ui;
    qa_application *application;
    qa_ui_id menu;
    qa_catalog *catalog;
    qa_launch_draft *draft;
    qa_ui_library_services services;
    qa_game_family family;
    qa_product_edition edition;
    qa_product_id native_product;
    qa_mode_kind mode_preference;
    int32_t native_skill, arena_number, arena_tier;
    unsigned group;
    qa_ui_library_field field;
    size_t page, roster_page;
    char *monster_classname;
    bool last_authored, draft_selected;
    char status[256], title[512], labels[48][512];
    qa_ui_control controls[48];
    library_page pages[9];
    library_choice_cache fields[QA_UI_LIBRARY_BOT_TEAM + 1], campaigns;
    library_choice_rows *choice_build;
    qa_ui_library_choice *choices;
    size_t choice_count;
    const char *selected;
    char inline_labels[3][512];
    const char *inline_choices[3];
    qa_ui_row *arena_rows;
    int32_t *arena_numbers;
    size_t arena_count;
    const char **tier_labels;
    int32_t *tiers;
    size_t tier_count;
    library_arena_rows arena_versions[2];
    unsigned arena_version;
    const qa_base_arena_catalog *arena_catalog;
    const qa_cvars *arena_cvars;
    const qa_launch_draft *arena_draft;
    uint64_t arena_profile_revision, arena_catalog_generation, arena_draft_revision;
    int32_t arena_cached_tier;
    bool arena_valid;
    uint64_t revision;
    library_profile *local_players;
    size_t local_player_count;
};
void ui_library_clear(qa_ui_library *);
#endif

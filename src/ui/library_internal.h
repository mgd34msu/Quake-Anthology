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
    bool last_authored;
    char status[256], title[512], labels[48][512];
    qa_ui_control controls[48];
    library_page pages[9];
    qa_ui_library_choice *choices;
    size_t choice_count, choice_capacity;
    char selected[512], inline_labels[3][512];
    const char *inline_choices[3];
    qa_ui_row *arena_rows;
    int32_t *arena_numbers;
    size_t arena_count, arena_capacity, arena_number_capacity;
    const char **tier_labels;
    int32_t *tiers;
    size_t tier_count, tier_capacity, tier_number_capacity;
    uint64_t revision;
    library_profile *local_players;
    size_t local_player_count;
};
void ui_library_clear(qa_ui_library *);
#endif

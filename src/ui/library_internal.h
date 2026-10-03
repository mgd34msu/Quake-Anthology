#ifndef QA_UI_LIBRARY_INTERNAL_H
#define QA_UI_LIBRARY_INTERNAL_H
#include "internal.h"
typedef struct library_profile {
    qa_launch_seat seat;
    char *name, *team;
    char *character_model, *character_skin, *character_head_model, *character_head_skin;
} library_profile;
typedef struct library_gameplay {
    const char *component;
    bool original;
} library_gameplay;
struct qa_ui_library {
    qa_ui *ui;
    qa_application *application;
    qa_ui_id menu;
    qa_catalog *catalog;
    qa_product_id product;
    qa_ui_row *products, *maps;
    qa_product_id *product_ids;
    size_t *map_indices;
    size_t product_count, product_capacity, id_capacity, map_count, map_capacity, index_capacity;
    size_t selected_product, selected_map;
    int32_t skill;
    bool starts, dirty, original;
    char *game_type;
    library_gameplay *gameplay;
    const char **gameplay_labels;
    size_t gameplay_count, gameplay_capacity, gameplay_label_capacity;
    uint64_t revision;
    char query[321], status[256];
    qa_buffer query_lower;
    qa_ui_control controls[9];
    library_profile *local_players;
    size_t local_player_count;
};
void ui_library_clear(qa_ui_library *);
#endif

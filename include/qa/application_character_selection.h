#ifndef QA_APPLICATION_CHARACTER_SELECTION_H
#define QA_APPLICATION_CHARACTER_SELECTION_H

#include "qa/application.h"

typedef struct qa_native_q3_character_declaration {
    const char *model, *skin, *head_model, *head_skin;
} qa_native_q3_character_declaration;

/* Retained constructor declarations, before any resource registration. The
 * lifetime owns copies and a selected launch metadata lease. Execution stays
 * borrowed and current validates the full actor generation and publication. */
typedef struct qa_native_q3_character_selection {
    qa_actor_owner owner;
    qa_product_id product;
    uint64_t publication_generation;
    const qa_launch_instance *launch;
    qa_vfs *content;
    const char *definition, *model, *skin, *head_model, *head_skin;
    void *lifetime;
    bool (*current)(void *, const struct qa_native_q3_character_selection *);
    void (*release)(void *);
} qa_native_q3_character_selection;

typedef struct qa_application_character_declaration {
    const qa_launch_provider *provider;
    const qa_launch_binding *binding;
    qa_product_id product;
    qa_game_family family;
    const char *definition;
    qa_native_q3_character_declaration appearance;
} qa_application_character_declaration;

/* Pure candidate preparation: all fields borrow these exact choices/catalog.
 * Resolve actor, seat, then default CHARACTER binding without a live roster.
 * No declaration returns found=false, including unknown external selections.
 * The getter never chooses a default or derives a name from a model path. */
bool qa_application_character_declaration_read(qa_catalog *, const qa_launch_choices *,
    const qa_launch_seat *, qa_application_character_declaration *, bool *found, qa_error *);
/* Source-services construction reads the active routing candidate when one
 * exists. The receiver must belong to that exact physical provider inventory.
 * Returned declarations borrow the candidate only for the current callback. */
bool qa_application_character_constructor_read(qa_application *, qa_actor_owner receiver,
    uint32_t seat, qa_application_character_declaration *, bool *found, qa_error *);
/* Physical source clients follow this exact choices array order. Seat ids
 * remain authored ids; this does not reinterpret an id as a client index. */
bool qa_application_constructor_seat_ordinal(qa_application *, qa_actor_owner receiver,
    uint32_t seat, uint32_t *physical_ordinal, qa_error *);

/* Explicit launcher constructor defaults from options.ts. The actual model
 * option replaces model/head before all four choices are copied into a seat. */
bool qa_native_q3_character_default_declaration(qa_game_family,
    qa_native_q3_character_declaration *, qa_error *);

/* Retain the declaration already published for this actual viewing seat.
 * The supplied constructor projection must match the selected seat exactly. */
bool qa_native_q3_character_selection_create(qa_application *, uint32_t seat,
    const qa_native_q3_character_declaration *, qa_native_q3_character_selection *, qa_error *);
bool qa_application_character_selection_read(qa_application *, uint32_t seat,
    qa_native_q3_character_selection *, bool *found, qa_error *);

#endif

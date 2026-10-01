#ifndef QA_BOT_SOURCE_WEAPON_SAVE_H
#define QA_BOT_SOURCE_WEAPON_SAVE_H
#include "source_weapon_state.h"
#include "qa/source_save.h"

typedef struct bot_weapon_weight_refs {
    void *context;
    /* Missing is permitted only for a retired weak configuration identity. */
    bool (*reference)(void *,qa_bot_weights *,uint64_t *,bool *,qa_error *);
    bool (*resolve)(void *,uint64_t,qa_bot_weights **,qa_error *);
} bot_weapon_weight_refs;
/* MEMORY and the real weight configuration registry precede these aliases. */
bool bot_weapon_pointer_fields(qa_source_save_io *,qa_bot_memory *,bot_weapon_pointers *,const bot_weapon_weight_refs *);
bool bot_weapon_record_fields(qa_source_save_io *,qa_bot_memory *,bot_weapon_record *);
#endif

#ifndef QA_BOT_SOURCE_WEAPON_CONFIG_H
#define QA_BOT_SOURCE_WEAPON_CONFIG_H
#include "qa/bot_library.h"
#include "qa/bots_allocator.h"

enum {BOT_WEAPON_CONFIG_BYTES=16,BOT_WEAPON_INFO_BYTES=552,BOT_PROJECTILE_INFO_BYTES=208};
typedef struct bot_weapon_config_record {
    qa_bot_memory *memory;
    qa_bot_memory_allocation allocation;
} bot_weapon_config_record;
typedef struct bot_weapon_config_cell {
    bot_weapon_config_record record;
    uint32_t offset,size;
} bot_weapon_config_cell;
/* Allocation publishes its actual identity before writing the source header;
 * a failed header write leaves the reached hunk and bytes in MEMORY. */
bool bot_weapon_config_allocate(qa_bot_memory *,uint32_t,uint32_t,bot_weapon_config_record *,qa_error *);
bool bot_weapon_config_bind(qa_bot_memory *,qa_bot_memory_allocation,bot_weapon_config_record *,qa_error *);
bool bot_weapon_config_header(const bot_weapon_config_record *,bot_weapon_config_cell *,qa_error *);
bool bot_weapon_config_weapon(const bot_weapon_config_record *,uint32_t,bot_weapon_config_cell *,qa_error *);
bool bot_weapon_config_projectile(const bot_weapon_config_record *,uint32_t,bot_weapon_config_cell *,qa_error *);
bool bot_weapon_config_projectile_at(const bot_weapon_config_record *,uint32_t,uint32_t,bot_weapon_config_cell *,qa_error *);
bool bot_weapon_config_projectile_signed(const bot_weapon_config_record *,uint32_t,int32_t,bot_weapon_config_cell *,qa_error *);
bool bot_weapon_config_span(const bot_weapon_config_cell *,qa_bot_memory_span *,qa_error *);
bool bot_weapon_config_clear(const bot_weapon_config_cell *,qa_error *);
bool bot_weapon_config_int(const bot_weapon_config_cell *,uint32_t,int32_t *,qa_error *);
bool bot_weapon_config_set_int(const bot_weapon_config_cell *,uint32_t,int32_t,qa_error *);
bool bot_weapon_config_float(const bot_weapon_config_cell *,uint32_t,float *,qa_error *);
bool bot_weapon_config_set_float(const bot_weapon_config_cell *,uint32_t,float,qa_error *);
bool bot_weapon_config_text(const bot_weapon_config_cell *,uint32_t,char [81],qa_error *);
bool bot_weapon_config_set_text(const bot_weapon_config_cell *,uint32_t,qa_bytes,qa_error *);
bool bot_weapon_config_vector(const bot_weapon_config_cell *,uint32_t,qa_vec3 *,qa_error *);
bool bot_weapon_config_set_vector(const bot_weapon_config_cell *,uint32_t,qa_vec3,qa_error *);
bool bot_weapon_config_embed_projectile(const bot_weapon_config_cell *,const bot_weapon_config_cell *,qa_error *);
/* Parsing owns a temporary native value; this writes its reached source fields
 * in Object.assign order, including partial records on allocation overflow. */
bool bot_weapon_config_store_weapon(const bot_weapon_config_record *,uint32_t,const qa_bot_weapon_info *,qa_error *);
#endif

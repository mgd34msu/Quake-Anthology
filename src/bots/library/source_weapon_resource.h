#ifndef QA_BOT_SOURCE_WEAPON_RESOURCE_H
#define QA_BOT_SOURCE_WEAPON_RESOURCE_H
#include "source_weapon_parse.h"

typedef enum bot_weapon_diagnostic_origin {
    BOT_WEAPON_DIAGNOSTIC_SOURCE,BOT_WEAPON_DIAGNOSTIC_PRINT
} bot_weapon_diagnostic_origin;
typedef struct bot_weapon_diagnostic {
    bot_weapon_diagnostic_origin origin;
    qa_script_diagnostic value;
    char *path,*message;
} bot_weapon_diagnostic;
typedef struct bot_weapon_resource_host {
    void *context;
    bool (*current)(void *,qa_error *);
    bool (*report)(void *,bot_weapon_diagnostic_origin,const qa_script_diagnostic *,qa_error *);
} bot_weapon_resource_host;
typedef struct bot_weapon_acquired_source {
    qa_script_resource value;
    bool owned;
    struct bot_weapon_acquired_source *next;
} bot_weapon_acquired_source;
typedef struct bot_weapon_resource {
    qa_bot_memory *memory;
    qa_script_services services;
    qa_script_options options;
    bot_weapon_resource_host host;
    qa_script *reader;
    bot_weapon_acquired_source *pending;
    bot_weapon_config_record record;
    char *path,*include_path,*date,*time;
    uint8_t *defined;
    uint32_t weapon_count,projectile_count,defined_count;
    bot_weapon_diagnostic *diagnostics;
    size_t diagnostic_count,diagnostic_capacity;
    bool active,attempted,bound,missing_root,report_failed;
    qa_error report_error;
} bot_weapon_resource;
/* Services and host callback contexts belong to the enclosing actual source
 * owner and remain alive until pure disposal. The caller retains this owner
 * before invoking load. Failed service calls
 * preserve its actual reader and reached allocation until pure disposal. */
bool bot_weapon_resource_create(qa_bot_memory *,const qa_script_services *,
    const qa_script_options *,const bot_weapon_resource_host *,bot_weapon_resource **,qa_error *);
void bot_weapon_resource_destroy(bot_weapon_resource *);
bool bot_weapon_resource_load(bot_weapon_resource *,const char *,uint32_t,uint32_t,bool *,qa_error *);
bool bot_weapon_resource_bind(bot_weapon_resource *,bot_weapon_config_record,const char *,qa_error *);
bool bot_weapon_resource_free(bot_weapon_resource *,qa_error *);
bool bot_weapon_resource_current(const bot_weapon_resource *,qa_error *);
qa_script_services bot_weapon_resource_services(bot_weapon_resource *);
/* Membership is captured at binding. Fields and the header capacity remain
 * live source bytes; a later valid-word write does not add an array member. */
bool bot_weapon_resource_weapon(const bot_weapon_resource *,uint32_t,bool *,bot_weapon_config_cell *,qa_error *);
bool bot_weapon_resource_capacity(const bot_weapon_resource *,int32_t *,qa_error *);
bool bot_weapon_resource_projectile_bytes(const bot_weapon_resource *,uint32_t,qa_bot_memory_span *,qa_error *);
#endif

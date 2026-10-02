#ifndef QA_BOT_SOURCE_WEAPON_PARSE_H
#define QA_BOT_SOURCE_WEAPON_PARSE_H
#include "source_weapon_config.h"

typedef struct bot_weapon_parser_host {
    void *context;
    bool (*current)(void *,qa_error *);
    bool (*report)(void *,const qa_script_diagnostic *,qa_error *);
    /* This is the source EOF stage, before validation and projectile fixups. */
    bool (*complete)(void *,qa_error *);
} bot_weapon_parser_host;
/* The owner has opened PC and allocated the true hunk before entering. It
 * retains reached PC/allocation state when a service or byte operation fails.
 * Only a successful grammar-error report sets source_failure. The separate
 * own_failure marks reached pure byte/conversion failures, never callbacks. */
bool bot_weapon_parse(qa_script *,const char *,uint32_t,uint32_t,
    const bot_weapon_config_record *,const bot_weapon_parser_host *,bool *,bool *,qa_error *);
#endif

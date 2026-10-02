#ifndef QA_BOT_ITEMS_SOURCE_H
#define QA_BOT_ITEMS_SOURCE_H
#include "internal.h"
#include "qa/source_save.h"
enum { BOT_ITEM_HEADER_BYTES = 8, BOT_ITEM_BYTES = 236 };
bool bot_items_create(qa_bot_memory *, const char *, size_t, qa_bot_items **, qa_error *);
bool bot_items_project(qa_bot_items *, qa_error *);
bool bot_items_cell(qa_bot_items *, size_t, qa_bot_memory_span *, qa_error *);
bool bot_items_store(qa_bot_items *, size_t, const qa_bot_item_info *, qa_error *);
bool bot_items_count(qa_bot_items *, uint32_t, qa_error *);
bool bot_items_header_count(qa_bot_items *, uint32_t *, qa_error *);
bool bot_items_member(qa_bot_items *, uint32_t, qa_error *);
bool bot_items_members_restore(qa_bot_items *, qa_error *);
bool bot_items_alias_fields(qa_source_save_io *, qa_bot_memory *, const qa_bot_items *,
                           const qa_script_services *, qa_bot_items **);
bool bot_items_standalone_fields(qa_source_save_io *, const qa_bot_items *, qa_bot_items **);
#endif

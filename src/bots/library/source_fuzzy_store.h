#ifndef QA_BOT_SOURCE_FUZZY_STORE_H
#define QA_BOT_SOURCE_FUZZY_STORE_H
#include "source_fuzzy.h"
#include "qa/bot_library.h"

typedef struct bot_fuzzy_store bot_fuzzy_store;
typedef struct bot_fuzzy_diagnostic {
    qa_script_diagnostic value;
    char *path,*message;
} bot_fuzzy_diagnostic;
typedef struct bot_fuzzy_reader {
    qa_script *source;
    bot_fuzzy_store *store;
    qa_script_services services;
    uint64_t generation;
    bot_fuzzy_diagnostic *reported;
    size_t reported_count,reported_capacity;
    bool report_failed,missing_root;
    qa_error report_error;
    struct bot_fuzzy_reader *next;
} bot_fuzzy_reader;
typedef struct bot_fuzzy_owned {
    size_t references;
    bot_fuzzy_store *store;
    bot_fuzzy_config source;
    char *path;
    bot_fuzzy_diagnostic *reported;
    size_t reported_count;
    bool disposed,owned;
    struct bot_fuzzy_owned *next;
} bot_fuzzy_owned;
struct bot_fuzzy_store {
    size_t references;
    qa_bot_library *library;
    bot_fuzzy_heap heap;
    bot_fuzzy_owned *first,*last,*cached[128];
    bot_fuzzy_reader *readers;
    size_t cached_count;
    uint64_t generation;
    bool active,closed;
};
bool bot_fuzzy_store_create(qa_bot_library *,bot_fuzzy_store **,qa_error *);
void bot_fuzzy_owned_retain(bot_fuzzy_owned *);
void bot_fuzzy_owned_release(bot_fuzzy_owned *);
bool bot_fuzzy_owned_open(const bot_fuzzy_owned *,qa_error *);
bool bot_fuzzy_store_load(bot_fuzzy_store *,const char *,bot_fuzzy_owned **,qa_error *);
bool bot_fuzzy_store_load_result(bot_fuzzy_store *,const char *,bot_fuzzy_owned **,bool *source_failure,qa_error *);
bool bot_fuzzy_store_free(bot_fuzzy_store *,bot_fuzzy_owned *,qa_error *);
bool bot_fuzzy_store_shutdown(bot_fuzzy_store *,qa_error *);
/* Retained callback context used by live opening and pure source restoration. */
bool bot_fuzzy_reader_create(bot_fuzzy_store *,uint64_t,bot_fuzzy_reader **,qa_error *);
qa_script_services bot_fuzzy_reader_services(bot_fuzzy_reader *);
/* Pure native disposal drops wrappers/aliases without replaying source frees.
 * The held memory owner remains qualified by its real library disposal. */
void bot_fuzzy_store_dispose(bot_fuzzy_store *);
#endif

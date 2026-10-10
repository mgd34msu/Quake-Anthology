#ifndef QA_Q3_AUTHORED_MENU_CONTEXT_H
#define QA_Q3_AUTHORED_MENU_CONTEXT_H
#include "authored_menu_shared.h"
#include "qa/common_parse.h"
#include "qa/script.h"
#include "qa/source_save.h"
#include <setjmp.h>

typedef struct scrollInfo_s {
    int nextScrollTime, nextAdjustTime, adjustValue, scrollKey;
    float xStart, yStart;
    itemDef_t *item;
    qboolean scrollDir;
} scrollInfo_t;
typedef struct {
    char *command;
    int id, defaultbind1, defaultbind2, bind1, bind2;
} bind_t;
typedef struct keywordHash_s {
    char *keyword;
    qboolean (*func)(itemDef_t *, int);
    struct keywordHash_s *next;
} keywordHash_t;
typedef struct stringDef_s { struct stringDef_s *next; const char *str; } stringDef_t;
typedef enum q3menu_allocation_kind { Q3MENU_STRING, Q3MENU_ITEM, Q3MENU_LIST, Q3MENU_EDIT, Q3MENU_MULTI, Q3MENU_MODEL } q3menu_allocation_kind;
typedef struct q3menu_allocation { uint32_t offset, source_size; q3menu_allocation_kind kind; } q3menu_allocation;
typedef struct q3menu_context {
    displayContextDef_t *display;
    void *owner;
    qa_cvars *cvars;
    qa_cvar_handle developer;
    uint64_t declaration_revision;
    qa_error *error;
    bool failed;
    scrollInfo_t scroll;
    void (*capture_func)(void *);
    void *capture_data;
    itemDef_t *item_capture, *bind_item, *edit_item;
    qboolean waiting_for_key, editing_field, debug, in_handle_key;
    menuDef_t menus[MAX_MENUS], *menu_stack[MAX_OPEN_MENUS];
    int menu_count, open_menu_count, last_list_click_time;
    union { max_align_t align; char bytes[256 * 1024]; } allocation;
    int native_alloc_point;
    q3menu_allocation allocations[8192];
    uint32_t allocation_count;
    jmp_buf allocation_failure;
    bool allocation_guard;
    int alloc_point, out_of_memory, string_pool_index, string_handle_count;
    char string_pool[128 * 1024];
    stringDef_t *string_handles[2048];
    keywordHash_t item_keywords[64], menu_keywords[32];
    keywordHash_t *item_keyword_hash[512], *menu_keyword_hash[512];
    bind_t bindings[64];
    char binding_names[2][32], format_buffers[8][4096];
    unsigned format_cursor;
    rectDef_t corrected_text_rect;
    qa_common_parser common_parser;
    qa_script *sources[64];
    qa_script_services scripts;
    qa_script_options script_options;
    qa_script_defines *global_defines;
    int32_t (*random_integer)(void *);
    char script_date[12], script_time[9];
} q3menu_context;
q3menu_context *q3menu_active(void);
q3menu_context *q3menu_enter(q3menu_context *);
void q3menu_leave(q3menu_context *);
q3menu_context *q3menu_create(displayContextDef_t *, void *, qa_cvars *, qa_error *);
void q3menu_bind_item(q3menu_context *, itemDef_t *);
void q3menu_cvar_text(qa_cvar_handle, const char *, char *, int);
float q3menu_cvar_number(qa_cvar_handle, const char *);
float q3menu_named_cvar_number(const char *);
void q3menu_destroy(q3menu_context *);
void q3menu_reset(q3menu_context *, bool strings);
int q3menu_capture_kind(const q3menu_context *);
bool q3menu_capture_restore(q3menu_context *, int);
void *q3menu_alloc(int, int, q3menu_allocation_kind);
int q3menu_random(void);
#endif

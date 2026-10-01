#ifndef QA_FRONTEND_CONFIG_BINDINGS_H
#define QA_FRONTEND_CONFIG_BINDINGS_H
#include "qa/input.h"
#include "qa/source_save.h"

typedef struct frontend_config_bindings frontend_config_bindings;
typedef struct frontend_config_binding_commands {
    qa_console *console;
    uint64_t owner;
    void *context;
    bool (*current)(void *,const qa_command_context *);
    void (*print)(void *,const char *);
} frontend_config_binding_commands;

/* Dedicated command data has no input device, focus or interactive seat. */
frontend_config_bindings *frontend_config_bindings_create(qa_error *);
bool frontend_config_bindings_destroy(frontend_config_bindings *,qa_error *);
bool frontend_config_bindings_commands(frontend_config_bindings *,const frontend_config_binding_commands *,qa_error *);
bool frontend_config_bindings_clone(const frontend_config_bindings *,frontend_config_bindings **,qa_error *);
bool frontend_config_bindings_config(const frontend_config_bindings *,qa_buffer *,qa_error *);
bool frontend_config_bindings_fields(frontend_config_bindings *,qa_source_save_io *);
#endif

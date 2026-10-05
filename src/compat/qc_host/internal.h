#ifndef QA_QC_GAME_INTERNAL_H
#define QA_QC_GAME_INTERNAL_H
#include "qa/qc_host.h"
#include "qa/binary.h"
#include "qa/text.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
struct qa_qc_game {
    qa_qc_game_options options;
    const qa_qc_program *program;
    qa_qc_game_fields fields;
    qa_qc_instance *vm;
    qa_qc_builtin_binding *bindings;
    size_t binding_count;
    size_t calls;
    bool loading;
};
bool qc_game_fail(qa_error *, qa_status, const char *);
bool qc_game_builtin(void *, qa_qc_instance *, qa_qc_builtin, const char *, qa_error *);
bool qc_game_value(qa_qc_game *, const qa_qc_game_value *, uint32_t words[3], qa_error *);
#endif

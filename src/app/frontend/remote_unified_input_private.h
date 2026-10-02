#ifndef QA_FRONTEND_REMOTE_UNIFIED_INPUT_PRIVATE_H
#define QA_FRONTEND_REMOTE_UNIFIED_INPUT_PRIVATE_H
#include "remote_unified_input.h"
#include "neutral_config.h"
#include "unified_input_command.h"
struct frontend_unified_input {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_remote_unified_prediction *prediction;
    frontend_client_source *client;
    frontend_client_source_view client_view;
    frontend_neutral_config_view configuration;
    qa_executable_recipe *recipe;
    const qa_recipe_provider *movement, *arsenal;
    frontend_unified_command_builder builder, pending_builder;
    qa_unified_input pending;
    qa_seat_input_sample retained_sample;
    uint64_t retained_sequence, last_sequence;
    double command_time, pending_time, retained_elapsed;
    uint32_t epoch;
    bool submitted, has_sample, has_pending, busy;
};
#endif

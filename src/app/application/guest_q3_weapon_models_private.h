#ifndef QA_APPLICATION_GUEST_Q3_WEAPON_MODELS_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_WEAPON_MODELS_PRIVATE_H
#include "guest_q3_weapon_models.h"
#include "qa/id_index.h"

enum {
    APPLICATION_Q3_WEAPON_RECEIPT = 0,
    APPLICATION_Q3_SOURCE_ROW_RECEIPT = 1
};

typedef struct application_q3_weapon_model_record {
    int32_t weapon, handles[APPLICATION_Q3_WEAPON_MODEL_FIELDS];
    uint32_t row;
} application_q3_weapon_model_record;
struct application_q3_weapon_models {
    application_q3_weapon_models_module module;
    qa_qvm_binding binding;
    application_q3_weapon_model_record *records;
    qa_id_index receipts;
    size_t count;
    unsigned calls;
    bool imported;
};
bool application_q3_weapon_models_current(const application_q3_weapon_models *, qa_error *);
bool application_q3_weapon_models_record(application_q3_weapon_models *, int32_t,
    application_q3_weapon_model_record *, const char **gun_path, bool *present, qa_error *);

#endif

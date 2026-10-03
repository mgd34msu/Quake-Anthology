#ifndef APPLICATION_GUEST_QC_ARMOR_H
#define APPLICATION_GUEST_QC_ARMOR_H
#include "guest_qc_profile.h"

typedef struct application_qc_armor_scale {
    uint32_t caller, statement;
    float scale;
} application_qc_armor_scale;
typedef struct application_qc_armor_stage {
    qa_qc_inline_region region;
    uint32_t target, damage;
    bool flag_bits;
    uint32_t flags, no_armor, no_power, no_regular, energy;
    application_qc_armor_scale *scales;
    size_t scale_count;
} application_qc_armor_stage;

bool application_qc_armor_parse(const qa_qc_program *, const qa_json_document *,
    qa_json_id, application_qc_armor_stage *, qa_error *);
bool application_qc_armor_inputs(const qa_qc_program *, const application_qc_call *,
    const application_qc_armor_stage *, bool standalone, qa_error *);
void application_qc_armor_free(application_qc_armor_stage *);

#endif

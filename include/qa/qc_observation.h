#ifndef QA_QC_OBSERVATION_H
#define QA_QC_OBSERVATION_H

#include "qa/qc.h"

/* Reads retained words of an existing full actor binding. No entity projection,
 * allocation, refresh, or guest access callback runs. */
bool qa_qc_actor_observation_slot(const qa_qc_instance *, qa_actor_id,
    uint32_t *physical_slot, qa_error *);
bool qa_qc_actor_observation_float(const qa_qc_instance *, uint32_t physical_slot,
    qa_actor_id, uint32_t field_word, float *, qa_error *);
bool qa_qc_actor_observation_int(const qa_qc_instance *, uint32_t physical_slot,
    qa_actor_id, uint32_t field_word, int32_t *, qa_error *);
bool qa_qc_actor_observation_vector(const qa_qc_instance *, uint32_t physical_slot,
    qa_actor_id, uint32_t field_word, qa_vec3 *, qa_error *);

#endif

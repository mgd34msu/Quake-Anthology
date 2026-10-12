#ifndef QA_QW_OUTPUT_H
#define QA_QW_OUTPUT_H

#include "qa/network_qw_source.h"

/* Source values stay wide through presence/delta decisions. Only the final
 * field write applies the module's original narrowing policy. */
typedef enum qw_output_policy { QW_OUTPUT_CHECKED, QW_OUTPUT_SOURCE } qw_output_policy;
typedef struct qw_output_entity {
    uint32_t number;
    double model, frame, colormap, skin, effects;
    const float *origin, *angles;
    uint8_t alpha, scale;
    bool solid;
} qw_output_entity;
typedef struct qw_output_player {
    uint8_t slot, msec;
    uint16_t flags;
    const float *origin, *velocity;
    double frame, model, skin, effects, weapon_frame;
    const qa_qw_command *command;
} qw_output_player;
typedef void (*qw_output_entity_read_fn)(const void *, size_t, qw_output_entity *);
typedef bool (*qw_output_baseline_read_fn)(const void *, uint32_t, qw_output_entity *);
typedef struct qw_output_packet {
    const void *current, *previous, *baselines;
    size_t count, previous_count;
    qw_output_entity_read_fn read;
    qw_output_baseline_read_fn baseline;
    bool delta;
    uint32_t previous_sequence;
} qw_output_packet;

static inline qw_output_entity qw_output_entity_checked(const qa_q1_entity *row)
{
    return (qw_output_entity){.number=row->number, .model=row->model, .frame=row->frame,
        .colormap=row->colormap, .skin=row->skin, .effects=row->effects,
        .origin=row->origin, .angles=row->angles, .alpha=row->alpha, .scale=row->scale,
        .solid=(row->qw_flags&QA_QW_ENTITY_SOLID)!=0};
}
static inline qw_output_entity qw_output_entity_source(const qa_qw_source_entity *row)
{
    return (qw_output_entity){.number=row->number, .model=row->model, .frame=row->frame,
        .colormap=row->colormap, .skin=row->skin, .effects=row->effects,
        .origin=row->origin, .angles=row->angles,
        .alpha=QA_Q1_ALPHA_DEFAULT, .scale=QA_Q1_SCALE_DEFAULT, .solid=row->solid};
}

bool qw_output_write_baseline(qa_net_writer *, qa_net_protocol_id, qw_output_policy,
    const qw_output_entity *);
bool qw_output_write_player(qa_net_writer *, qa_net_protocol_id, qw_output_policy,
    const qw_output_player *);
bool qw_output_write_stat(qa_net_writer *, uint8_t, double);
bool qw_output_write_entities(qa_net_writer *, qa_net_protocol_id, qw_output_policy,
    const qw_output_packet *);

#endif

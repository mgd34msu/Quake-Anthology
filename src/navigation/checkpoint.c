#include "internal.h"

bool qa_navigation_capture(const qa_navigation *n, qa_nav_checkpoint *out, qa_error *e) {
    if (n == NULL || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing navigation checkpoint input/output");
        return false;
    }
    size_t enabled = 0, blocked = 0, admissions = 0;
    for (size_t i = 0; i < n->graph->view.node_count; ++i)
        if (n->enabled[i] >= 0)
            ++enabled;
    for (size_t i = 0; i < n->graph->view.edge_count; ++i) {
        if (n->blocked[i])
            ++blocked;
        if (!isnan(n->admission_seconds[i]))
            ++admissions;
    }
    if (!nav_reserve((void **)&out->enabled, &out->enabled_capacity, enabled, sizeof(*out->enabled),
                     e) ||
        !nav_reserve((void **)&out->blocked, &out->blocked_capacity, blocked, sizeof(*out->blocked),
                     e) ||
        !nav_reserve((void **)&out->admissions, &out->admission_capacity, admissions,
                     sizeof(*out->admissions), e))
        return false;
    out->version = 1;
    out->map = n->graph->view.map;
    out->generation = n->generation;
    out->world_revision = n->world_revision;
    out->enabled_count = 0;
    out->blocked_count = 0;
    out->admission_count = 0;
    for (size_t i = 0; i < n->graph->view.node_count; ++i)
        if (n->enabled[i] >= 0)
            out->enabled[out->enabled_count++] =
                (qa_nav_saved_area){n->graph->nodes[i].id, n->enabled[i] != 0};
    for (size_t i = 0; i < n->graph->view.edge_count; ++i) {
        if (n->blocked[i])
            out->blocked[out->blocked_count++] = n->graph->edges[i].id;
        if (!isnan(n->admission_seconds[i]))
            out->admissions[out->admission_count++] =
                (qa_nav_saved_admission){n->graph->edges[i].id, n->admission_seconds[i]};
    }
    return true;
}
bool qa_navigation_restore(qa_navigation *n, const qa_nav_checkpoint *state, qa_error *e) {
    if (n == NULL || state == NULL || state->version != 1 ||
        state->map.name != n->graph->view.map.name ||
        state->map.format != n->graph->view.map.format ||
        memcmp(state->map.digest, n->graph->view.map.digest, sizeof(state->map.digest)) != 0 ||
        (state->enabled_count != 0 && state->enabled == NULL) ||
        (state->blocked_count != 0 && state->blocked == NULL) ||
        (state->admission_count != 0 && state->admissions == NULL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "Navigation checkpoint belongs to another map or has invalid tables");
        return false;
    }
    size_t nodes = n->graph->view.node_count, edges = n->graph->view.edge_count;
    int8_t *enabled = malloc(nodes == 0 ? 1 : nodes);
    uint8_t *blocked = calloc(edges == 0 ? 1 : edges, 1);
    float *admissions = malloc((edges == 0 ? 1 : edges) * sizeof(*admissions));
    if (enabled == NULL || blocked == NULL || admissions == NULL) {
        free(enabled);
        free(blocked);
        free(admissions);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Preparing navigation checkpoint restore");
        return false;
    }
    memset(enabled, -1, nodes);
    for (size_t i = 0; i < edges; ++i)
        admissions[i] = NAN;
    bool ok = true;
    for (size_t i = 0; ok && i < state->enabled_count; ++i) {
        uint32_t index = nav_node_index(n->graph, state->enabled[i].id);
        if (index == QA_NAV_NO_INDEX || enabled[index] >= 0) {
            ok = false;
            break;
        }
        enabled[index] = state->enabled[i].enabled ? 1 : 0;
    }
    for (size_t i = 0; ok && i < state->blocked_count; ++i) {
        uint32_t index = nav_edge_index(n->graph, state->blocked[i]);
        if (index == QA_NAV_NO_INDEX || blocked[index]) {
            ok = false;
            break;
        }
        blocked[index] = 1;
    }
    for (size_t i = 0; ok && i < state->admission_count; ++i) {
        uint32_t index = nav_edge_index(n->graph, state->admissions[i].id);
        float seconds = state->admissions[i].seconds;
        if (index == QA_NAV_NO_INDEX || !isnan(admissions[index]) || !isfinite(seconds) ||
            seconds < 0) {
            ok = false;
            break;
        }
        admissions[index] = seconds;
    }
    if (!ok) {
        free(enabled);
        free(blocked);
        free(admissions);
        qa_error_set(e, QA_ERROR_FORMAT, 0,
                     "Unknown, duplicate or invalid navigation checkpoint record");
        return false;
    }
    free(n->enabled);
    free(n->blocked);
    free(n->admission_seconds);
    n->enabled = enabled;
    n->blocked = blocked;
    n->admission_seconds = admissions;
    n->generation = state->generation;
    n->world_revision = state->world_revision;
    ++n->topology_revision;
    nav_estimates_free(n);
    return true;
}
void qa_nav_checkpoint_free(qa_nav_checkpoint *state) {
    if (state == NULL)
        return;
    free(state->enabled);
    free(state->blocked);
    free(state->admissions);
    *state = (qa_nav_checkpoint){0};
}

#include "player_inventory.h"
#include "capture.h"
#include "save_private.h"
#include <limits.h>

enum { HELP_NULL, HELP_EMPTY, HELP_OWNED };
typedef struct player_record {
    qa_actor_id actor;
    qa_q2_player_view view;
    qa_hud_value vitals[3];
    qa_hud_timer timer;
    qa_item_id timer_item;
    char *timer_label, *help_text[2];
    uint8_t help_alias[2];
    qa_hud_score *scores;
    char *names;
    size_t score_count;
    bool view_ready, help, inventory;
} player_record;

static bool actual(const qa_frontend *f, unsigned id)
{
    return f->seats && f->seats[id].frontend == f && f->seats[id].id == id;
}
static void dispose(player_record *row)
{
    free(row->timer_label); free(row->help_text[0]); free(row->help_text[1]);
    free(row->scores); free(row->names); *row = (player_record){0};
}
static bool fail(qa_source_save_io *io, const char *message)
{ return frontend_fail(io->error, QA_ERROR_FORMAT, message); }
static bool int_field(qa_source_save_io *io, int *value)
{
#if INT_MAX > INT32_MAX || INT_MIN < INT32_MIN
    if (io->direction == QA_SOURCE_SAVE_WRITE &&
        ((int64_t)*value < INT32_MIN || (int64_t)*value > INT32_MAX))
        return fail(io, "Player projection integer exceeds its source word");
#endif
    int32_t word = io->direction == QA_SOURCE_SAVE_WRITE ? (int32_t)*value : 0;
    if (!qa_source_save_i32(io, &word)) return false;
#if INT_MAX < INT32_MAX || INT_MIN > INT32_MIN
    if ((int64_t)word < INT_MIN || (int64_t)word > INT_MAX) return false;
#endif
    if (io->direction == QA_SOURCE_SAVE_READ) *value = (int)word;
    return true;
}
static bool float_field(qa_source_save_io *io, float *value)
{ return qa_source_save_f32(io, value) && isfinite(*value); }
static bool vector_field(qa_source_save_io *io, qa_vec3 *value)
{ return qa_source_save_vec3(io, value) && qa_vec_finite(*value); }
static bool view_field(qa_source_save_io *io, qa_q2_player_view *view)
{
    return vector_field(io, &view->angles) && vector_field(io, &view->offset) &&
        vector_field(io, &view->kick_angles) && vector_field(io, &view->gun_angles) &&
        vector_field(io, &view->gun_offset) && float_field(io, &view->blend.x) &&
        float_field(io, &view->blend.y) && float_field(io, &view->blend.z) &&
        float_field(io, &view->blend.w) && float_field(io, &view->fov) &&
        float_field(io, &view->health) && float_field(io, &view->armor) &&
        float_field(io, &view->ammo) && int_field(io, &view->score) &&
        int_field(io, &view->flashes) && int_field(io, &view->layouts) &&
        qa_source_save_i32(io, &view->hit_marker_damage) &&
        view->hit_marker_damage >= INT16_MIN && view->hit_marker_damage <= INT16_MAX &&
        qa_source_save_string(io, &view->selected_item) && qa_source_save_string(io, &view->timer_item) &&
        int_field(io, &view->timer_seconds) && qa_source_save_bool(io, &view->underwater) &&
        qa_source_save_bool(io, &view->spectator);
}
static bool vitals_field(qa_source_save_io *io, qa_hud_value values[3])
{
    static const char *const labels[] = {"Health", "Armor", "Ammo"};
    for (unsigned i = 0; i < 3; ++i) {
        qa_hud_value *value = values + i;
        bool present = value->label != NULL;
        if (io->direction == QA_SOURCE_SAVE_WRITE &&
            (value->icon || (present && strcmp(value->label, labels[i]))))
            return fail(io, "Player vitals leave their actual immutable label projection");
        if (!qa_source_save_bool(io, &present) || !qa_source_save_f64(io, &value->value) ||
            !qa_source_save_f64(io, &value->maximum) || !qa_source_save_bool(io, &value->warning) ||
            !isfinite(value->value) || !isfinite(value->maximum)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            value->label = present ? labels[i] : NULL; value->icon = NULL;
        }
    }
    return true;
}
static bool scores_field(qa_source_save_io *io, player_record *row)
{
    size_t maximum = SIZE_MAX / sizeof(*row->scores);
    if (io->direction == QA_SOURCE_SAVE_READ && maximum > (io->input.size - io->offset) / 10)
        maximum = (io->input.size - io->offset) / 10;
    if (!qa_source_save_count(io, &row->score_count, maximum)) return false;
    size_t bytes = 0;
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        if ((row->score_count != 0) != (row->scores != NULL) ||
            (row->score_count != 0) != (row->names != NULL))
            return fail(io, "Player scores leave their actual copied row and name owners");
        for (size_t i = 0; i < row->score_count; ++i) {
            const qa_hud_score *score = row->scores + i;
            if (score->name != row->names + bytes || score->team)
                return fail(io, "Player score names leave their packed copied owner");
            size_t length = strlen(score->name);
            if (length == SIZE_MAX || length + 1 > SIZE_MAX - bytes)
                return fail(io, "Player copied score names exceed address space");
            bytes += length + 1;
        }
    }
    size_t available = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX;
    if (!qa_source_save_count(io, &bytes, available)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        available = io->input.size - io->offset;
        if (row->score_count > available / 10 || bytes > available - row->score_count * 10 ||
            bytes < row->score_count || (!row->score_count && bytes))
            return fail(io, "Player copied score storage differs from its bounded rows");
        row->scores = row->score_count ? calloc(row->score_count, sizeof(*row->scores)) : NULL;
        row->names = bytes ? malloc(bytes) : NULL;
        if ((row->score_count && !row->scores) || (bytes && !row->names))
            return frontend_fail(io->error, QA_ERROR_MEMORY, "Retaining saved player score rows and names");
    }
    if (!qa_source_save_bytes(io, row->names, bytes)) return false;
    size_t offset = 0;
    for (size_t i = 0; i < row->score_count; ++i) {
        qa_hud_score *score = row->scores + i;
        const char *end = memchr(row->names + offset, 0, bytes - offset);
        if (!end) return fail(io, "Player copied score name has no bounded terminator");
        if (io->direction == QA_SOURCE_SAVE_READ) score->name = row->names + offset;
        offset = (size_t)(end - row->names) + 1;
        if (!qa_source_save_i32(io, &score->score) || !qa_source_save_i32(io, &score->ping) ||
            !qa_source_save_bool(io, &score->local) || !qa_source_save_bool(io, &score->spectator)) return false;
    }
    return offset == bytes || fail(io, "Player copied score names contain unowned trailing bytes");
}
static bool fields(qa_source_save_io *io, player_record *row)
{
    if (io->direction == QA_SOURCE_SAVE_WRITE &&
        (row->timer.icon || row->timer.label != row->timer_label))
        return fail(io, "Player timer label leaves its actual copied owner");
    if (!qa_source_save_actor(io, &row->actor) || !view_field(io, &row->view) ||
        !vitals_field(io, row->vitals) || !qa_source_save_u64(io, &row->timer.until_ns) ||
        !qa_source_save_string(io, &row->timer_item) || !frontend_save_text(io, &row->timer_label)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) row->timer.label = row->timer_label;
    for (unsigned i = 0; i < 2; ++i) {
        if (!frontend_save_text(io, row->help_text + i) ||
            !qa_source_save_u8(io, row->help_alias + i)) return false;
        if (row->help_alias[i] > HELP_OWNED ||
            (row->help_text[i] ? (!*row->help_text[i] || row->help_alias[i] != HELP_OWNED)
                               : row->help_alias[i] == HELP_OWNED))
            return fail(io, "Player help line leaves its actual copied or empty alias");
    }
    return scores_field(io, row) && qa_source_save_bool(io, &row->view_ready) &&
        qa_source_save_bool(io, &row->help) && qa_source_save_bool(io, &row->inventory) &&
        (!row->view_ready || row->actor.registry != 0);
}
static bool header(qa_source_save_io *io, const qa_frontend *f)
{
    uint8_t magic[] = {'Q','F','P','L'};
    uint32_t version = 2, seats = f->options.seats;
    bool dedicated = f->options.dedicated;
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QFPL", sizeof(magic)) &&
        qa_source_save_u32(io, &version) && version == 2 && qa_source_save_u32(io, &seats) &&
        seats == f->options.seats && qa_source_save_bool(io, &dedicated) && dedicated == f->options.dedicated;
}
static bool capture(const frontend_seat *seat, player_record *row, qa_error *error)
{
    *row = (player_record){.actor = seat->q2_actor, .view = seat->q2_view, .timer = seat->q2_timer,
        .timer_item = seat->q2_timer_item, .timer_label = seat->q2_timer_label,
        .help_text = {seat->q2_help_text[0], seat->q2_help_text[1]},
        .scores = seat->q2_scores, .names = seat->q2_score_names, .score_count = seat->q2_score_count,
        .view_ready = seat->q2_view_ready, .help = seat->q2_help, .inventory = seat->q2_inventory};
    memcpy(row->vitals, seat->q2_vitals, sizeof(row->vitals));
    for (unsigned i = 0; i < 2; ++i) {
        if (row->help_text[i]) {
            if (seat->q2_help_lines[i] != row->help_text[i])
                return frontend_fail(error, QA_ERROR_FORMAT, "Actual player help line leaves its owned copy");
            row->help_alias[i] = HELP_OWNED;
        } else if (!seat->q2_help_lines[i]) row->help_alias[i] = HELP_NULL;
        else if (!*seat->q2_help_lines[i]) row->help_alias[i] = HELP_EMPTY;
        else return frontend_fail(error, QA_ERROR_FORMAT, "Actual player help line has no copied owner");
    }
    return true;
}
static void install(frontend_seat *seat, player_record *row)
{
    frontend_player_retire(seat);
    seat->q2_actor = row->actor; seat->q2_view = row->view;
    memcpy(seat->q2_vitals, row->vitals, sizeof(row->vitals));
    seat->q2_timer = row->timer; seat->q2_timer_item = row->timer_item;
    seat->q2_timer_label = row->timer_label;
    for (unsigned i = 0; i < 2; ++i) {
        seat->q2_help_text[i] = row->help_text[i];
        seat->q2_help_lines[i] = row->help_alias[i] == HELP_NULL ? NULL :
            row->help_alias[i] == HELP_EMPTY ? "" : row->help_text[i];
    }
    seat->q2_scores = row->scores; seat->q2_score_names = row->names; seat->q2_score_count = row->score_count;
    seat->q2_view_ready = row->view_ready; seat->q2_help = row->help; seat->q2_inventory = row->inventory;
    *row = (player_record){0};
}
bool frontend_players_checkpoint(qa_frontend *f, qa_buffer *out, qa_error *error)
{
    if (!f || !f->application || !f->capture || f->stepping || !out || out->data || out->size ||
        !f->options.seats || f->options.seats > QA_INPUT_LOCAL_SEATS || !frontend_seat_callbacks_idle(f))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Player projection capture requires its held frontend and empty output");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, qa_application_session(f->application), error) && header(&io, f);
    for (unsigned i = 0; ok && !f->options.dedicated && i < f->options.seats; ++i) {
        player_record row = {0}; uint32_t id = i;
        ok = actual(f, i) && capture(f->seats + i, &row, error) &&
            qa_source_save_u32(&io, &id) && fields(&io, &row);
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK)
        frontend_fail(error, QA_ERROR_FORMAT, "Actual player projection is not completely qualified");
    return ok;
}
bool frontend_players_restore(qa_frontend *f, qa_bytes bytes, qa_error *error)
{
    if (!f || !f->application || !f->source_restoring || f->capture || f->stepping ||
        !f->options.seats || f->options.seats > QA_INPUT_LOCAL_SEATS || !frontend_seat_callbacks_idle(f))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Player projection import requires its isolated stable frontend seats");
    player_record records[QA_INPUT_LOCAL_SEATS] = {0};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, qa_application_session(f->application), bytes, error) && header(&io, f);
    for (unsigned i = 0; ok && !f->options.dedicated && i < f->options.seats; ++i) {
        uint32_t id = i;
        ok = actual(f, i) && qa_source_save_u32(&io, &id) && id == i && fields(&io, records + i);
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    if (ok && !f->options.dedicated)
        for (unsigned i = 0; i < f->options.seats; ++i) install(f->seats + i, records + i);
    for (unsigned i = 0; i < QA_INPUT_LOCAL_SEATS; ++i) dispose(records + i);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK)
        frontend_fail(error, QA_ERROR_FORMAT, "Saved player projection differs from its actual seat owners");
    return ok;
}

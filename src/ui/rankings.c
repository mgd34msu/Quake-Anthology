#include "internal.h"
#include "qa/ui_account_save.h"
#include "qa/application_rankings.h"
#include "qa/source_save.h"
#include <stdio.h>

struct qa_ui_rankings {
    qa_ui *ui;
    qa_application *application;
    qa_ui_id menu;
    int32_t slot;
    bool create, busy;
    char username[513], password[513], email[513], status[256], display[256];
    qa_ui_control controls[9];
};
enum { RANK_USER = 1, RANK_PASSWORD, RANK_EMAIL, RANK_CREATE, RANK_SUBMIT,
       RANK_RESET, RANK_SPECTATE, RANK_BACK, RANK_STATUS };
static void erase(char *text, size_t size) {
    volatile char *bytes = text;
    while (size--) *bytes++ = 0;
}
static void credentials_clear(qa_ui_rankings *menu) {
    erase(menu->username, sizeof(menu->username));
    erase(menu->password, sizeof(menu->password));
    erase(menu->email, sizeof(menu->email));
}
static bool nonempty(const char *text) {
    qa_bytes bytes = {(const uint8_t *)text, strlen(text)};
    size_t offset = 0; uint32_t scalar;
    while (qa_utf8_next(bytes, &offset, &scalar))
        if (!qa_unicode_whitespace(scalar)) return true;
    return false;
}
static void close_menu(void *context, uint32_t seat) {
    qa_ui_rankings *menu = context;
    (void)seat;
    credentials_clear(menu);
    menu->status[0] = 0;
}
static bool action(void *context, uint32_t seat, qa_ui_id control,
                    const qa_ui_action *event, qa_error *error) {
    qa_ui_rankings *menu = context;
    (void)seat;
    if (menu->busy) return true;
    if (event->kind == QA_UI_CHANGE_TEXT) {
        char *target = control == RANK_USER ? menu->username : control == RANK_PASSWORD ? menu->password
                       : control == RANK_EMAIL ? menu->email : NULL;
        if (target) snprintf(target, 513, "%s", event->value.text ? event->value.text : "");
        return true;
    }
    if (control == RANK_CREATE && event->kind == QA_UI_CHANGE_NUMBER) {
        menu->create = event->value.number != 0;
        return true;
    }
    if (event->kind != QA_UI_ACTIVATE) return true;
    if (control == RANK_BACK) return qa_ui_close(menu->ui, menu->ui->time_ms, error);
    qa_rankings *rankings = qa_application_rankings(menu->application);
    if (!rankings || menu->slot < 0) return ui_fail(error, "ranking source slot is unavailable");
    menu->busy = true;
    menu->status[0] = 0;
    bool ok = true;
    if (control == RANK_SUBMIT) {
        char username[513], password[513], email[513];
        memcpy(username, menu->username, sizeof(username));
        memcpy(password, menu->password, sizeof(password));
        memcpy(email, menu->email, sizeof(email));
        qa_ranking_request request = {.kind = menu->create ? QA_RANKING_CREATE_ACCOUNT : QA_RANKING_LOGIN,
                                      .username = username, .password = password, .email = email};
        erase(menu->password, sizeof(menu->password));
        ok = qa_application_rankings_account(menu->application, menu->slot, &request, error);
        erase(username, sizeof(username)); erase(password, sizeof(password)); erase(email, sizeof(email));
    } else if (control == RANK_RESET) ok = qa_application_rankings_reset(menu->application, menu->slot, error);
    else if (control == RANK_SPECTATE) {
        credentials_clear(menu);
        ok = qa_application_rankings_spectate(menu->application, menu->slot, error);
    }
    if (!ok) snprintf(menu->status, sizeof(menu->status), "%s",
        error ? error->message : "Ranking account operation failed");
    menu->busy = false;
    return ok;
}
static bool factory(void *context, uint32_t seat, qa_ui_menu *out, qa_error *error) {
    qa_ui_rankings *menu = context;
    (void)seat; (void)error;
    qa_rankings *rankings = qa_application_rankings(menu->application);
    qa_ranking_state service = qa_rankings_state(rankings);
    qa_ranking_player player = qa_rankings_player(rankings, menu->slot);
    bool active = rankings && menu->slot >= 0 && service.kind == QA_RANKING_ACTIVE;
    bool editable = active && !menu->busy && player.kind != QA_RANKING_ACTIVE_PLAYER &&
                    player.kind != QA_RANKING_PENDING_PLAYER;
    for (size_t i = 0; i < 9; ++i)
        menu->controls[i] = (qa_ui_control){.id = i + 1, .kind = QA_UI_BUTTON,
            .enabled = true, .visible = true, .context = menu, .action = action,
            .rect = {96, 84 + (float)i * 40, 448, 28}};
    const char *labels[] = {"Username", "Password", "Email (new account)"};
    const char *values[] = {menu->username, menu->password, menu->email};
    for (size_t i = 0; i < 3; ++i) {
        menu->controls[i].kind = QA_UI_FIELD;
        menu->controls[i].label = labels[i];
        menu->controls[i].enabled = editable;
        menu->controls[i].value.field.text = values[i];
        menu->controls[i].value.field.maximum = 128;
        menu->controls[i].value.field.masked = i == 1;
    }
    menu->controls[3].kind = QA_UI_TOGGLE;
    menu->controls[3].label = "Create a new account";
    menu->controls[3].enabled = editable;
    menu->controls[3].value.checked = menu->create;
    menu->controls[4].label = menu->create ? "Create account" : "Sign in";
    menu->controls[4].enabled = editable && nonempty(menu->username) && *menu->password &&
                              (!menu->create || nonempty(menu->email));
    menu->controls[5].label = "Reset account status";
    menu->controls[5].enabled = active && !menu->busy &&
        (player.kind == QA_RANKING_DENIED_PLAYER || player.kind == QA_RANKING_SPECTATOR);
    menu->controls[6].label = "Spectate / sign out";
    menu->controls[6].enabled = active && !menu->busy && player.kind != QA_RANKING_PENDING_PLAYER;
    menu->controls[7].label = "Back";
    menu->controls[7].rect.y = 438;
    if (!menu->status[0]) {
        const char *status = !rankings || menu->slot < 0 ? "No ranking account service for this player"
            : service.kind == QA_RANKING_DISABLED ? "Rankings are disabled for this match"
            : service.kind == QA_RANKING_UNAVAILABLE ? service.reason
            : service.kind != QA_RANKING_ACTIVE || menu->busy || player.kind == QA_RANKING_PENDING_PLAYER
              ? "Contacting ranking provider..."
            : player.kind == QA_RANKING_DENIED_PLAYER ? player.reason
            : player.kind == QA_RANKING_SPECTATOR ? "Spectating. Sign in to play ranked"
            : player.kind == QA_RANKING_ACTIVE_PLAYER ? "Signed in" : "Sign in or create an account";
        if (active && player.kind == QA_RANKING_ACTIVE_PLAYER)
            snprintf(menu->display, sizeof(menu->display), "Signed in. Rank: %.6g", player.account.rank);
        else snprintf(menu->display, sizeof(menu->display), "%s", status);
        menu->controls[8].label = menu->display;
    } else menu->controls[8].label = menu->status;
    menu->controls[8].enabled = false;
    menu->controls[8].rect = (qa_scene_rect_f){48, 390, 544, 40};
    *out = (qa_ui_menu){.id = menu->menu, .title = "Ranking account", .controls = menu->controls,
                        .count = 9, .fullscreen = false};
    return true;
}
bool qa_ui_rankings_create(qa_ui *ui, qa_application *application, qa_ui_id id, int32_t slot,
                           qa_ui_rankings **out, qa_error *error) {
    if (!ui || !application || !id || !out || slot < -1)
        return ui_fail(error, "invalid ranking account menu owner");
    qa_ui_rankings *menu = calloc(1, sizeof(*menu));
    if (!menu) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating ranking account menu"); return false; }
    menu->ui = ui; menu->application = application; menu->menu = id; menu->slot = slot;
    if (!qa_ui_register(ui, &(qa_ui_menu_registration){.id = id, .context = menu,
        .factory = factory, .close = close_menu}, error)) { free(menu); return false; }
    *out = menu;
    return true;
}
bool qa_ui_rankings_set_slot(qa_ui_rankings *menu, int32_t slot, qa_error *error) {
    if (!menu || menu->busy || menu->ui->handling || slot < -1)
        return ui_fail(error, "cannot replace ranking account slot during callbacks");
    if (menu->slot == slot) return true;
    credentials_clear(menu);
    menu->status[0] = 0; menu->slot = slot;
    return true;
}
bool qa_ui_rankings_reset_binding(qa_ui_rankings *menu, qa_error *error) {
    if (!menu || menu->busy || menu->ui->handling)
        return ui_fail(error, "cannot reset ranking account binding during callbacks");
    credentials_clear(menu);
    menu->status[0] = 0; menu->slot = -1;
    return true;
}
bool qa_ui_rankings_destroy(qa_ui_rankings *menu, double time, qa_error *error) {
    if (!menu) return true;
    if (menu->busy || menu->ui->handling) return ui_fail(error, "ranking account callback is active");
    if (!qa_ui_unregister(menu->ui, menu->menu, time, error)) return false;
    credentials_clear(menu); free(menu);
    return true;
}
static bool checkpoint_fields(qa_source_save_io *io, qa_ui_rankings *saved,
                               const qa_ui_rankings *qualified) {
    uint8_t magic[4] = {'Q','R','U','I'};
    uint32_t seat = qualified->ui->options.seat;
    uint64_t menu = qualified->menu;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QRUI", sizeof(magic)) ||
        !qa_source_save_u32(io, &seat) || seat != qualified->ui->options.seat ||
        !qa_source_save_u64(io, &menu) || menu != qualified->menu ||
        !qa_source_save_i32(io, &saved->slot) || saved->slot < -1 ||
        !qa_source_save_bool(io, &saved->create)) return false;
    char *strings[] = {saved->username, saved->password, saved->email, saved->status, saved->display};
    const size_t sizes[] = {sizeof(saved->username), sizeof(saved->password), sizeof(saved->email),
        sizeof(saved->status), sizeof(saved->display)};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i)
        if (!qa_source_save_bytes(io, strings[i], sizes[i]) || !memchr(strings[i], 0, sizes[i])) return false;
    return true;
}
bool qa_ui_rankings_checkpoint(const qa_ui_rankings *menu, qa_buffer *out, qa_error *error) {
    if (!menu || !out || out->data || out->size || menu->busy || menu->ui->handling || menu->ui->drawing)
        return ui_fail(error, "ranking menu capture requires idle actual owners and empty output");
    qa_ui_rankings saved = *menu;
    qa_source_save_io io = {0};
    bool success = qa_source_save_writer(&io, NULL, error) && checkpoint_fields(&io, &saved, menu) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    credentials_clear(&saved);
    if (!success && error && error->code == QA_OK)
        qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid ranking menu continuation");
    return success;
}
bool qa_ui_rankings_restore(qa_ui_rankings *menu, qa_bytes bytes, qa_error *error) {
    if (!menu || menu->busy || menu->ui->handling || menu->ui->drawing)
        return ui_fail(error, "ranking menu restore requires idle actual owners");
    qa_ui_rankings saved = {.ui = menu->ui, .application = menu->application, .menu = menu->menu};
    qa_source_save_io io = {0};
    bool success = qa_source_save_reader(&io, NULL, bytes, error) && checkpoint_fields(&io, &saved, menu) &&
        qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!success) {
        credentials_clear(&saved);
        if (error && error->code == QA_OK) qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid ranking menu continuation");
        return false;
    }
    qa_ui_rankings displaced = *menu;
    *menu = saved;
    credentials_clear(&displaced); credentials_clear(&saved);
    return true;
}

/* Port of src/app/bootstrap/player-death.ts. The async restart
 * becomes the landed service's synchronous error-string call.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_PLAYER_DEATH_H
#define QA_APP_BOOTSTRAP_PLAYER_DEATH_H
#include <stdbool.h>
#include "qa/contracts/ui.h"
#include "qa/ui/common/controller.h"
#include "qa/ui/saves/menu.h"
extern const qa_ui_menu_id qa_app_player_death_menu; /* "menu:application:death" */
typedef struct qa_app_seat_player_death qa_app_seat_player_death;
/* saves NULL = TS undefined. quit is fn+ctx. */
qa_app_seat_player_death *qa_app_seat_player_death_create(qa_native_ui_controller *controller,
    const qa_ui_saved_game_menu_service *saves, qa_ui_menu_id load_menu,
    void (*quit)(void *ctx), void *quit_ctx);
bool qa_app_seat_player_death_active(const qa_app_seat_player_death *death);
void qa_app_seat_player_death_observe(qa_app_seat_player_death *death, double health);
bool qa_app_seat_player_death_input(qa_app_seat_player_death *death, qa_seat_input_event event);
void qa_app_seat_player_death_close(qa_app_seat_player_death *death);
#endif

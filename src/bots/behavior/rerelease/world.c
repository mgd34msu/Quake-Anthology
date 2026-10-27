/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/world.h"
#include <string.h>
double qa_bot_ammo_get(const qa_bot_ammo_entry *ammo, size_t count, const char *name) { for (size_t i = 0; i < count; i++) if (ammo[i].name && !strcmp(ammo[i].name, name)) return ammo[i].value; return 0; }
qa_bot_usercmd qa_empty_usercmd(void) { qa_bot_usercmd c; memset(&c, 0, sizeof c); return c; }

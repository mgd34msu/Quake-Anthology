#ifndef QA_APPLICATION_BOTS_GUESTS_H
#define QA_APPLICATION_BOTS_GUESTS_H
#include "bots_private.h"

bool application_bots_guests_create(application_bots *,const qa_bot_runtime_options *,qa_error *);
bool application_bots_guests_can_destroy(const application_bots *);
bool application_bots_guests_destroy(application_bots *,qa_error *);
bool application_bots_guest_fields(qa_source_save_io *,application_bot_guest *);
bool application_bots_guests_construct_restored(application_bots *,qa_error *);
bool application_bots_guests_restore(application_bots *,qa_error *);
void application_bots_guests_finish(application_bots *);
#endif

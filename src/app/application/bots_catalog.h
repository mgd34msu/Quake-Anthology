#ifndef QA_APPLICATION_BOTS_CATALOG_H
#define QA_APPLICATION_BOTS_CATALOG_H
#include "bots_private.h"
#include "qa/bots_catalog.h"

bool application_bots_catalog_create(application_bots *,qa_error *);
bool application_bots_catalog_initialize(application_bots *,bool,qa_error *);
bool application_bots_catalog_check_spawn(void *,qa_error *);
bool application_bots_spawn_admitted(const qa_application *);
bool application_bots_admission_seat(const qa_application *,const qa_launch_seat **);
bool application_bots_catalog_console(qa_application *,const qa_command_invocation *,bool *,qa_error *);
bool application_bots_catalog_add(qa_application *,const qa_bot_catalog_add_request *,qa_error *);
bool application_bots_catalog_add_seat(qa_application *,const qa_launch_seat *,const qa_bot_catalog_add_request *,qa_error *);
bool application_bots_catalog_remove_begin(qa_application *,uint32_t,qa_error *);
bool application_bots_catalog_character(qa_application *,const char *,char *,size_t,qa_error *);
#endif

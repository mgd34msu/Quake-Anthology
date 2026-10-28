/* Port of src/app/bootstrap/prepared-startup.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/prepared_startup.h"
#include <stddef.h>
#include <stdlib.h>
#include "qa/foundation/support.h"
bool qa_app_allow_seat_configuration_command(qa_command_context source) {
  const qa_command_origin *origin = &source.origin;
  while (origin != NULL && origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  if (origin == NULL) return true;
  return origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT
      || origin->kind == QA_COMMAND_ORIGIN_SERVER_CONSOLE;
}
struct qa_app_prepared_startup {
  qa_command_buffer *commands;
  qa_app_prepared_startup_host host;
  qa_app_prepared_seat *seats; size_t seat_count;
  bool closed;
};
qa_app_prepared_startup *qa_app_prepared_startup_create(qa_command_buffer *commands,
    qa_app_prepared_startup_host host) {
  qa_app_prepared_startup *startup = calloc(1, sizeof *startup);
  if (startup == NULL) qa_range_error("Out of memory");
  startup->commands = commands;
  startup->host = host;
  return startup;
}
qa_command_buffer *qa_app_prepared_startup_commands(const qa_app_prepared_startup *startup) {
  return startup->commands;
}
const qa_app_prepared_seat *qa_app_prepared_startup_seats(const qa_app_prepared_startup *startup, size_t *out_count) {
  *out_count = startup->seat_count;
  return startup->seats;
}
void qa_app_prepared_startup_close(qa_app_prepared_startup *startup) {
  if (startup == NULL || startup->closed) return;
  startup->closed = true;
  free(startup->seats);
  free(startup);
}

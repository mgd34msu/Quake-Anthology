/* Port of src/app/bootstrap/weapon-behavior-tool.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/weapon_behavior_tool.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/foundation/support.h"
#include "qa/input/support.h"
void qa_app_run_weapon_behavior_tool(qa_app_weapon_behavior_tool_host host, qa_app_weapon_behavior_tool_command command,
                                     void (*print)(void *ctx, const char *text), void *print_ctx) {
  if (strcmp(command.action, "help") == 0) { print(print_ctx, qa_app_weapon_behavior_tool_help); return; }
  const char *family = NULL, *edition = NULL, *expectation_id = NULL, *content_directory = NULL, *product_id = NULL;
  const void *content = host.open_product(host.ctx, &command, &family, &edition, &expectation_id, &content_directory, &product_id);
  (void)expectation_id; (void)content_directory; (void)product_id;
  const char *error = NULL;
  if (strcmp(family, "q2") == 0 && strcmp(edition, "rerelease") != 0)
    qa_range_error("Native weapon declarations currently require the Q2 rerelease API2023 Windows x64 adapter");
  if (strcmp(family, "q2") == 0) error = host.run_native(host.ctx, &command, content, print, print_ctx);
  else if (strcmp(family, "q3") == 0) error = host.run_qvm(host.ctx, &command, content, print, print_ctx);
  else {
    if (strcmp(command.action, "declare-qvm") == 0 || strcmp(command.action, "declare-native") == 0) {
      host.close_product(host.ctx, content);
      qa_range_error("%s requires its matching Q3 or Q2 rerelease provider", command.action);
    }
    error = host.run_qc(host.ctx, &command, content, print, print_ctx);
  }
  host.close_product(host.ctx, content);
  if (error != NULL) qa_range_error("%s", error);
}
char *qa_app_write_behavior_document(const char *directory, const char *name, const char *bytes) {
  /* mkdir -p (single level walk). */
  char *walk = qa_input_dup(directory);
  if (!walk) qa_range_error("Out of memory");
  for (char *p = walk + 1; *p; p++) {
    if (*p == '/') { *p = '\0'; mkdir(walk, 0777); *p = '/'; }
  }
  mkdir(walk, 0777);
  free(walk);
  size_t dir_len = strlen(directory), name_len = strlen(name);
  char *destination = malloc(dir_len + 1 + name_len + 1);
  char *temporary = malloc(dir_len + 64);
  if (!destination || !temporary) qa_range_error("Out of memory");
  snprintf(destination, dir_len + 1 + name_len + 1, "%s/%s", directory, name);
  snprintf(temporary, dir_len + 64, "%s/.weapon-behaviors-%ld.tmp", directory, (long)getpid());
  int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) qa_range_error("Cannot write behavior document: %s", strerror(errno));
  size_t len = strlen(bytes), written = 0;
  while (written < len) {
    ssize_t n = write(fd, bytes + written, len - written);
    if (n < 0) { close(fd); unlink(temporary); qa_range_error("Cannot write behavior document: %s", strerror(errno)); }
    written += (size_t)n;
  }
  fsync(fd);
  close(fd);
  if (rename(temporary, destination) != 0) {
    unlink(temporary);
    qa_range_error("Cannot write behavior document: %s", strerror(errno));
  }
  unlink(temporary);
  free(temporary);
  return destination;
}

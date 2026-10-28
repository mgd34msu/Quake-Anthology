/* Port of src/app/bootstrap/client-bootstrap.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/client_bootstrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const char qa_app_client_source_publication_error[] = "Client source failed after publication began";
typedef struct { const qa_identity_owner *identity; qa_app_local_account account; } account_row;
struct qa_app_local_accounts {
  void (*random_bytes)(void *ctx, unsigned char out[16]);
  void *random_ctx;
  account_row *rows; size_t count, cap;
};
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
qa_app_local_accounts *qa_app_local_accounts_create(void (*random_bytes)(void *ctx, unsigned char out[16]), void *random_ctx) {
  qa_app_local_accounts *accounts = calloc(1, sizeof *accounts);
  if (!accounts) return NULL;
  accounts->random_bytes = random_bytes;
  accounts->random_ctx = random_ctx;
  return accounts;
}
void qa_app_local_accounts_destroy(qa_app_local_accounts *accounts) {
  if (!accounts) return;
  for (size_t i = 0; i < accounts->count; i++) { free(accounts->rows[i].account.id); free(accounts->rows[i].account.name); }
  free(accounts->rows);
  free(accounts);
}
const qa_app_local_account *qa_app_local_client_account(qa_app_local_accounts *accounts, const qa_identity_owner *identity) {
  for (size_t i = 0; i < accounts->count; i++)
    if (accounts->rows[i].identity == identity) return &accounts->rows[i].account;
  unsigned char bytes[16];
  accounts->random_bytes(accounts->random_ctx, bytes);
  char id[48];
  size_t len = (size_t)snprintf(id, sizeof id, "account:");
  for (size_t i = 0; i < 16; i++) len += (size_t)snprintf(id + len, sizeof id - len, "%02x", bytes[i]);
  if (accounts->count == accounts->cap) {
    accounts->cap = accounts->cap ? accounts->cap * 2 : 8;
    accounts->rows = realloc(accounts->rows, accounts->cap * sizeof *accounts->rows);
  }
  account_row *row = &accounts->rows[accounts->count++];
  row->identity = identity;
  row->account.id = dup(id);
  row->account.name = dup(identity->session.name ? identity->session.name : "");
  return &row->account;
}

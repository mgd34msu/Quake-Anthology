/*
 * Port of src/contracts/mod-client-presentation.ts (types only).
 * String unions keep their exact TS strings: Qc hud
 * "none"|"replace-vitals", view "none"|"set-view"; Native hud
 * "none"|"layout-overlay"|"replace-status", view "none"|"playerstate".
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_MOD_CLIENT_PRESENTATION_H
#define QA_CONTRACTS_MOD_CLIENT_PRESENTATION_H

typedef struct qa_qc_mod_client_presentation {
  const char *hud;
  const char *view;
} qa_qc_mod_client_presentation;

typedef struct qa_native_mod_client_presentation {
  const char *hud;
  const char *view;
} qa_native_mod_client_presentation;

#endif

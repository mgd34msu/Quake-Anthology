/*
 * Port of src/contracts/common.ts (types only): command dialects,
 * origins/contexts, and cvar policies. TS `symbol` instance fields
 * become opaque identity pointers; recursive CommandOrigin "script"
 * callers are held by pointer. GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_COMMON_H
#define QA_CONTRACTS_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"

/* TS CommandDialect (exact strings). */
typedef enum qa_command_dialect {
  QA_COMMAND_DIALECT_Q1_NETQUAKE,    /* "q1-netquake" */
  QA_COMMAND_DIALECT_Q1_QUAKEWORLD,  /* "q1-quakeworld" */
  QA_COMMAND_DIALECT_Q2_CLASSIC,     /* "q2-classic" */
  QA_COMMAND_DIALECT_Q2_RERELEASE,   /* "q2-rerelease" */
  QA_COMMAND_DIALECT_Q3              /* "q3" */
} qa_command_dialect;

/* TS CommandOrigin union on kind. */
typedef enum qa_command_origin_kind {
  QA_COMMAND_ORIGIN_LOCAL_CONSOLE,  /* "local-console" */
  QA_COMMAND_ORIGIN_SERVER_CONSOLE, /* "server-console" */
  QA_COMMAND_ORIGIN_LOCAL_SEAT,     /* "local-seat" */
  QA_COMMAND_ORIGIN_REMOTE_CLIENT,  /* "remote-client" */
  QA_COMMAND_ORIGIN_SCRIPT         /* "script" */
} qa_command_origin_kind;
typedef struct qa_command_origin qa_command_origin;
struct qa_command_origin {
  qa_command_origin_kind kind;
  qa_seat_id seat;                  /* local-seat */
  qa_client_id client;              /* local-seat / remote-client */
  const char *name;                 /* script */
  const qa_command_origin *caller;  /* script */
};

/* TS CommandContext.producer: {kind:"game-module"|"client-module",
 * module, instance?: symbol}. */
typedef struct qa_command_producer {
  const char *kind;
  qa_module_identity module;
  const void *instance; /* TS symbol, nullable */
} qa_command_producer;

typedef struct qa_command_context {
  qa_session_id session;
  qa_command_origin origin;
  const qa_command_producer *producer; /* optional */
} qa_command_context;

/* TS CvarInfoTarget = "client-userinfo" | "server-info". */
typedef enum qa_cvar_info_target {
  QA_CVAR_INFO_CLIENT_USERINFO, QA_CVAR_INFO_SERVER_INFO
} qa_cvar_info_target;

#endif

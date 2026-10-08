#ifndef QA_GAME_DOMAINS_H
#define QA_GAME_DOMAINS_H

#include "qa/console.h"
#include "qa/movement.h"
#include "qa/scheduler.h"

static inline qa_console_dialect qa_movement_console_dialect(qa_movement_kind kind)
{
    switch (kind) {
    case QA_MOVEMENT_NETQUAKE: return QA_CONSOLE_Q1;
    case QA_MOVEMENT_QUAKEWORLD: return QA_CONSOLE_QW;
    case QA_MOVEMENT_Q2_CLASSIC: return QA_CONSOLE_Q2;
    case QA_MOVEMENT_Q2_RERELEASE: return QA_CONSOLE_Q2_RERELEASE;
    case QA_MOVEMENT_Q3: return QA_CONSOLE_Q3;
    }
    return QA_CONSOLE_Q1;
}

static inline qa_movement_kind qa_console_movement_kind(qa_console_dialect dialect)
{
    switch (dialect) {
    case QA_CONSOLE_Q1: return QA_MOVEMENT_NETQUAKE;
    case QA_CONSOLE_QW: return QA_MOVEMENT_QUAKEWORLD;
    case QA_CONSOLE_Q2: return QA_MOVEMENT_Q2_CLASSIC;
    case QA_CONSOLE_Q2_RERELEASE: return QA_MOVEMENT_Q2_RERELEASE;
    case QA_CONSOLE_Q3: return QA_MOVEMENT_Q3;
    }
    return QA_MOVEMENT_NETQUAKE;
}

static inline qa_movement_kind qa_clock_movement_kind(qa_clock_kind kind)
{
    switch (kind) {
    case QA_CLOCK_NETQUAKE: return QA_MOVEMENT_NETQUAKE;
    case QA_CLOCK_QUAKEWORLD: return QA_MOVEMENT_QUAKEWORLD;
    case QA_CLOCK_Q2_CLASSIC: return QA_MOVEMENT_Q2_CLASSIC;
    case QA_CLOCK_Q2_RERELEASE: return QA_MOVEMENT_Q2_RERELEASE;
    case QA_CLOCK_Q3: return QA_MOVEMENT_Q3;
    }
    return QA_MOVEMENT_NETQUAKE;
}

static inline qa_console_dialect qa_clock_console_dialect(qa_clock_kind kind)
{
    return qa_movement_console_dialect(qa_clock_movement_kind(kind));
}

#endif

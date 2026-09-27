#include "internal.h"

qa_nav_travel qa_nav_aas_travel(uint32_t type) {
    static const qa_nav_travel modes[] = {
        QA_NAV_UNKNOWN,     QA_NAV_UNKNOWN,     QA_NAV_WALK,     QA_NAV_CROUCH,
        QA_NAV_JUMP,        QA_NAV_JUMP,        QA_NAV_LADDER,   QA_NAV_DROP,
        QA_NAV_SWIM,        QA_NAV_WATER_JUMP,  QA_NAV_TELEPORT, QA_NAV_MOVER,
        QA_NAV_ROCKET_JUMP, QA_NAV_BFG_JUMP,    QA_NAV_GRAPPLE,  QA_NAV_DOUBLE_JUMP,
        QA_NAV_RAMP_JUMP,   QA_NAV_STRAFE_JUMP, QA_NAV_JUMP_PAD, QA_NAV_MOVER};
    type &= UINT32_C(0x00ffffff);
    return type < sizeof(modes) / sizeof(*modes) ? modes[type] : QA_NAV_UNKNOWN;
}
qa_nav_travel qa_nav_kex_travel(uint32_t type) {
    static const qa_nav_travel modes[] = {
        QA_NAV_WALK,   QA_NAV_JUMP,  QA_NAV_TELEPORT, QA_NAV_DROP,        QA_NAV_JUMP_PAD,
        QA_NAV_JUMP,   QA_NAV_MOVER, QA_NAV_MOVER,    QA_NAV_JUMP,        QA_NAV_CROUCH,
        QA_NAV_LADDER, QA_NAV_JUMP,  QA_NAV_JUMP,     QA_NAV_ROCKET_JUMP, QA_NAV_UNKNOWN};
    return type < sizeof(modes) / sizeof(*modes) ? modes[type] : QA_NAV_UNKNOWN;
}
uint32_t qa_nav_aas_travel_flag(uint32_t type) {
    type &= UINT32_C(0x00ffffff);
    return type < 2 || type > 19 ? 1
           : type == 19          ? UINT32_C(0x01000000)
                                 : UINT32_C(1) << (type >= 7 ? type : type - 1);
}
uint32_t qa_nav_area_travel_flags(const qa_aas_setting *s) {
    uint32_t c = (uint32_t)s->contents;
    return ((c & 1) != 0   ? UINT32_C(0x00100000)
            : (c & 4) != 0 ? UINT32_C(0x00200000)
            : (c & 2) != 0 ? UINT32_C(0x00400000)
                           : UINT32_C(0x00080000)) |
           ((c & 256) != 0 ? UINT32_C(0x00800000) : 0) |
           ((c & 2048) != 0 ? UINT32_C(0x08000000) : 0) |
           ((c & 4096) != 0 ? UINT32_C(0x10000000) : 0) |
           ((s->flags & 16) != 0 ? UINT32_C(0x04000000) : 0);
}
uint32_t qa_nav_edge_travel_flag(const qa_nav_edge *edge) {
    if (edge->source.kind == QA_NAV_ORIGIN_AAS)
        return qa_nav_aas_travel_flag(edge->source_travel_type);
    static const uint32_t flags[] = {2,     4,     16,       128,    256,  512,
                                     32,    1024,  16779264, 262144, 4096, 8192,
                                     16384, 32768, 65536,    131072, 1};
    return (unsigned)edge->mode < QA_NAV_TRAVEL_COUNT ? flags[edge->mode] : 1;
}

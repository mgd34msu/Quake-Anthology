/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QA_Q1_GEOMETRY_H
#define QA_Q1_GEOMETRY_H

#include "qa/arena.h"
#include "qa/math.h"

/* Derived cells use doubles: their winding tolerances are much smaller than
 * one float ULP at ordinary map coordinates. Simulation hulls remain floats. */
typedef struct q1v { double x, y, z; } q1v;
typedef struct q1p { q1v normal; double distance; } q1p;
typedef struct q1bounds { q1v min, max; } q1bounds;
typedef struct q1face { q1p plane; q1v *vertices; size_t count; } q1face;
typedef struct q1cell { q1face *faces; size_t count; q1bounds bounds; } q1cell;
typedef struct q1work { qa_arena *arena; qa_error *error; bool failed; } q1work;
typedef struct q1cells { q1cell **items; size_t count, capacity; } q1cells;
typedef struct q1planes { q1p *items; size_t count, capacity; } q1planes;
typedef struct q1shape { bool capsule; q1v axes[3], extents; double radius, half_segment; } q1shape;
typedef struct q1interval { double enter, exit, contact; q1p plane; int32_t contents; size_t order; } q1interval;

static inline q1v qv(double x, double y, double z) { return (q1v){x,y,z}; }
static inline q1v qadd(q1v a,q1v b) { return qv(a.x+b.x,a.y+b.y,a.z+b.z); }
static inline q1v qsub(q1v a,q1v b) { return qv(a.x-b.x,a.y-b.y,a.z-b.z); }
static inline q1v qscale(q1v a,double s) { return qv(a.x*s,a.y*s,a.z*s); }
static inline double qdot(q1v a,q1v b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline q1v qcross(q1v a,q1v b) { return qv(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x); }
static inline double qlength(q1v a) { return sqrt(qdot(a,a)); }
static inline q1v qlerp(q1v a,q1v b,double f) { return qadd(a,qscale(qsub(b,a),f)); }
static inline q1v qfrom(qa_vec3 a) { return qv(a.x,a.y,a.z); }
static inline qa_vec3 qto(q1v a) { return qa_v3((float)a.x,(float)a.y,(float)a.z); }
static inline q1p qnegate(q1p p) { return (q1p){qscale(p.normal,-1),-p.distance}; }
static inline q1bounds qbounds(qa_bounds b) { return (q1bounds){qfrom(b.mins),qfrom(b.maxs)}; }

void *q1_alloc(q1work *,size_t count,size_t element_size,size_t alignment);
bool q1_grow(q1work *,void **,size_t *capacity,size_t needed,size_t element_size,size_t alignment);
bool q1_cells_push(q1work *,q1cells *,q1cell *);
q1cell *q1_box(q1work *,q1bounds);
void q1_split(q1work *,q1cell *,q1p,q1cell **front,q1cell **back);
q1cell *q1_clip(q1work *,q1cell *,q1p);
bool q1_separating_planes(q1work *,const q1cell *,const q1v axes[3],q1planes *);
double q1_shape_support(const q1shape *,q1v);
bool q1_sweep_cell(q1work *,const q1cell *,q1v start,q1v end,const q1shape *,q1interval *);

#endif

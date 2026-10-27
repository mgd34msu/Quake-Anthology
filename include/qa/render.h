/* Nesting accommodates render barrels: umbrella for the render slice
 * headers ported in this step (cpu/gl/commands + top-level modules).
 * Scene headers join when that slice lands. GPL-2.0-or-later. */
#ifndef QA_RENDER_H
#define QA_RENDER_H
#include "qa/render/commands/index.h"
#include "qa/render/cpu/index.h"
#include "qa/render/debug_graph.h"
#include "qa/render/execution.h"
#include "qa/render/gl/index.h"
#include "qa/render/image_journal.h"
#include "qa/render/output_gamma.h"
#include "qa/render/q3_hardware.h"
#include "qa/render/worker.h"
#include "qa/render/worker_entry.h"
#include "qa/render/worker_protocol.h"
#include "qa/render/worker_runtime.h"
#include "qa/render/worker_transport.h"
#endif

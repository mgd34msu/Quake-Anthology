/*
 * Port of src/contracts/model-attachment.ts (types only).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_MODEL_ATTACHMENT_H
#define QA_CONTRACTS_MODEL_ATTACHMENT_H

#include <stdbool.h>

#include "qa/contracts/content.h"
#include "qa/contracts/scene.h"

/* TS ModelAttachmentDefinition = {digest, grip} & ({kind:"joint",name} |
 * {kind:"mesh",referenceFrame,vertices:[number,number,number]}). */
typedef struct qa_model_attachment_definition {
  qa_content_digest digest;
  qa_model_transform grip;
  bool is_joint;
  const char *name;          /* joint */
  double reference_frame;    /* mesh */
  double vertices[3];        /* mesh */
} qa_model_attachment_definition;

#endif

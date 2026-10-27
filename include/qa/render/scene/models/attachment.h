/* Port of src/render/scene/models/attachment.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_MODELS_ATTACHMENT_H
#define QA_RENDER_SCENE_MODELS_ATTACHMENT_H

#include "qa/contracts/scene.h"

qa_model_transform qa_align_model_attachment(qa_model_transform source,
                                             qa_model_transform destination);

#endif

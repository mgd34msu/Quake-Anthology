/* Port of src/render/scene/models/attachment.ts. GPL-2.0-or-later. */
#include "qa/render/scene/models/attachment.h"

#include "qa/core/math.h"
#include "qa/render/scene/models/transform.h"

qa_model_transform qa_align_model_attachment(qa_model_transform source,
                                             qa_model_transform destination) {
  qa_model_transform inverse;
  inverse.origin =
      qa_model_local_delta(source, qa_scale3(source.origin, -1));
  inverse.axis.vectors[0] =
      qa_model_local_delta(source, (qa_vec3){1, 0, 0});
  inverse.axis.vectors[1] =
      qa_model_local_delta(source, (qa_vec3){0, 1, 0});
  inverse.axis.vectors[2] =
      qa_model_local_delta(source, (qa_vec3){0, 0, 1});
  inverse.scale = (qa_vec3){1, 1, 1};
  return qa_compose_model_transform(destination, inverse);
}

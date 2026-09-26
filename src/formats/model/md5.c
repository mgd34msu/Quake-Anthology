#include "internal.h"
#include <ctype.h>
#include <errno.h>

static void quaternion(float q[4]) {
    float d = 1 - q[0] * q[0] - q[1] * q[1] - q[2] * q[2];
    q[3] = d < 0 ? 0 : -sqrtf(d);
}
static void rotate(const float q[4], const float p[3], float out[3]) {
    float dot = q[0] * p[0] + q[1] * p[1] + q[2] * p[2];
    float scalar = q[3] * q[3] - q[0] * q[0] - q[1] * q[1] - q[2] * q[2];
    out[0] = scalar * p[0] + 2 * (q[0] * dot + q[3] * (q[1] * p[2] - q[2] * p[1]));
    out[1] = scalar * p[1] + 2 * (q[1] * dot + q[3] * (q[2] * p[0] - q[0] * p[2]));
    out[2] = scalar * p[2] + 2 * (q[2] * dot + q[3] * (q[0] * p[1] - q[1] * p[0]));
}
static void rotate_axis(const float q[4], const float p[3], float out[3]) {
    out[0] = p[0] * (2 * (q[3] * q[3] + q[0] * q[0]) - 1) + p[1] * 2 * (q[0] * q[1] - q[3] * q[2]) +
             p[2] * 2 * (q[0] * q[2] + q[3] * q[1]);
    out[1] = p[0] * 2 * (q[0] * q[1] + q[3] * q[2]) + p[1] * (2 * (q[3] * q[3] + q[1] * q[1]) - 1) +
             p[2] * 2 * (q[1] * q[2] - q[3] * q[0]);
    out[2] = p[0] * 2 * (q[0] * q[2] - q[3] * q[1]) + p[1] * 2 * (q[1] * q[2] + q[3] * q[0]) +
             p[2] * (2 * (q[3] * q[3] + q[2] * q[2]) - 1);
}
static bool text_name(model_reader *r, char name[65], qa_bytes *full) {
    qa_bytes t = model_token_next(r);
    if (!t.data)
        return model_fail(r, "missing MD5 name");
    *full = (qa_bytes){t.data, t.size};
    size_t copy = t.size < 64 ? t.size : 64;
    memcpy(name, t.data, copy);
    name[copy] = '\0';
    return true;
}
static bool record_index(model_reader *r, const char *kind, uint32_t count, bool *seen,
                         uint32_t *index) {
    if (!model_expect(r, kind))
        return false;
    *index = (uint32_t)model_integer(r, 0, (int32_t)count - 1);
    if (!r->ok)
        return false;
    if (seen[*index])
        return model_fail(r, "duplicate MD5 record index");
    seen[*index] = true;
    return true;
}
static void skin_vertex(const qa_model_mesh *s, uint32_t vertex, const qa_model_pose *pose,
                        qa_model_vertex *out, bool bind) {
    memset(out, 0, sizeof(*out));
    qa_model_weight_range range = s->vertex_weights[vertex];
    for (uint32_t i = 0; i < range.count; ++i) {
        const qa_model_weight *w = &s->weights[range.first + i];
        const qa_model_pose *p = &pose[w->bone];
        float position[3], normal[3];
        if (bind) {
            rotate(p->orientation, w->offset, position);
            rotate(p->orientation, s->vertices[vertex].normal, normal);
        } else {
            rotate_axis(p->orientation, w->offset, position);
            rotate_axis(p->orientation, s->vertices[vertex].normal, normal);
        }
        for (unsigned k = 0; k < 3; ++k) {
            out->position[k] += w->bias * (p->position[k] + p->scale * position[k]);
            out->normal[k] += w->bias * normal[k];
        }
    }
}
typedef struct position_key {
    float position[3];
    uint32_t vertex;
} position_key;
static int compare_position(const void *va, const void *vb) {
    const position_key *a = va, *b = vb;
    for (unsigned i = 0; i < 3; ++i) {
        if (a->position[i] < b->position[i])
            return -1;
        if (a->position[i] > b->position[i])
            return 1;
    }
    return 0;
}
static bool bind_normals(model_reader *r, qa_model *m, qa_model_mesh *s) {
    position_key *keys = model_alloc(r, s->vertex_count, sizeof(*keys));
    uint32_t *groups = model_alloc(r, s->vertex_count, sizeof(*groups));
    float (*normals)[3] = model_alloc(r, s->vertex_count, sizeof(*normals));
    if (!r->ok) {
        free(keys);
        free(groups);
        free(normals);
        return false;
    }
    for (uint32_t i = 0; i < s->vertex_count; ++i) {
        qa_model_vertex v;
        skin_vertex(s, i, m->bind_pose, &v, true);
        for (unsigned k = 0; k < 3; ++k)
            if (!isfinite(v.position[k])) {
                model_fail(r, "MD5 bind position overflows");
                goto finish;
            }
        memcpy(s->vertices[i].position, v.position, sizeof(v.position));
        memcpy(keys[i].position, v.position, sizeof(v.position));
        keys[i].vertex = i;
        model_bounds_add(&m->bounds, v.position);
    }
    if (s->vertex_count)
        qsort(keys, s->vertex_count, sizeof(*keys), compare_position);
    uint32_t group = 0;
    for (uint32_t i = 0; i < s->vertex_count; ++i) {
        if (i && compare_position(&keys[i - 1], &keys[i]))
            ++group;
        groups[keys[i].vertex] = group;
    }
    for (uint32_t i = 0; i < s->triangle_count; ++i) {
        const uint32_t *tri = s->triangles[i].vertex;
        const float *a = s->vertices[tri[0]].position, *b = s->vertices[tri[1]].position,
                    *c = s->vertices[tri[2]].position;
        float d1[3], d2[3], normal[3];
        for (unsigned k = 0; k < 3; ++k) {
            d1[k] = c[k] - a[k];
            d2[k] = b[k] - a[k];
        }
        model_normalize(d1);
        model_normalize(d2);
        normal[0] = d1[1] * d2[2] - d1[2] * d2[1];
        normal[1] = d1[2] * d2[0] - d1[0] * d2[2];
        normal[2] = d1[0] * d2[1] - d1[1] * d2[0];
        model_normalize(normal);
        float angle = acosf(fmaxf(-1, fminf(1, d1[0] * d2[0] + d1[1] * d2[1] + d1[2] * d2[2])));
        for (unsigned j = 0; j < 3; ++j)
            for (unsigned k = 0; k < 3; ++k)
                normals[groups[tri[j]]][k] += normal[k] * angle;
    }
    for (uint32_t i = 0; i < s->vertex_count; ++i)
        model_normalize(normals[i]);
    for (uint32_t i = 0; i < s->vertex_count; ++i) {
        qa_model_weight_range range = s->vertex_weights[i];
        for (uint32_t j = 0; j < range.count; ++j) {
            const qa_model_weight *w = &s->weights[range.first + j];
            const float *q = m->bind_pose[w->bone].orientation;
            float conjugate[4] = {-q[0], -q[1], -q[2], q[3]}, local[3];
            rotate(conjugate, normals[groups[i]], local);
            for (unsigned k = 0; k < 3; ++k)
                s->vertices[i].normal[k] += local[k] * w->bias;
        }
    }
finish:
    free(keys);
    free(groups);
    free(normals);
    return r->ok;
}
static bool mesh(model_reader *r, qa_model *m, qa_model_mesh *s) {
    if (!model_expect(r, "mesh") || !model_expect(r, "{") || !model_expect(r, "shader"))
        return false;
    s->shader_count = 1;
    s->shaders = model_alloc(r, 1, sizeof(*s->shaders));
    if (!r->ok || !text_name(r, s->shaders[0].name, &s->shaders[0].text_name))
        return false;
    if (!model_expect(r, "numverts"))
        return false;
    s->vertex_count = (uint32_t)model_integer(r, 0, 65535);
    s->texcoord_count = s->vertex_count;
    s->frame_count = 1;
    if (!r->ok || s->vertex_count > r->bytes.size - r->pos)
        return model_fail(r, "MD5 vertex count exceeds text");
    s->vertices = model_alloc(r, s->vertex_count, sizeof(*s->vertices));
    s->texcoords = model_alloc(r, s->vertex_count, sizeof(*s->texcoords));
    s->vertex_weights = model_alloc(r, s->vertex_count, sizeof(*s->vertex_weights));
    bool *seen = model_alloc(r, s->vertex_count, sizeof(*seen));
    if (!r->ok) {
        free(seen);
        return false;
    }
    for (uint32_t i = 0; i < s->vertex_count; ++i) {
        uint32_t id;
        if (!record_index(r, "vert", s->vertex_count, seen, &id))
            break;
        model_expect(r, "(");
        s->texcoords[id].uv[0] = model_scalar(r);
        s->texcoords[id].uv[1] = model_scalar(r);
        model_expect(r, ")");
        s->vertex_weights[id].first = (uint32_t)model_integer(r, 0, INT32_MAX);
        s->vertex_weights[id].count = (uint32_t)model_integer(r, 0, INT32_MAX);
    }
    free(seen);
    if (!r->ok || !model_expect(r, "numtris"))
        return false;
    s->triangle_count = (uint32_t)model_integer(r, 0, 65535);
    if (!r->ok || s->triangle_count > r->bytes.size - r->pos)
        return model_fail(r, "MD5 triangle count exceeds text");
    s->triangles = model_alloc(r, s->triangle_count, sizeof(*s->triangles));
    seen = model_alloc(r, s->triangle_count, sizeof(*seen));
    if (!r->ok) {
        free(seen);
        return false;
    }
    for (uint32_t i = 0; i < s->triangle_count; ++i) {
        uint32_t id;
        if (!record_index(r, "tri", s->triangle_count, seen, &id))
            break;
        for (unsigned k = 0; k < 3; ++k)
            s->triangles[id].texcoord[k] = s->triangles[id].vertex[k] =
                (uint32_t)model_integer(r, 0, (int32_t)s->vertex_count - 1);
    }
    free(seen);
    if (!r->ok || !model_expect(r, "numweights"))
        return false;
    s->weight_count = (uint32_t)model_integer(r, 0, 1048576);
    if (!r->ok || s->weight_count > r->bytes.size - r->pos)
        return model_fail(r, "MD5 weight count exceeds text");
    s->weights = model_alloc(r, s->weight_count, sizeof(*s->weights));
    seen = model_alloc(r, s->weight_count, sizeof(*seen));
    if (!r->ok) {
        free(seen);
        return false;
    }
    for (uint32_t i = 0; i < s->weight_count; ++i) {
        uint32_t id;
        if (!record_index(r, "weight", s->weight_count, seen, &id))
            break;
        qa_model_weight *w = &s->weights[id];
        w->bone = (uint32_t)model_integer(r, 0, (int32_t)m->bone_count - 1);
        w->bias = model_scalar(r);
        if (w->bias < 0 || w->bias > 1) {
            model_fail(r, "MD5 weight bias outside 0..1");
            break;
        }
        model_vector(r, w->offset);
    }
    free(seen);
    if (!r->ok || !model_expect(r, "}"))
        return false;
    for (uint32_t i = 0; i < s->vertex_count; ++i) {
        qa_model_weight_range range = s->vertex_weights[i];
        if (range.first > s->weight_count || range.count > s->weight_count - range.first)
            return model_fail(r, "MD5 vertex weights exceed table");
    }
    return bind_normals(r, m, s);
}
bool model_md5(model_reader *r, qa_model *m) {
    if (!model_expect(r, "MD5Version") || !model_expect(r, "10") || !model_expect(r, "commandline"))
        return false;
    qa_bytes command = model_token_next(r);
    if (!command.data)
        return model_fail(r, "missing MD5 command line");
    m->command_line = (qa_bytes){command.data, command.size};
    if (!model_expect(r, "numJoints"))
        return false;
    m->bone_count = (uint32_t)model_integer(r, 1, 256);
    if (!model_expect(r, "numMeshes"))
        return false;
    m->mesh_count = (uint32_t)model_integer(r, 1, 32);
    if (!model_expect(r, "joints") || !model_expect(r, "{"))
        return false;
    m->bones = model_alloc(r, m->bone_count, sizeof(*m->bones));
    m->bind_pose = model_alloc(r, m->bone_count, sizeof(*m->bind_pose));
    m->meshes = model_alloc(r, m->mesh_count, sizeof(*m->meshes));
    if (!r->ok)
        return false;
    for (uint32_t i = 0; i < m->bone_count; ++i) {
        if (!text_name(r, m->bones[i].name, &m->bones[i].text_name))
            return false;
        m->bones[i].parent = model_integer(r, -1, (int32_t)m->bone_count - 1);
        model_vector(r, m->bind_pose[i].position);
        model_vector(r, m->bind_pose[i].orientation);
        quaternion(m->bind_pose[i].orientation);
        m->bind_pose[i].scale = 1;
    }
    if (!r->ok || !model_expect(r, "}"))
        return false;
    for (uint32_t i = 0; i < m->mesh_count; ++i)
        if (!mesh(r, m, &m->meshes[i]))
            return false;
    if (model_token_next(r).data)
        return model_fail(r, "trailing MD5 token");
    if (m->bounds.min[0] > m->bounds.max[0])
        memset(&m->bounds, 0, sizeof(m->bounds));
    m->frame_count = 1;
    m->frames = model_alloc(r, 1, sizeof(*m->frames));
    if (!r->ok)
        return false;
    m->frames[0].bounds = m->bounds;
    return r->ok;
}
bool qa_model_skin_md5(const qa_model *m, uint32_t index, const qa_model_pose *pose, size_t joints,
                       qa_model_vertex *out, size_t count, qa_error *error) {
    if (!m || m->format != QA_MODEL_MD5 || index >= m->mesh_count || !pose ||
        joints < m->bone_count || count < m->meshes[index].vertex_count ||
        (!out && m->meshes[index].vertex_count)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid MD5 skinning spans");
        return false;
    }
    for (uint32_t i = 0; i < m->bone_count; ++i) {
        for (unsigned k = 0; k < 3; ++k)
            if (!isfinite(pose[i].position[k]))
                goto invalid;
        for (unsigned k = 0; k < 4; ++k)
            if (!isfinite(pose[i].orientation[k]))
                goto invalid;
        if (!isfinite(pose[i].scale))
            goto invalid;
    }
    for (uint32_t i = 0; i < m->meshes[index].vertex_count; ++i)
        skin_vertex(&m->meshes[index], i, pose, &out[i], false);
    return true;
invalid:
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "non-finite MD5 pose");
    return false;
}

bool model_is_md5(qa_bytes bytes) {
    model_reader reader = {bytes, 0, NULL, true};
    qa_bytes token = model_token_next(&reader);
    return token.data && token.size == 10 && !memcmp(token.data, "MD5Version", 10);
}

static void multiply_quaternion(const float a[4], const float b[4], float out[4]) {
    out[0] = a[0] * b[3] + a[3] * b[0] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[1] * b[3] + a[3] * b[1] + a[2] * b[0] - a[0] * b[2];
    out[2] = a[2] * b[3] + a[3] * b[2] + a[0] * b[1] - a[1] * b[0];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    float length = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    if (length > 0)
        for (unsigned k = 0; k < 4; ++k)
            out[k] /= length;
}
static bool animation_world(model_reader *r, const qa_model_animation *a,
                            const qa_model_pose *local, const bool *scale_positions,
                            qa_model_pose *poses) {
    for (uint32_t frame = 0; frame < a->frame_count; ++frame)
        for (uint32_t joint = 0; joint < a->joint_count; ++joint) {
            size_t index = (size_t)frame * a->joint_count + joint;
            const qa_model_pose *src = &local[index];
            qa_model_pose *dst = &poses[index];
            *dst = *src;
            bool scaled =
                scale_positions ? scale_positions[joint] : a->joints[joint].scale_positions;
            float position[3];
            for (unsigned k = 0; k < 3; ++k)
                position[k] = src->position[k] * (scaled ? src->scale : 1);
            int32_t parent = a->joints[joint].bone.parent;
            if (parent < 0)
                memcpy(dst->position, position, sizeof(position));
            else {
                const qa_model_pose *p = &poses[(size_t)frame * a->joint_count + (uint32_t)parent];
                rotate(p->orientation, position, dst->position);
                for (unsigned k = 0; k < 3; ++k)
                    dst->position[k] += p->position[k];
                multiply_quaternion(p->orientation, src->orientation, dst->orientation);
            }
            for (unsigned k = 0; k < 3; ++k)
                if (!isfinite(dst->position[k]))
                    return model_fail(r, "MD5 animated position overflows");
            for (unsigned k = 0; k < 4; ++k)
                if (!isfinite(dst->orientation[k]))
                    return model_fail(r, "MD5 animated orientation overflows");
        }
    return r->ok;
}
void qa_model_animation_free(qa_model_animation *a) {
    if (!a)
        return;
    free(a->joints);
    free(a->base_pose);
    free(a->local_poses);
    free(a->poses);
    free(a->bounds);
    free(a->components);
    qa_buffer_free(&a->source);
    memset(a, 0, sizeof(*a));
}
bool qa_model_animation_load(qa_bytes bytes, qa_model_animation *out, qa_error *error) {
    if (!bytes.data || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "MD5 animation and output are required");
        return false;
    }
    qa_model_animation a = {0};
    model_reader r = {bytes, 0, error, true};
    bool *seen = NULL;
    a.source.data = model_alloc(&r, bytes.size, 1);
    if (!r.ok)
        return false;
    a.source.size = bytes.size;
    if (bytes.size)
        memcpy(a.source.data, bytes.data, bytes.size);
    r.bytes.data = a.source.data;
    if (!model_expect(&r, "MD5Version") || !model_expect(&r, "10") ||
        !model_expect(&r, "commandline"))
        goto fail;
    a.command_line = model_token_next(&r);
    if (!a.command_line.data) {
        model_fail(&r, "missing MD5 animation command line");
        goto fail;
    }
    if (!model_expect(&r, "numFrames"))
        goto fail;
    a.frame_count = (uint32_t)model_integer(&r, 1, 65535);
    if (!model_expect(&r, "numJoints"))
        goto fail;
    a.joint_count = (uint32_t)model_integer(&r, 1, 256);
    if (!model_expect(&r, "frameRate"))
        goto fail;
    a.frame_rate = (uint32_t)model_integer(&r, 1, 1000);
    if (!model_expect(&r, "numAnimatedComponents"))
        goto fail;
    a.component_count = (uint32_t)model_integer(&r, 0, (int32_t)a.joint_count * 6);
    if (a.frame_count > bytes.size) {
        model_fail(&r, "MD5 frame count exceeds text");
        goto fail;
    }
    if (!r.ok || !model_expect(&r, "hierarchy") || !model_expect(&r, "{"))
        goto fail;
    a.joints = model_alloc(&r, a.joint_count, sizeof(*a.joints));
    a.base_pose = model_alloc(&r, a.joint_count, sizeof(*a.base_pose));
    a.bounds = model_alloc(&r, a.frame_count, sizeof(*a.bounds));
    a.components =
        model_alloc(&r, (size_t)a.frame_count * a.component_count, sizeof(*a.components));
    a.local_poses = model_alloc(&r, (size_t)a.frame_count * a.joint_count, sizeof(*a.local_poses));
    a.poses = model_alloc(&r, (size_t)a.frame_count * a.joint_count, sizeof(*a.poses));
    seen = model_alloc(&r, a.frame_count, sizeof(*seen));
    if (!r.ok)
        goto fail;
    for (uint32_t i = 0; i < a.joint_count; ++i) {
        qa_model_animation_joint *joint = &a.joints[i];
        if (!text_name(&r, joint->bone.name, &joint->bone.text_name))
            goto fail;
        joint->bone.parent = model_integer(&r, -1, (int32_t)i - 1);
        joint->flags = (uint32_t)model_integer(&r, 0, 63);
        joint->first_component = (uint32_t)model_integer(&r, 0, (int32_t)a.component_count);
        unsigned count = 0;
        for (unsigned bit = 0; bit < 6; ++bit)
            if (joint->flags & (1u << bit))
                ++count;
        if (joint->first_component > a.component_count ||
            count > a.component_count - joint->first_component) {
            model_fail(&r, "MD5 animated components exceed frame");
            goto fail;
        }
    }
    if (!r.ok || !model_expect(&r, "}") || !model_expect(&r, "bounds") || !model_expect(&r, "{"))
        goto fail;
    for (uint32_t i = 0; i < a.frame_count; ++i) {
        model_vector(&r, a.bounds[i].min);
        model_vector(&r, a.bounds[i].max);
    }
    if (!model_expect(&r, "}") || !model_expect(&r, "baseframe") || !model_expect(&r, "{"))
        goto fail;
    for (uint32_t i = 0; i < a.joint_count; ++i) {
        model_vector(&r, a.base_pose[i].position);
        model_vector(&r, a.base_pose[i].orientation);
        quaternion(a.base_pose[i].orientation);
        a.base_pose[i].scale = 1;
    }
    if (!r.ok || !model_expect(&r, "}"))
        goto fail;
    for (uint32_t i = 0; i < a.frame_count; ++i) {
        uint32_t id;
        if (!record_index(&r, "frame", a.frame_count, seen, &id) || !model_expect(&r, "{"))
            goto fail;
        for (uint32_t j = 0; j < a.component_count; ++j)
            a.components[(size_t)id * a.component_count + j] = model_scalar(&r);
        if (!model_expect(&r, "}"))
            goto fail;
    }
    if (model_token_next(&r).data) {
        model_fail(&r, "trailing MD5 animation token");
        goto fail;
    }
    for (uint32_t frame = 0; frame < a.frame_count; ++frame)
        for (uint32_t joint = 0; joint < a.joint_count; ++joint) {
            qa_model_pose *pose = &a.local_poses[(size_t)frame * a.joint_count + joint];
            *pose = a.base_pose[joint];
            uint32_t component = a.joints[joint].first_component;
            for (unsigned bit = 0; bit < 6; ++bit)
                if (a.joints[joint].flags & (1u << bit)) {
                    float value = a.components[(size_t)frame * a.component_count + component++];
                    if (bit < 3)
                        pose->position[bit] = value;
                    else
                        pose->orientation[bit - 3] = value;
                }
            quaternion(pose->orientation);
        }
    if (!animation_world(&r, &a, a.local_poses, NULL, a.poses))
        goto fail;
    free(seen);
    *out = a;
    return true;
fail:
    free(seen);
    qa_model_animation_free(&a);
    return false;
}
bool qa_model_animation_scales(qa_model_animation *a, const qa_model_scale *scales, size_t count,
                               const bool *scale_positions, size_t joints, qa_error *error) {
    if (!a || !a->joint_count || !a->frame_count || (count && !scales) ||
        (scale_positions && joints < a->joint_count))
        goto invalid;
    for (size_t i = 0; i < count; ++i)
        if (scales[i].joint >= a->joint_count || scales[i].frame >= a->frame_count ||
            !isfinite(scales[i].value))
            goto invalid;
    model_reader r = {{0}, 0, error, true};
    size_t pose_count = (size_t)a->joint_count * a->frame_count;
    qa_model_pose *local = model_alloc(&r, pose_count, sizeof(*local)),
                  *poses = model_alloc(&r, pose_count, sizeof(*poses));
    if (!r.ok) {
        free(local);
        free(poses);
        return false;
    }
    memcpy(local, a->local_poses, pose_count * sizeof(*local));
    for (size_t i = 0; i < count; ++i)
        local[(size_t)scales[i].frame * a->joint_count + scales[i].joint].scale = scales[i].value;
    if (!animation_world(&r, a, local, scale_positions, poses)) {
        free(local);
        free(poses);
        return false;
    }
    free(a->local_poses);
    free(a->poses);
    a->local_poses = local;
    a->poses = poses;
    if (scale_positions)
        for (uint32_t i = 0; i < a->joint_count; ++i)
            a->joints[i].scale_positions = scale_positions[i];
    return true;
invalid:
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid MD5 scale table");
    return false;
}
bool qa_model_animation_sample(const qa_model_animation *a, uint32_t frame, uint32_t previous,
                               float back, qa_model_pose *out, size_t count, qa_error *error) {
    if (!a || !a->frame_count || !out || count < a->joint_count || !isfinite(back) || back < 0 ||
        back > 1) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid MD5 animation sample");
        return false;
    }
    const qa_model_pose *current = a->poses + (size_t)(frame % a->frame_count) * a->joint_count;
    const qa_model_pose *old = a->poses + (size_t)(previous % a->frame_count) * a->joint_count;
    for (uint32_t i = 0; i < a->joint_count; ++i) {
        out[i] = current[i];
        if (back == 0 || frame == previous)
            continue;
        for (unsigned k = 0; k < 3; ++k)
            out[i].position[k] = old[i].position[k] * back + current[i].position[k] * (1 - back);
        if (back == 1) {
            memcpy(out[i].orientation, old[i].orientation, sizeof(out[i].orientation));
            continue;
        }
        float cosine = 0;
        for (unsigned k = 0; k < 4; ++k)
            cosine += old[i].orientation[k] * current[i].orientation[k];
        float sign = cosine < 0 ? -1 : 1;
        cosine *= sign;
        float weight_old = back, weight_new = 1 - back;
        if (cosine <= 0.9995f) {
            float sine = sqrtf(fmaxf(0, 1 - cosine * cosine)), omega = atan2f(sine, cosine);
            if (sine > 0) {
                weight_old = sinf(back * omega) / sine;
                weight_new = sinf((1 - back) * omega) / sine;
            }
        }
        weight_new *= sign;
        for (unsigned k = 0; k < 4; ++k)
            out[i].orientation[k] =
                weight_old * old[i].orientation[k] + weight_new * current[i].orientation[k];
    }
    return true;
}
void qa_model_joint_tag(const qa_model_pose *pose, qa_model_tag *tag) {
    if (!pose || !tag)
        return;
    memcpy(tag->origin, pose->position, sizeof(tag->origin));
    for (unsigned k = 0; k < 3; ++k) {
        float axis[3] = {0};
        axis[k] = 1;
        rotate_axis(pose->orientation, axis, tag->axes[k]);
    }
}

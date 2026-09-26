#include "internal.h"
#include "alias_shadedots.inc"

uint8_t scene_model_normal_index(const float normal[3]) {
    for (unsigned i = 0; i < QA_BYTE_NORMAL_COUNT; ++i)
        if (normal[0] == qa_byte_normals[i].x && normal[1] == qa_byte_normals[i].y &&
            normal[2] == qa_byte_normals[i].z) return (uint8_t)i;
    return 255;
}

bool scene_model_has_shell(const qa_scene_model_input *input) {
    return input->family == QA_SCENE_Q2 && (input->flags & (1024u | 2048u | 4096u | 65536u | 131072u)) != 0;
}

qa_vec3 scene_model_shell_color(uint32_t flags) {
    bool red = (flags & 1024) != 0, green = (flags & 2048) != 0, blue = (flags & 4096) != 0;
    bool double_damage = (flags & 65536) != 0, half = (flags & 131072) != 0;
    if (red && green && blue) return qa_v3(1, 1, 1);
    if (red) return qa_v3(1, 0, blue || double_damage ? 1 : 0);
    if (blue) return qa_v3(0, double_damage ? 1 : 0, 1);
    if (double_damage) return qa_v3(0.9f, 0.7f, 0);
    return qa_v3(half ? 0.56f : 0, green ? 1 : half ? 0.59f : 0, half ? 0.45f : 0);
}

qa_vec3 scene_model_alias_light(const qa_scene_model_input *input) {
    qa_vec3 light = input->ambient;
    if (input->family == QA_SCENE_Q2) {
        bool shell = scene_model_has_shell(input);
        if (shell) light = scene_model_shell_color(input->flags);
        else if (input->flags & 8) light = qa_v3(1, 1, 1);
        else if (input->monochrome) {
            float channel = fmaxf(light.x, fmaxf(light.y, light.z));
            light = qa_v3(channel, channel, channel);
        }
        if ((input->flags & 1) && light.x <= 0.1f && light.y <= 0.1f && light.z <= 0.1f)
            light = qa_v3(0.1f, 0.1f, 0.1f);
        if (input->flags & 512) {
            float pulse = (float)(0.1 * sin(input->seconds * 7));
            light.x = fmaxf(light.x * 0.8f, light.x + pulse);
            light.y = fmaxf(light.y * 0.8f, light.y + pulse);
            light.z = fmaxf(light.z * 0.8f, light.z + pulse);
        }
        if (input->infrared && (input->flags & 32768)) light = qa_v3(1, 0, 0);
    } else if (input->family == QA_SCENE_Q1) {
        float sampled[3] = {light.x, light.y, light.z};
        bool player = input->player || (input->source_path && !strcmp(input->source_path, "progs/player.mdl"));
        bool flame = input->source_path && (!strcmp(input->source_path, "progs/flame.mdl") || !strcmp(input->source_path, "progs/flame2.mdl"));
        float overbright = input->q1_overbright > 0 ? input->q1_overbright : 2;
        for (unsigned k = 0; k < 3; ++k) {
            float ambient = sampled[k] * 255, shade = ambient;
            if (input->view_model && ambient < 24) ambient = shade = 24;
            ambient = fminf(ambient, 128);
            shade = fminf(shade, 192 - ambient);
            if (player && ambient < 8) shade = 8;
            if (flame) shade = 256;
            sampled[k] = shade / 200 * overbright;
        }
        light = model_vec(sampled);
    }
    return light;
}

float scene_model_shade(const qa_scene_model_input *input, const float normal[3], uint8_t index) {
    double yaw = atan2(input->transform.axes[0][1], input->transform.axes[0][0]);
    if (index != 255) {
        int row = (int)(yaw * (16.0 / 6.28318530717958647693));
        return alias_shadedots[((unsigned)row & 15u) * 256u + index];
    }
    qa_vec3 direction = qa_vec_normalize(qa_v3((float)cos(-yaw), (float)sin(-yaw), 1));
    float dot = qa_vec_dot(model_vec(normal), direction);
    return 1 + (dot < 0 ? dot * 0.3f : dot);
}

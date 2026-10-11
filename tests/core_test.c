#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "qa/arena.h"
#include "qa/native.h"
#include "qa/pool.h"
#include "qa/network_unified_frame_pool.h"
#include "qa/network_unified_control.h"
#include "qa/unified_frame_metadata.h"
#include "qa/unified_frame_events.h"
#include "qa/console_cvars_prepare.h"
#include "qa/settings.h"
#include "qa/binary.h"
#include "qa/json.h"
#include "qa/campaign.h"
#include "qa/arena_progress.h"
#include "qa/recovery.h"
#include "qa/persistence_fields.h"
#include "qa/localization.h"
#include "qa/text.h"
#include "qa/q2_sound.h"
#include "qa/hud_q2.h"
#include "qa/vfs.h"
#include "qa/audio.h"
#include "qa/movement.h"
#include "qa/q1_save.h"
#include "qa/scene.h"
#include "qa/render_cpu.h"
#include "../src/render/scene/world/internal.h"
#include "../src/render/scene/world/q3/patch.h"
#include "qa/tools.h"
#include "qa/input.h"
#include "qa/platform_events.h"
#include "qa/network_q3.h"
#include "qa/network_q3_save.h"
#include "qa/network_q1_channel.h"
#include "qa/network_q1_nq.h"
#include "qa/network_q2_kex.h"
#include "qa/network_q2_messages.h"
#include "../src/app/frontend/selected_effects_q1_temporary.h"
#include "../src/network/q2/session_internal.h"
#include "../src/app/application/internal.h"
#include "../src/app/application/unified_q3_events.h"
#include "../src/app/application/events_save.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void test_compiled_sound_names(void)
{
    qa_strings *strings=NULL;const qa_string_id *first,*second;qa_error error={0};
    CHECK(qa_strings_create(&strings,&error));
    CHECK(qa_q2_sound_names_bind(strings,&first,&error));
    size_t count=qa_strings_count(strings);
    CHECK(qa_q2_sound_names_bind(strings,&second,&error));
    CHECK(first==second && qa_strings_count(strings)==count);
    CHECK(first[QA_Q2_SFX_NONE]==QA_STRING_NONE);
    for (size_t i=1;i<QA_Q2_SFX_COUNT;++i)
        CHECK(!strcmp(qa_strings_cstr(strings,first[i]),qa_q2_sound_path(i-1)));
    qa_strings_destroy(strings);
}
static void test_shared_angle_basis(void)
{
    for(int rule=QA_ANGLE_FLOAT_MATH;rule<=QA_ANGLE_FLOAT_NATIVE_MATH;++rule) {
        qa_vec3 forward,right,up,axis[3];
        qa_angle_vectors(qa_v3(0,90,0),&forward,&right,&up,(qa_angle_math_rule)rule);
        CHECK(fabsf(forward.x)<1e-6f && fabsf(forward.y-1)<1e-6f && forward.z==0);
        CHECK(fabsf(right.x-1)<1e-6f && fabsf(right.y)<1e-6f && right.z==0);
        CHECK(up.x==0 && up.y==0 && up.z==1);
        qa_vec3 angles=qa_v3(17.5f,83.125f,-42.75f);
        qa_angle_vectors(angles,&forward,&right,&up,(qa_angle_math_rule)rule);
        qa_angle_axis(angles,axis,(qa_angle_math_rule)rule);
        CHECK(qa_vec_length(qa_vec_sub(axis[0],forward))==0);
        CHECK(qa_vec_length(qa_vec_add(axis[1],right))==0);
        CHECK(qa_vec_length(qa_vec_sub(axis[2],up))==0);
        CHECK(fabsf(qa_vec_length(forward)-1)<1e-6f && fabsf(qa_vec_length(right)-1)<1e-6f);
        CHECK(fabsf(qa_vec_dot(forward,right))<1e-6f && fabsf(qa_vec_dot(forward,up))<1e-6f);
        CHECK(qa_vec_length(qa_vec_sub(qa_vec_cross(axis[0],axis[1]),axis[2]))<1e-6f);
        qa_vec3 only;
        qa_angle_vectors(angles,&only,NULL,NULL,(qa_angle_math_rule)rule);
        CHECK(qa_vec_length(qa_vec_sub(only,forward))==0);
        qa_angle_vectors(qa_v3(90,0,0),&only,NULL,NULL,(qa_angle_math_rule)rule);
        CHECK(fabsf(only.x)<1e-6f && only.y==0 && fabsf(only.z+1)<1e-6f);
    }
}
static void test_shared_bounds_and_transform(void)
{
    qa_bounds unit={qa_v3(0,0,0),qa_v3(1,1,1)};
    qa_bounds contact=qa_bounds_translate(unit,qa_v3(1,0,0));
    CHECK(qa_bounds_overlap(unit,contact) && !qa_bounds_overlap_strict(unit,contact));
    CHECK(qa_bounds_overlap_strict(unit,qa_bounds_translate(unit,qa_v3(.5f,0,0))));
    CHECK(!qa_bounds_overlap(unit,qa_bounds_translate(unit,qa_v3(2,0,0))));
    CHECK(!qa_bounds_expands(unit,unit));
    qa_bounds smaller={qa_v3(.25f,.25f,.25f),qa_v3(.75f,.75f,.75f)};
    CHECK(!qa_bounds_expands(unit,smaller) && qa_bounds_expands(smaller,unit));
    qa_scene_matrix matrix;qa_scene_matrix_identity(&matrix);
    matrix.m[4]=2;matrix.m[12]=7;matrix.m[13]=-3;matrix.m[14]=5;
    qa_vec3 point=qa_scene_matrix_point3(matrix,qa_v3(1,2,3));
    qa_vec4 projected=qa_scene_matrix_point(matrix,qa_v3(1,2,3));
    CHECK(point.x==12 && point.y==-1 && point.z==8);
    CHECK(projected.x==point.x && projected.y==point.y && projected.z==point.z && projected.w==1);
    CHECK(qa_vec_dot_wide(qa_v3(1,2,3),qa_v3(4,5,6))==32);
    qa_vec3 identity[3]={{1,0,0},{0,1,0},{0,0,1}};
    qa_scene_vertex source={.position={11,12,13},.texcoord={.25f,.5f},.color={1,2,3,4}},remapped;
    qa_scene_vertices_remap(&remapped,&source,1,qa_v3(10,10,10),identity,qa_v3(20,30,40),identity);
    CHECK(remapped.position.x==21 && remapped.position.y==32 && remapped.position.z==43);
    CHECK(source.position.x==11 && remapped.texcoord.x==source.texcoord.x &&
        remapped.texcoord.y==source.texcoord.y && !memcmp(&remapped.color,&source.color,sizeof(source.color)));
    qa_scene_vertices_remap(&remapped,&remapped,1,qa_v3(20,30,40),identity,qa_v3(10,10,10),identity);
    CHECK(remapped.position.x==source.position.x && remapped.position.y==source.position.y &&
        remapped.position.z==source.position.z);
}
static void test_shared_vector_geometry(void)
{
    qa_vec3 v=qa_v3(3,4,0);
    CHECK(qa_vec_normalize_length(&v)==5 && v.x==3.f/5 && v.y==4.f/5 && v.z==0);
    v=qa_v3(0,-0.f,0);
    CHECK(qa_vec_normalize_length(&v)==0 && signbit(v.y));
    qa_vec3 axis[3],identity[3]={{1,0,0},{0,1,0},{0,0,1}},product[3];
    qa_angle_axis(qa_v3(12,37,21),axis,QA_ANGLE_VM_MATH);
    qa_vec3 world=qa_axis_from_local(qa_v3(3,-4,5),axis);
    CHECK(qa_vec_length(qa_vec_sub(qa_axis_to_local(world,axis),qa_v3(3,-4,5)))<2e-6f);
    qa_axis_multiply(axis,identity,product);
    CHECK(!memcmp(product,axis,sizeof(axis)));
    qa_axis_multiply(product,identity,product);
    CHECK(!memcmp(product,axis,sizeof(axis)));
    qa_vec3 normal=qa_vec_normalize(qa_v3(1,2,3)),side=qa_vec_perpendicular(normal);
    CHECK(fabsf(qa_vec_dot(normal,side))<1e-6f && fabsf(qa_vec_length(side)-1)<1e-6f);
    qa_vec3 rotated=qa_vec_rotate_around(qa_v3(0,0,1),qa_v3(1,0,0),90);
    CHECK(fabsf(rotated.x)<1e-6f && fabsf(rotated.y-1)<1e-6f && rotated.z==0);
    CHECK(fabsf(qa_inverse_sqrt(4)-.5f)<.001f);
    CHECK(fabsf(qa_vec_length(qa_vec_fast_normalize(qa_v3(3,4,0)))-1)<.002f);
}
static void test_shared_angle_wrapping(void)
{
    for(int rule=QA_ANGLE_WORD_NATIVE;rule<=QA_ANGLE_WORD_ROUNDED;++rule) {
        CHECK(qa_angle_to_word(90,(qa_angle_word_rule)rule)==16384);
        CHECK(qa_angle_to_word(-90,(qa_angle_word_rule)rule)==49152);
        CHECK(qa_angle_mod(720,(qa_angle_word_rule)rule)==0);
    }
    CHECK(qa_angle_to_word(.75f*(360.f/65536),QA_ANGLE_WORD_NATIVE)==0);
    CHECK(qa_angle_to_word(.75f*(360.f/65536),QA_ANGLE_WORD_ROUNDED)==1);
    CHECK(qa_angle_wrap(-450)==270);
    CHECK(qa_angle_subtract(1,359)==2 && qa_angle_subtract(359,1)==-2);
    CHECK(qa_angle_subtract(180,0)==180 && qa_angle_subtract(-180,0)==-180);
    CHECK(qa_angle_delta(719,1)==-2);
}
static void test_shared_direction_angles(void)
{
    qa_vec3 input=qa_v3(3,-4,5);
    qa_vec3 signed_angles=qa_vec_angles(input,QA_DIRECTION_FLOAT,QA_VECTOR_NEGATIVE_PITCH);
    qa_vec3 wrapped=qa_vec_angles(input,QA_DIRECTION_FLOAT,QA_VECTOR_VIEW);
    CHECK(signed_angles.x== -45 && signed_angles.y<0);
    CHECK(wrapped.x==signed_angles.x && wrapped.y==signed_angles.y+360);
    CHECK(qa_vec_yaw(input,QA_DIRECTION_FLOAT,true)==wrapped.y);
    qa_vec3 source=qa_vec_angles(qa_v3(0,0,-1),QA_DIRECTION_VM,QA_VECTOR_SOURCE);
    CHECK(source.x== -270 && source.y==0 && source.z==0);
    qa_vec3 view=qa_vec_angles(qa_v3(0,0,-1),QA_DIRECTION_FLOAT,QA_VECTOR_NEGATIVE_PITCH|QA_VECTOR_VERTICAL_SIGNED);
    CHECK(view.x==90 && view.y==0);
    qa_vec3 integral=qa_vec_angles(input,QA_DIRECTION_INTEGER,QA_VECTOR_SOURCE);
    CHECK(integral.x== -45 && integral.y==307);
    qa_vec3 axis[3];qa_angle_axis(wrapped,axis,QA_ANGLE_FLOAT_MATH);
    CHECK(qa_vec_length(qa_vec_sub(axis[0],qa_vec_normalize(input)))<1e-6f);
}
static void test_source_text(void)
{
    uint8_t source[256],roundtrip[257];char utf8[513];size_t size,restored;qa_error error={0};
    for (unsigned i=0;i<256;++i) source[i]=(uint8_t)i;
    CHECK(qa_text_from_source_bytes((qa_bytes){source,sizeof(source)},utf8,sizeof(utf8),&size,&error));
    CHECK(size==384 && utf8[size]==0);
    CHECK(qa_text_to_source_bytes((qa_bytes){(const uint8_t *)utf8,size},roundtrip,sizeof(roundtrip),&restored,&error));
    CHECK(restored==sizeof(source) && roundtrip[restored]==0 && !memcmp(source,roundtrip,sizeof(source)));
    CHECK(!qa_text_from_source_bytes((qa_bytes){source,sizeof(source)},utf8,size,&restored,&error));
    CHECK(!qa_text_to_source_bytes((qa_bytes){(const uint8_t *)"A",1},roundtrip,1,&restored,&error));
    const uint8_t non_source[]={0xe2,0x82,0xac},malformed[]={0xc3};
    CHECK(!qa_text_to_source_bytes((qa_bytes){non_source,sizeof(non_source)},roundtrip,sizeof(roundtrip),&restored,&error));
    CHECK(!qa_text_to_source_bytes((qa_bytes){malformed,sizeof(malformed)},roundtrip,sizeof(roundtrip),&restored,&error));
    CHECK(qa_text_to_source_bytes((qa_bytes){0},roundtrip,1,&restored,&error) && !restored && !roundtrip[0]);
}
static void test_q2_hud_configuration(void)
{
    qa_strings *strings=NULL;qa_error error={0};qa_hud_q2_names names;
    CHECK(qa_strings_create(&strings,&error) && qa_hud_q2_names_init(&names,strings,&error));
    qa_net_protocol_id protocol={.kind=QA_NET_Q2PRO_36,.revision=1024};
    qa_q2_codec codec;qa_q2_config_layout layout;
    CHECK(qa_q2_codec_init(&codec,protocol,&error));
    codec.wire_flags=8;
    CHECK(qa_q2_config_layout_read(&codec,&layout,&error));
    CHECK(qa_hud_q2_names_configure(&names,protocol,&layout,&error));
    qa_string_id info;
    CHECK(qa_strings_intern_cstr(strings,"player\\custom\\tag",&info,&error));
    CHECK(qa_hud_q2_names_set(&names,names.layout.player_skins,info,NULL,&error));
    CHECK(!strcmp(qa_strings_cstr(strings,names.players[0].model),"male"));
    CHECK(!strcmp(qa_strings_cstr(strings,names.players[0].skin),"grunt"));
    CHECK(qa_hud_q2_names_configure(&names,qa_q2_default_protocol(false),NULL,&error));
    CHECK(qa_hud_q2_names_set(&names,names.layout.player_skins,info,NULL,&error));
    CHECK(!strcmp(qa_strings_cstr(strings,names.players[0].model),"custom"));
    CHECK(!strcmp(qa_strings_cstr(strings,names.players[0].skin),"tag"));
    qa_hud_q2_names_destroy(&names);qa_strings_destroy(strings);
}
static void test_json_caller_storage(void)
{
    const char source[]="\"A\\u0000B\\ud83d\\ude00\\n\"";
    const uint8_t expected[]={'A',0,'B',0xf0,0x9f,0x98,0x80,'\n',0};
    qa_json_document *document=NULL; qa_error error={0};
    CHECK(qa_json_parse((qa_bytes){(const uint8_t *)source,sizeof(source)-1},&document,&error));
    uint8_t storage[sizeof(source)]; qa_bytes decoded={0};
    CHECK(qa_json_string_into(document,qa_json_root(document),storage,sizeof(storage),&decoded,&error));
    CHECK(decoded.data==storage && decoded.size==sizeof(expected)-1);
    CHECK(!memcmp(storage,expected,sizeof(expected)));
    qa_bytes previous=decoded;
    CHECK(!qa_json_string_into(document,qa_json_root(document),storage,1,&decoded,&error));
    CHECK(decoded.data==previous.data && decoded.size==previous.size);
    CHECK(!memcmp(storage,expected,sizeof(expected)));
    qa_buffer owned={0};
    CHECK(qa_json_string(document,qa_json_root(document),&owned,&error));
    CHECK(owned.size==decoded.size && !memcmp(owned.data,expected,sizeof(expected)));
    qa_buffer_free(&owned); qa_json_destroy(document);
}
static bool capture_lightstyle(void *context, const qa_builtin_event *event, qa_error *error)
{
    (void)error;
    *(qa_builtin_event *)context = *event;
    return true;
}
static void test_q1_lightstyle_event(void)
{
    qa_builtin_event event = {0};
    qa_builtin_services services = {.context = &event, .emit = capture_lightstyle};
    qa_actor_id actor = {.registry = 1, .slot = 7, .generation = 2};
    qa_error error = {0};
    CHECK(qa_builtin_q1_lightstyle(&services, 19, actor, 1234, 47, 35, &error));
    CHECK(event.kind == QA_BUILTIN_LIGHT && event.family == QA_GAME_Q1);
    CHECK(event.code == 47 && event.resource == 35 && !event.text);
    CHECK(event.provider == 19 && event.time_ns == 1234 && qa_actor_id_equal(event.actor, actor));
}

static void test_q1_effect_state(void)
{
    frontend_fx_q1_state state;
    frontend_fx_q1_state_initialize(&state, 2, 2);
    qa_actor_id actor={.registry=1,.generation=1,.slot=7};
    frontend_fx_q1_light_recipe recipe={.radius=200,.duration=.1,.color={1,1,1}};
    frontend_fx_q1_light *first=frontend_fx_q1_state_light(&state,actor,0,qa_v3(1,2,3),1,&recipe);
    uint64_t identity=first->identity;
    frontend_fx_q1_state_light(&state,(qa_actor_id){0},8,qa_v3(4,5,6),1,&recipe);
    CHECK(frontend_fx_q1_state_light(&state,actor,0,qa_v3(7,8,9),1.05,&recipe)==first);
    CHECK(first->identity==identity && first->origin.x==7 && fabs(first->die-1.15)<1e-9);
    qa_q1_temp beam={.kind=QA_Q1_TEMP_BEAM,.type=5,.entity=3,.origin={1,2,3},.end={4,5,6}};
    CHECK(frontend_fx_q1_state_beam(&state,actor,true,&beam,2,0));
    beam.entity=4;
    CHECK(frontend_fx_q1_state_beam(&state,actor,true,&beam,2,0));
    beam.entity=5;
    CHECK(!frontend_fx_q1_state_beam(&state,actor,true,&beam,2,0));
    beam.entity=3; beam.end[0]=10;
    CHECK(frontend_fx_q1_state_beam(&state,actor,true,&beam,2.05,0));
    CHECK(state.beams[0].end.x==10 && state.beams[1].source_entity==4);
    beam.entity=5;
    CHECK(frontend_fx_q1_state_beam(&state,actor,true,&beam,2.3,0));
    qa_builtin_random random; qa_builtin_random_seed(&random,1);
    qa_q1_temp explosion={.kind=QA_Q1_TEMP_POINT,.type=3,.origin={10,20,30}};
    frontend_fx_q1_sound sound=FRONTEND_FX_Q1_SOUND_NONE;
    CHECK(frontend_fx_q1_state_temporary(&state,&random,&explosion,(qa_actor_id){0},true,false,3,&sound));
    CHECK(state.particles.count>0 && sound==FRONTEND_FX_Q1_SOUND_EXPLOSION);
    CHECK(state.lights[0].radius==350 && state.lights[0].decay==300 && state.lights[0].die==3.5);
}

static void test_errors_and_buffers(void)
{
    qa_error error;
    qa_error_set(&error, QA_ERROR_FORMAT, 19, "bad %s: %d", "input", 7);
    CHECK(error.code == QA_ERROR_FORMAT);
    CHECK(error.offset == 19);

    char long_message[512];
    memset(long_message, 'x', sizeof(long_message));
    long_message[sizeof(long_message) - 1] = '\0';
    qa_error_set(&error, QA_ERROR_IO, 0, "%s", long_message);
    CHECK(strlen(error.message) == sizeof(error.message) - 1);
    qa_error_set(&error, QA_OK, 0, NULL);
    CHECK(error.code == QA_OK && error.offset == 0 && error.message[0] == '\0');
    qa_error_set(NULL, QA_ERROR_IO, 0, "unused");

    qa_buffer buffer = {.data = malloc(32), .size = 32};
    CHECK(buffer.data != NULL);
    qa_buffer_free(&buffer);
    CHECK(buffer.data == NULL && buffer.size == 0);
    qa_buffer_free(&buffer);
    qa_buffer_free(NULL);
}

static void test_binary(void)
{
    /* All multi-byte reads and writes deliberately use unaligned addresses. */
    uint8_t storage[18] = {0};
    qa_store_u16le(storage + 1, UINT16_C(0x89ab));
    CHECK(storage[1] == 0xab && storage[2] == 0x89);
    CHECK(qa_load_u16le(storage + 1) == UINT16_C(0x89ab));
    qa_store_u32le(storage + 1, UINT32_C(0x89abcdef));
    CHECK(storage[1] == 0xef && storage[2] == 0xcd && storage[3] == 0xab && storage[4] == 0x89);
    CHECK(qa_load_u32le(storage + 1) == UINT32_C(0x89abcdef));
    qa_store_u64le(storage + 1, UINT64_C(0xfedcba9876543210));
    const uint8_t expected[] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe};
    CHECK(memcmp(storage + 1, expected, sizeof(expected)) == 0);
    CHECK(qa_load_u64le(storage + 1) == UINT64_C(0xfedcba9876543210));
    CHECK(storage[0] == 0 && storage[9] == 0);

    const uint16_t signed16_bits[] = {0, INT16_MAX, UINT16_C(0x8000), UINT16_MAX};
    const int16_t signed16_values[] = {0, INT16_MAX, INT16_MIN, -1};
    for (size_t i = 0; i < sizeof(signed16_bits) / sizeof(signed16_bits[0]); i++) {
        qa_store_u16le(storage + 1, signed16_bits[i]);
        CHECK(qa_load_i16le(storage + 1) == signed16_values[i]);
    }
    const uint32_t signed32_bits[] = {0, INT32_MAX, UINT32_C(0x80000000), UINT32_MAX};
    const int32_t signed32_values[] = {0, INT32_MAX, INT32_MIN, -1};
    for (size_t i = 0; i < sizeof(signed32_bits) / sizeof(signed32_bits[0]); i++) {
        qa_store_u32le(storage + 1, signed32_bits[i]);
        CHECK(qa_load_i32le(storage + 1) == signed32_values[i]);
    }

    const uint32_t float_bits[] = {
        0, UINT32_C(0x80000000), UINT32_C(0x3f800000), UINT32_C(0xc1200000),
        UINT32_C(0x00000001), UINT32_C(0x007fffff), UINT32_C(0x7f7fffff),
        UINT32_C(0x7f800000), UINT32_C(0xff800000), UINT32_C(0x7fcabcde)
    };
    for (size_t i = 0; i < sizeof(float_bits) / sizeof(float_bits[0]); i++) {
        qa_store_u32le(storage + 1, float_bits[i]);
        float value = qa_load_f32le(storage + 1);
        uint32_t actual;
        memcpy(&actual, &value, sizeof(actual));
        CHECK(actual == float_bits[i]);
    }
    qa_store_u32le(storage + 1, UINT32_C(0x3f800000));
    CHECK(qa_load_f32le(storage + 1) == 1.0f);
    qa_store_u32le(storage + 1, UINT32_C(0xc1200000));
    CHECK(qa_load_f32le(storage + 1) == -10.0f);
}

static void test_spans(void)
{
    uint8_t data[8] = {0};
    qa_bytes source = {data, sizeof(data)};
    qa_bytes result = {0};
    qa_error error = {0};
    CHECK(qa_bytes_slice(source, 2, 6, &result, &error));
    CHECK(result.data == data + 2 && result.size == 6);
    CHECK(qa_bytes_slice(source, sizeof(data), 0, &result, &error));
    CHECK(result.data == data + sizeof(data) && result.size == 0);
    const size_t bad_ranges[][2] = {
        {9, 0}, {8, 1}, {0, 9}, {1, SIZE_MAX}, {SIZE_MAX, 1}, {SIZE_MAX, SIZE_MAX}
    };
    for (size_t i = 0; i < sizeof(bad_ranges) / sizeof(bad_ranges[0]); i++) {
        CHECK(!qa_bytes_slice(source, bad_ranges[i][0], bad_ranges[i][1], &result, &error));
        CHECK(error.code == QA_ERROR_FORMAT && error.offset == bad_ranges[i][0]);
        CHECK(result.data == data + sizeof(data) && result.size == 0);
    }
    CHECK(qa_bytes_slice((qa_bytes){0}, 0, 0, &result, &error));
    CHECK(result.data == NULL && result.size == 0);
    CHECK(!qa_bytes_slice((qa_bytes){NULL, 1}, 0, 0, &result, &error));
    CHECK(error.code == QA_ERROR_ARGUMENT);
    CHECK(!qa_bytes_slice(source, 0, 1, NULL, &error));
    CHECK(error.code == QA_ERROR_ARGUMENT);
    CHECK(!qa_bytes_slice(source, SIZE_MAX, SIZE_MAX, &result, NULL));
}

static void test_reserved_scene_frame(void)
{
    qa_scene_frame frame;qa_error error={0};
    CHECK(qa_scene_frame_init(&frame,19,2*4096*sizeof(qa_scene_command)+1024*1024,&error));
    CHECK(frame.storage.sealed && frame.storage.pages);
    for(unsigned pass=0;pass<2;++pass){
        qa_scene_frame_reset(&frame,pass+1);
        uint8_t *held=qa_arena_alloc(&frame.storage,33000,64,&error);
        CHECK(held);memset(held,0x36,33000);
        size_t count=pass?1500:1000;
        for(unsigned half=0;half<2;++half){
            for(size_t i=0;i<count;++i){
                size_t first=frame.command_count;
                qa_scene_command view={.kind=QA_SCENE_COMMAND_VIEW};
                view.data.view.viewport.x=(int32_t)(half*count+i);
                bool admitted=qa_scene_frame_emit(&frame,&view,&error);
                if(!admitted)fprintf(stderr,"scene storage: pass=%u half=%u command=%zu capacity=%zu pages=%zu/%zu: %s\n",
                    pass,half,i,frame.command_capacity,frame.storage.pages->active,frame.storage.pages->capacity,error.message);
                CHECK(admitted);
                CHECK(qa_scene_frame_group(&frame,first,QA_SCENE_GROUP_COMPILED,NULL,(float)(count-i),0,0,0,&error));
            }
            CHECK(qa_scene_frame_finish(&frame,NULL,NULL,&error));
            CHECK(frame.commands[half*count].data.view.viewport.x==(int32_t)((half+1)*count-1));
            CHECK(frame.commands[(half+1)*count-1].data.view.viewport.x==(int32_t)(half*count));
            CHECK(held[0]==0x36 && held[32999]==0x36);
        }
    }
    CHECK(frame.storage.pages->overflow==0);
    qa_scene_frame_destroy(&frame);
    CHECK(qa_scene_frame_init(&frame,19,256*1024,&error));
    qa_scene_command view={.kind=QA_SCENE_COMMAND_VIEW};
    while(qa_scene_frame_emit(&frame,&view,&error))view.data.view.viewport.x++;
    CHECK(error.code==QA_ERROR_MEMORY && frame.command_count>0);
    CHECK(frame.storage.pages->overflow==1);
    CHECK(frame.commands[frame.command_count-1].data.view.viewport.x==(int32_t)frame.command_count-1);
    qa_scene_frame_destroy(&frame);
}

static void test_arena(void)
{
    qa_arena arena = {0};
    qa_error error = {0};
    uint8_t *first = qa_arena_alloc(&arena, 17, 1, &error);
    CHECK(first != NULL);
    memset(first, 0x5a, 17);
    for (size_t alignment = 1; alignment <= 4096; alignment *= 2) {
        void *allocation = qa_arena_alloc(&arena, 53, alignment, &error);
        CHECK(allocation != NULL);
        CHECK((uintptr_t)allocation % alignment == 0);
        memset(allocation, 0xa5, 53);
    }
    uint8_t *large = qa_arena_alloc(&arena, 65536, 64, &error);
    CHECK(large != NULL && (uintptr_t)large % 64 == 0);
    memset(large, 0x33, 65536);
    for (size_t i = 0; i < 17; i++) {
        CHECK(first[i] == 0x5a);
    }
    CHECK(qa_arena_alloc(&arena, SIZE_MAX, 16, &error) == NULL);
    CHECK(error.code == QA_ERROR_MEMORY);
    CHECK(qa_arena_alloc(&arena, SIZE_MAX, 1, &error) == NULL);
    CHECK(error.code == QA_ERROR_MEMORY);
    CHECK(qa_arena_alloc(&arena, 8, 3, &error) == NULL);
    CHECK(error.code == QA_ERROR_ARGUMENT);
    CHECK(qa_arena_alloc(&arena, 8, 0, &error) == NULL);
    CHECK(qa_arena_alloc(&arena, 0, 8, &error) == NULL);
    CHECK(qa_arena_alloc(NULL, 1, 1, &error) == NULL);
    CHECK(first[0] == 0x5a && large[65535] == 0x33);

    qa_arena_reset(&arena);
    CHECK(qa_arena_alloc(&arena, 17, 1, &error) == first);
    qa_arena_destroy(&arena);
    CHECK(arena.first == NULL && arena.current == NULL && arena.block_size == 0);
    qa_arena_destroy(&arena);
    qa_arena_reset(&arena);

    qa_arena_init(&arena, 32);
    void *small[3];
    for (size_t i = 0; i < 3; i++) {
        small[i] = qa_arena_alloc(&arena, 32, 1, &error);
        CHECK(small[i] != NULL);
        memset(small[i], (int)i, 32);
    }
    qa_arena_reset(&arena);
    for (size_t i = 0; i < 3; i++) {
        CHECK(qa_arena_alloc(&arena, 32, 1, &error) == small[i]);
    }
    qa_arena_destroy(&arena);
    qa_arena_init(&arena, SIZE_MAX);
    CHECK(qa_arena_alloc(&arena, 1, 1, &error) == NULL);
    CHECK(arena.first == NULL && arena.current == NULL);
    qa_arena_destroy(&arena);
    qa_arena_destroy(NULL);
    qa_arena_reset(NULL);
    qa_arena_init(NULL, 0);

    uint8_t local[1024];
    CHECK(qa_arena_init_buffer(&arena,local+1,sizeof(local)-1,&error));
    uint8_t *borrowed=qa_arena_alloc(&arena,256,64,&error);
    CHECK(borrowed && (uintptr_t)borrowed%64==0);
    memset(borrowed,0x42,256);
    uint8_t *arena_grown=qa_arena_grow(&arena,borrowed,256,512,64,&error);
    CHECK(arena_grown && arena_grown!=borrowed && (uintptr_t)arena_grown%64==0);
    for (size_t i=0;i<256;++i) CHECK(arena_grown[i]==0x42 && borrowed[i]==0x42);
    arena_grown[511]=0x73;
    CHECK(qa_arena_grow(&arena,arena_grown,512,128,64,&error)==arena_grown);
    CHECK(!qa_arena_grow(&arena,arena_grown,512,1024,64,&error));
    CHECK(arena.overflow_count==1 && arena_grown[0]==0x42 && arena_grown[511]==0x73);
    CHECK(!qa_arena_alloc(&arena,sizeof(local),1,&error));
    CHECK(arena.overflow_count==2 && borrowed[255]==0x42);
    qa_arena_reset(&arena);
    CHECK(qa_arena_alloc(&arena,256,64,&error)==borrowed);
    qa_arena_destroy(&arena);
    CHECK(borrowed[255]==0x42);

    qa_arena backing={0},left={0},right={0};
    qa_pool pages={0};
    CHECK(qa_pool_reserve_pages(&pages,&backing,8,4096,64,&error));
    CHECK(backing.sealed);
    qa_arena_init_pool(&left,&pages);qa_arena_init_pool(&right,&pages);
    uint8_t *held=qa_arena_alloc(&left,7000,64,&error);
    CHECK(held && (uintptr_t)held%64==0);
    memset(held,0x37,7000);
    CHECK(qa_arena_alloc(&right,17000,64,&error));
    CHECK(!qa_arena_alloc(&right,9000,64,&error));
    CHECK(pages.overflow==1 && held[6999]==0x37);
    qa_arena_reset(&right);
    CHECK(qa_arena_alloc(&right,20000,64,&error));
    CHECK(held[0]==0x37 && held[6999]==0x37);
    qa_arena_destroy(&right);qa_arena_destroy(&left);
    CHECK(pages.active==0);
    size_t slot;
    uint8_t *first_slot=qa_pool_take(&pages,&slot);
    CHECK(first_slot && slot==0);
    first_slot[64]=73;
    qa_pool_reset(&pages);
    CHECK(pages.active==0 && first_slot[64]==73);
    for(size_t i=0;i<pages.capacity;++i) {
        CHECK(qa_pool_take(&pages,&slot)==qa_pool_at(&pages,i));
        CHECK(slot==i);
    }
    CHECK(pages.active==pages.capacity && first_slot[64]==73);
    qa_pool_reset(&pages);
    size_t left_slot,move_slot,guard_slot;
    size_t overflows=pages.overflow;
    CHECK(qa_pool_take_run(&pages,2,&left_slot));
    uint8_t *moving=qa_pool_take_run(&pages,2,&move_slot);
    uint8_t *guard=qa_pool_take(&pages,&guard_slot);
    CHECK(moving && guard && move_slot==2 && guard_slot==4);
    memset(moving,0x25,2*pages.stride);guard[0]=0x62;
    qa_pool_release_run(&pages,left_slot,2);
    uint8_t *grown=qa_pool_grow_run(&pages,&move_slot,2,4);
    CHECK(grown && move_slot==0 && pages.active==5);
    memmove(grown,moving,2*pages.stride);
    CHECK(grown[0]==0x25 && grown[2*pages.stride-1]==0x25 && guard[0]==0x62);
    CHECK(!qa_pool_grow_run(&pages,&move_slot,4,5));
    CHECK(move_slot==0 && pages.active==5 && guard[0]==0x62 && pages.overflow==overflows);
    qa_pool_reset(&pages);
    qa_arena_destroy(&backing);

    qa_unified_frame_pool *frames=qa_unified_frame_pool_create(128*1024,2,&error);
    CHECK(frames);
    qa_unified_frame_lease *a=qa_unified_frame_lease_acquire(frames,&error);
    qa_unified_frame_lease *b=qa_unified_frame_lease_acquire(frames,&error);
    CHECK(a && b && !qa_unified_frame_lease_acquire(frames,&error));
    held=qa_unified_frame_lease_alloc(a,80000,1,64,&error);
    CHECK(held);memset(held,0x62,80000);
    CHECK(!qa_unified_frame_lease_alloc(b,80000,1,64,&error));
    qa_unified_frame_lease_release(b);
    qa_unified_frame_pool_destroy(&frames);
    CHECK(!frames && held[79999]==0x62);
    qa_unified_frame_lease_release(a);

    qa_unified_control control={.kind=QA_UNIFIED_CONTROL_DISCONNECT,.epoch=7,
        .value.disconnect="session closed"};
    qa_unified_document *source=NULL,*decoded=NULL,*retained=NULL;
    qa_buffer wire={0};
    CHECK(qa_unified_document_create_control(&control,NULL,&source,&error));
    CHECK(qa_unified_document_encode(source,&wire,&error));
    frames=qa_unified_frame_pool_create(128*1024,2,&error);
    a=qa_unified_frame_lease_acquire(frames,&error);
    CHECK(qa_unified_document_decode(QA_UNIFIED_CONTROL_DOCUMENT,
        (qa_bytes){wire.data,wire.size},NULL,a,NULL,&decoded,&error));
    CHECK(qa_unified_document_retain(decoded,&retained,&error));
    qa_unified_document_destroy(source);qa_buffer_free(&wire);
    qa_unified_frame_lease_release(a);qa_unified_frame_pool_destroy(&frames);
    qa_unified_document_destroy(decoded);
    const qa_unified_control *read=qa_unified_document_control(retained);
    CHECK(read && read->kind==control.kind && read->epoch==control.epoch);
    CHECK(!strcmp(read->value.disconnect,control.value.disconnect));
    qa_unified_document_destroy(retained);
}

static void *tokens_frame_allocate(void *lease,size_t bytes,size_t alignment,qa_error *error)
{ return qa_unified_frame_lease_alloc(lease,1,bytes,alignment,error); }
static void test_literal_tokens(void)
{
    qa_error error={0}; qa_command_tokens owned={0},pooled={0};
    char first[]="print",second[]="quoted; text";
    char *values[]={first,second};
    qa_command_tokens literal={.count=2,.values=values,.args_text="quoted; text"};
    CHECK(qa_command_tokens_copy(&literal,&owned,NULL,NULL,&error));
    qa_unified_frame_pool *pool=qa_unified_frame_pool_create(128*1024,2,&error); CHECK(pool);
    qa_unified_frame_lease *lease=qa_unified_frame_lease_acquire(pool,&error); CHECK(lease);
    CHECK(qa_command_tokens_copy(&literal,&pooled,tokens_frame_allocate,lease,&error));
    for (qa_ruleset_id dialect=QA_RULESET_NETQUAKE;dialect<=QA_RULESET_Q3;++dialect) {
        qa_command_tokens heap={0},frame={0},span={0};
        CHECK(qa_command_tokenize("say \"hello world\" next",dialect,false,&heap,NULL,NULL,&error));
        CHECK(qa_command_tokenize("say \"hello world\" next",dialect,false,&frame,tokens_frame_allocate,lease,&error));
        CHECK(qa_command_tokenize_span("say \"hello world\" next;ignored",strlen("say \"hello world\" next"),
            dialect,false,&span,tokens_frame_allocate,lease,&error));
        CHECK(heap.count==3 && frame.count==3 && frame.borrowed);
        CHECK(span.count==heap.count && span.borrowed);
        for (size_t i=0;i<heap.count;++i) CHECK(!strcmp(heap.values[i],frame.values[i]));
        for (size_t i=0;i<heap.count;++i) CHECK(!strcmp(heap.values[i],span.values[i]));
        CHECK(!strcmp(frame.values[1],"hello world"));
        CHECK(!strcmp(frame.args_text,dialect==QA_RULESET_Q3?"hello world next":"\"hello world\" next"));
        CHECK(!strcmp(frame.args_text,heap.args_text));
        CHECK(!strcmp(span.args_text,heap.args_text));
        qa_command_tokens_free(&heap); qa_command_tokens_free(&frame); qa_command_tokens_free(&span);
    }
    second[0]='X'; qa_unified_frame_pool_destroy(&pool);
    CHECK(owned.count==2 && pooled.count==2);
    CHECK(!strcmp(owned.values[1],"quoted; text") && !strcmp(pooled.values[1],owned.values[1]));
    CHECK(!strcmp(pooled.args_text,"quoted; text"));
    char *held=pooled.values[1];
    qa_command_tokens_free(&pooled); CHECK(!strcmp(held,"quoted; text"));
    qa_unified_frame_lease_release(lease); qa_command_tokens_free(&owned);
}

static qa_unified_document *metadata_decode(qa_unified_frame_pool *pool,
    qa_unified_frame_metadata *owned)
{
    qa_error error={0};qa_buffer wire={0};qa_unified_document *source=NULL,*decoded=NULL;
    CHECK(qa_unified_document_create_metadata(&owned,NULL,&source,&error));
    CHECK(qa_unified_document_encode(source,&wire,&error));
    qa_unified_frame_lease *lease=qa_unified_frame_lease_acquire(pool,&error);CHECK(lease);
    CHECK(qa_unified_document_decode(QA_UNIFIED_CONTROL_DOCUMENT,
        (qa_bytes){wire.data,wire.size},NULL,lease,NULL,&decoded,&error));
    qa_unified_frame_lease_release(lease);qa_unified_document_destroy(source);qa_buffer_free(&wire);
    return decoded;
}
static void test_metadata_retention(void)
{
    qa_error error={0};qa_unified_frame_pool *pool=qa_unified_frame_pool_create(256*1024,4,&error);CHECK(pool);
    qa_unified_frame_lease *lease=qa_unified_frame_lease_acquire(pool,&error);CHECK(lease);
    qa_unified_frame_metadata *value=qa_unified_frame_lease_alloc(lease,1,sizeof(*value),_Alignof(qa_unified_frame_metadata),&error);CHECK(value);
    *value=(qa_unified_frame_metadata){.epoch=1,.q1_revision=1,.replace_configurations=true,
        .replace_styles=true,.replace_q3=true,.replace_q1=true};
    value->q1=qa_unified_frame_lease_alloc(lease,1,sizeof(*value->q1),_Alignof(qa_unified_q1_world_state),&error);CHECK(value->q1);
    value->q1->level=qa_unified_frame_lease_alloc(lease,6,1,1,&error);CHECK(value->q1->level);memcpy(value->q1->level,"start",6);
    qa_unified_document *current=NULL;
    CHECK(qa_unified_document_create_metadata(&value,lease,&current,&error));CHECK(!value);
    qa_unified_frame_lease_release(lease);
    for (uint64_t i=1;i<=64;++i) {
        value=calloc(1,sizeof(*value));CHECK(value);
        *value=(qa_unified_frame_metadata){.epoch=1,.frame=i,.q1_revision=1,.style_revision=i,.replace_styles=true,.style_count=1};
        value->styles=calloc(1,sizeof(*value->styles));CHECK(value->styles);
        value->styles[0]=(qa_unified_style_pattern){.family=QA_GAME_Q1,.pattern=strdup(i&1?"m":"a")};CHECK(value->styles[0].pattern);
        qa_unified_document *update=metadata_decode(pool,value),*merged=NULL;
        CHECK(qa_unified_metadata_apply(current,update,&merged,&error));
        qa_unified_document_destroy(current);qa_unified_document_destroy(update);current=merged;
        const qa_unified_frame_metadata *read=qa_unified_document_metadata(current);
        CHECK(read->frame==i && read->style_revision==i && !strcmp(read->q1->level,"start"));
        CHECK(!strcmp(read->styles[0].pattern,i&1?"m":"a"));
    }
    value=calloc(1,sizeof(*value));CHECK(value);
    *value=(qa_unified_frame_metadata){.epoch=1,.frame=65,.q1_revision=2,.style_revision=64,.replace_q1=true};
    value->q1=calloc(1,sizeof(*value->q1));CHECK(value->q1);value->q1->level=strdup("e1m1");CHECK(value->q1->level);
    qa_unified_document *update=metadata_decode(pool,value),*merged=NULL;
    CHECK(qa_unified_metadata_apply(current,update,&merged,&error));
    qa_unified_document_destroy(current);qa_unified_document_destroy(update);qa_unified_frame_pool_destroy(&pool);
    const qa_unified_frame_metadata *read=qa_unified_document_metadata(merged);
    CHECK(read->frame==65 && !strcmp(read->q1->level,"e1m1") && !strcmp(read->styles[0].pattern,"a"));
    qa_unified_document_destroy(merged);
}

static void test_files(void)
{
    char directory[] = "/tmp/qa-core-test-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char path[128], link_path[128], fifo_path[128], missing_path[128];
    CHECK(snprintf(path, sizeof(path), "%s/data", directory) > 0);
    CHECK(snprintf(link_path, sizeof(link_path), "%s/link", directory) > 0);
    CHECK(snprintf(fifo_path, sizeof(fifo_path), "%s/fifo", directory) > 0);
    CHECK(snprintf(missing_path, sizeof(missing_path), "%s/missing", directory) > 0);
    int descriptor = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    CHECK(descriptor >= 0);
    qa_buffer buffer = {0};
    qa_error error = {0};
    CHECK(qa_file_read_all(path, &buffer, &error));
    CHECK(buffer.data == NULL && buffer.size == 0);

    const uint8_t contents[] = {0, 1, 0xfe, 0xff, 13, 10, 0, 42};
    CHECK(write(descriptor, contents, sizeof(contents)) == (ssize_t)sizeof(contents));
    CHECK(qa_file_read_all(path, &buffer, &error));
    CHECK(buffer.size == sizeof(contents) && memcmp(buffer.data, contents, sizeof(contents)) == 0);
    qa_buffer previous = buffer;
    CHECK(!qa_file_read_all(missing_path, &buffer, &error));
    CHECK(error.code == QA_ERROR_NOT_FOUND && buffer.data == previous.data && buffer.size == previous.size);
    CHECK(!qa_file_read_all(directory, &buffer, &error));
    CHECK(error.code == QA_ERROR_IO && buffer.data == previous.data);
    CHECK(mkfifo(fifo_path, 0600) == 0);
    CHECK(!qa_file_read_all(fifo_path, &buffer, &error));
    CHECK(error.code == QA_ERROR_IO && buffer.data == previous.data);
    CHECK(!qa_file_read_all(NULL, &buffer, &error));
    CHECK(error.code == QA_ERROR_ARGUMENT && buffer.data == previous.data);
    CHECK(!qa_file_read_all("", &buffer, &error));
    CHECK(!qa_file_read_all(path, NULL, &error));
    CHECK(!qa_file_read_all(missing_path, &buffer, NULL));
    qa_buffer_free(&buffer);

    CHECK(symlink(path, link_path) == 0);
    CHECK(qa_file_read_all(link_path, &buffer, &error));
    CHECK(buffer.size == sizeof(contents) && memcmp(buffer.data, contents, sizeof(contents)) == 0);
    qa_buffer_free(&buffer);
    CHECK(close(descriptor) == 0);
    CHECK(unlink(link_path) == 0);
    CHECK(unlink(path) == 0);
    CHECK(unlink(fifo_path) == 0);
    CHECK(rmdir(directory) == 0);
}

static void test_localization_lookup_generation(void)
{
    qa_error error={0};char directories[2][64]={"/tmp/qa-localization-a-XXXXXX","/tmp/qa-localization-b-XXXXXX"};
    const char *values[]={"GREETING = \"first\"\n","GREETING = \"second\"\n"};
    char folders[2][128],paths[2][160];
    for (size_t i=0;i<2;++i) {
        CHECK(mkdtemp(directories[i]));
        CHECK(snprintf(folders[i],sizeof(folders[i]),"%s/localization",directories[i])>0);
        CHECK(mkdir(folders[i],0700)==0);
        CHECK(snprintf(paths[i],sizeof(paths[i]),"%s/loc_english.txt",folders[i])>0);
        int fd=open(paths[i],O_WRONLY|O_CREAT|O_EXCL,0600);CHECK(fd>=0);
        CHECK(write(fd,values[i],strlen(values[i]))==(ssize_t)strlen(values[i]));CHECK(close(fd)==0);
    }
    qa_resource_pool *resources=qa_resource_pool_create(&error);CHECK(resources);
    qa_vfs *view=qa_vfs_create(resources,&error);CHECK(view);
    qa_localization_pool *pool=qa_localization_pool_create(&error);CHECK(pool);
    qa_mount_id mounts[2];
    CHECK(qa_vfs_mount_directory(view,directories[0],QA_ARCHIVE_EXACT,false,mounts,&error));
    qa_localization_options options={.profile=QA_LOCALIZATION_Q2_RERELEASE,.platform="LINUX"};
    qa_localization *first=NULL,*again=NULL,*second=NULL;
    CHECK(qa_localization_acquire(pool,view,"english",&options,&first,&error));
    CHECK(!strcmp(qa_localization_find(first,"GREETING")->format,"first"));
    options.platform="linux";
    CHECK(qa_localization_acquire(pool,view,"english",&options,&again,&error));CHECK(again==first);
    qa_localization_release(again);
    CHECK(qa_vfs_mount_directory(view,directories[1],QA_ARCHIVE_EXACT,false,mounts+1,&error));
    qa_mount_id order[]={mounts[1],mounts[0]};CHECK(qa_vfs_set_order(view,order,2,&error));
    CHECK(qa_localization_acquire(pool,view,"english",&options,&second,&error));
    CHECK(!strcmp(qa_localization_find(second,"GREETING")->format,"second"));
    CHECK(!strcmp(qa_localization_find(first,"GREETING")->format,"first"));
    qa_vfs_destroy(view);qa_localization_pool_destroy(pool);qa_resource_pool_destroy(resources);
    CHECK(!strcmp(qa_localization_find(second,"GREETING")->format,"second"));
    qa_localization_release(first);qa_localization_release(second);
    for (size_t i=0;i<2;++i){CHECK(unlink(paths[i])==0);CHECK(rmdir(folders[i])==0);CHECK(rmdir(directories[i])==0);}
}

static void test_retained_movement_result(void)
{
    for (qa_game_family family = QA_GAME_Q1; family <= QA_GAME_Q3; ++family) {
        uint32_t tag = (uint32_t)family + 1u;
        CHECK(qa_persistence_family_tag(family) == tag);
        CHECK(qa_persistence_family_from_tag(tag) == family);
    }
    CHECK(qa_persistence_family_tag(qa_persistence_family_from_tag(0)) == 0);

    qa_error error = {0}; qa_movement_contact contacts[32] = {0};
    contacts[0].trace.fraction = 0.25f; contacts[0].substep = 7;
    qa_movement_result source = {.status = QA_MOVEMENT_ACTIVE,
        .command_sequence = 17, .state = {.kind = QA_RULESET_Q2_RERELEASE},
        .contacts = contacts, .contact_count = 1, .contact_capacity = 32};
    source.state.data.q2r.origin = qa_v3(1, 2, 3);
    qa_movement_result out = {0};
    CHECK(qa_movement_result_reserve(&out, 32, &error));
    qa_movement_contact *reserved = out.contacts;
    CHECK(qa_movement_result_reserve(&out, 16, &error) && out.contacts == reserved);
    CHECK(qa_movement_result_copy(&source, &out, &error) && out.contacts == reserved);
    qa_movement_contact *storage = out.contacts;
    CHECK(storage != contacts && out.contact_count == 1 && out.contact_capacity >= 32);
    CHECK(out.command_sequence == 17 && out.state.data.q2r.origin.z == 3);
    CHECK(out.contacts[0].trace.fraction == 0.25f && out.contacts[0].substep == 7);
    contacts[0].trace.fraction = 0.75f; source.contact_count = 32; source.command_sequence = 18;
    CHECK(out.contacts[0].trace.fraction == 0.25f);
    CHECK(qa_movement_result_copy(&source, &out, &error));
    CHECK(out.contacts == storage && out.contact_count == 32 && out.command_sequence == 18);
    CHECK(out.contacts[0].trace.fraction == 0.75f);
    source.contact_count = 0; source.contacts = NULL; source.contact_capacity = 0;
    CHECK(qa_movement_result_copy(&source, &out, &error));
    CHECK(out.contacts == storage && out.contact_count == 0 && out.contact_capacity >= 32);
    CHECK(qa_movement_result_copy(&out, &out, &error) && out.contacts == storage);
    qa_movement_result_clear(&out);
    CHECK(out.contacts == storage && !out.command_sequence && !out.contact_count);
    CHECK(qa_movement_result_copy(&source, &out, &error) && out.contacts == storage);
    qa_movement_result_free(&out); CHECK(!out.contacts && !out.contact_capacity);
}

typedef struct audio_notification_counts { size_t starts, stops; } audio_notification_counts;
static void count_audio_notification(void *context, const qa_audio_voice_event *event)
{
    audio_notification_counts *counts = context;
    CHECK(event->sample && event->voice_id);
    if (event->started) ++counts->starts; else ++counts->stops;
}
static qa_audio_trace_hit audio_material_trace(void *context, qa_vec3 start, qa_vec3 end,
    qa_vec3 mins, qa_vec3 maxs)
{
    (void)start; (void)mins; (void)maxs;
    return (qa_audio_trace_hit){.fraction=.5f, .end=end, .material=*(qa_string_id *)context};
}

typedef struct log_delivery_test {
    qa_application app;
    size_t normal, captured;
    qa_command_context delivered;
} log_delivery_test;
static bool log_test_output(void *context,const char *text,qa_error *error)
{
    log_delivery_test *test=context;(void)error;
    CHECK(text && *text=='v');++test->captured;return true;
}
static void log_test_normal(void *context,const qa_command_context *command,const char *text)
{
    log_delivery_test *test=context;
    CHECK(text && *text=='v');test->delivered=*command;++test->normal;
}
static bool log_test_produce(void *context,const qa_command_invocation *command,qa_error *error)
{
    log_delivery_test *test=context;char text[]="value";
    bool okay=qa_application_log_emit(&test->app,&command->context,text,true,error);
    text[0]='x';CHECK(test->captured==0 && test->normal==0);
    return okay;
}
static void test_ring_console_capture(void)
{
    qa_error error={0};log_delivery_test test={0};
    CHECK(application_event_stream_create(&test.app,16,&error));
    qa_command_context context={.owner=1,.dialect=QA_RULESET_Q2_RERELEASE,.origin=QA_COMMAND_SERVER};
    test.app.console=qa_console_create(&(qa_console_options){.context=context,
        .disable_builtins=true,.user=&test,.print=log_test_normal},&error);
    CHECK(test.app.console);
    CHECK(qa_console_register(test.app.console,"ringlog","",1,false,log_test_produce,&test,&error));
    uint64_t first=qa_application_events_next(&test.app);
    CHECK(qa_application_console_execute_capture(&test.app,test.app.console,&context,"ringlog",log_test_output,&test,&error));
    CHECK(test.captured==1 && test.normal==0);
    qa_application_event_view view;
    CHECK(qa_application_event_read(&test.app,&(qa_application_event_cursor){.id=first},&view) &&
        view.kind==QA_APPLICATION_EVENT_LOG);
    const qa_application_log_event *log=view.value.log;
    CHECK(log->context.dialect==context.dialect && log->context.owner==1 && log->text[0]=='v');
    qa_application_log_deliver(test.app.console,log,NULL,NULL);
    CHECK(test.captured==1 && test.normal==0);
    uint64_t next=qa_application_events_next(&test.app);
    CHECK(qa_application_log_emit(&test.app,&context,"value",true,&error));
    CHECK(qa_application_event_read(&test.app,&(qa_application_event_cursor){.id=next},&view));
    qa_application_log_deliver(test.app.console,view.value.log,NULL,NULL);
    CHECK(test.normal==1 && test.delivered.dialect==context.dialect);
    qa_console_destroy(test.app.console);
    application_event_stream_dispose(&test.app);free(test.app.unified_persistent);
}
static void test_shared_effect_cursor(void)
{
    qa_application app={0}; qa_error error={0};
    CHECK(application_event_stream_create(&app,16,&error));
    qa_application_event_subscription first={0},second={0};
    qa_application_events_subscribe(&app,&first);
    qa_application_events_subscribe(&app,&second);
    qa_application_q3_effect_event event={.recipient=&first,.physical=11,
        .entity={.number=11,.event=14,.eventParm=7},.position={1,2,3},
        .has_sound_origin=true,.sound_origin={4,5,6}};
    CHECK(qa_application_q3_effect_emit(&app,&event,&error));
    event.entity.event=21; event.recipient=&second;
    CHECK(qa_application_q3_effect_emit(&app,&event,&error));
    app.event_local_cursor=app.event_source_cursor=qa_application_events_next(&app);
    application_events_retire(&app);
    CHECK(qa_application_events_first(&app)==1);
    qa_application_event_cursor cursor={.id=1}; qa_application_event_view view;
    CHECK(qa_application_event_read(&app,&cursor,&view) && view.kind==QA_APPLICATION_EVENT_Q3_EFFECT);
    CHECK(view.value.q3_effect->recipient==&first && view.value.q3_effect->entity.event==14 &&
        view.value.q3_effect->entity.eventParm==7 && view.value.q3_effect->physical==11 &&
        view.value.q3_effect->has_sound_origin && view.value.q3_effect->sound_origin.z==6);
    qa_application_events_consumed(&first,3);
    CHECK(qa_application_events_first(&app)==1);
    qa_application_events_consumed(&second,2);
    CHECK(qa_application_events_first(&app)==2);
    cursor=(qa_application_event_cursor){.id=2};
    CHECK(qa_application_event_read(&app,&cursor,&view) && view.value.q3_effect->recipient==&second &&
        view.value.q3_effect->entity.event==21);
    qa_application_events_unsubscribe(&second);
    CHECK(qa_application_events_first(&app)==3 && !second.application);
    application_event_stream_dispose(&app);
    CHECK(!first.application);
    free(app.unified_persistent);
}

static void test_shared_audio_preparation(void)
{
    qa_error error = {0}; char directory[] = "/tmp/qa-audio-assets-XXXXXX";
    CHECK(mkdtemp(directory));
    char folder[128], path[160];
    CHECK(snprintf(folder, sizeof(folder), "%s/sound", directory) > 0);
    CHECK(mkdir(folder, 0700) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/test.wav", folder) > 0);
    uint8_t wave[76] = {0};
    memcpy(wave, "RIFF", 4); qa_store_u32le(wave + 4, sizeof(wave) - 8);
    memcpy(wave + 8, "WAVEfmt ", 8); qa_store_u32le(wave + 16, 16);
    qa_store_u16le(wave + 20, 1); qa_store_u16le(wave + 22, 1);
    qa_store_u32le(wave + 24, 22050); qa_store_u32le(wave + 28, 44100);
    qa_store_u16le(wave + 32, 2); qa_store_u16le(wave + 34, 16);
    memcpy(wave + 36, "data", 4); qa_store_u32le(wave + 40, 32);
    for (size_t i = 0; i < 16; ++i) qa_store_u16le(wave + 44 + 2 * i, (uint16_t)(i * 713));
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600); CHECK(fd >= 0);
    CHECK(write(fd, wave, sizeof(wave)) == (ssize_t)sizeof(wave)); CHECK(close(fd) == 0);
    qa_resource_pool *resources = qa_resource_pool_create(&error); CHECK(resources);
    qa_vfs *view = qa_vfs_create(resources, &error); CHECK(view);
    qa_mount_id mount;
    CHECK(qa_vfs_mount_directory(view, directory, QA_ARCHIVE_EXACT, false, &mount, &error));
    qa_strings *names = NULL; CHECK(qa_strings_create(&names, &error));
    qa_audio_bank *bank = NULL; CHECK(qa_audio_bank_create(view, names, &bank, &error));
    qa_audio_sample *held = NULL; int16_t held_samples[32];
    qa_game_family families[] = {QA_GAME_Q1, QA_GAME_Q2, QA_GAME_Q3};
    for (size_t i = 0; i < sizeof(families) / sizeof(*families); ++i) {
        qa_audio_asset *asset = NULL; qa_audio_sample *first = NULL, *again = NULL, *expected = NULL, *changed = NULL;
        CHECK(qa_audio_bank_register(bank, "test.wav", families[i], &asset, &error) && asset);
        const char *aliases[] = {"test.wav", "sound/test.wav", "#sound/test.wav"};
        for (size_t j = 0; j < 3; ++j) {
            qa_string_id id = qa_strings_find(names, (qa_bytes){(const uint8_t *)aliases[j], strlen(aliases[j])});
            CHECK(id && qa_audio_bank_get_id(bank, id, families[i]) == asset);
        }
        CHECK(qa_audio_asset_resample(asset, 44100, families[i], &first, &error));
        CHECK(qa_audio_asset_resample(asset, 44100, families[i], &again, &error));
        CHECK(first == again);
        CHECK(qa_audio_resample_source(qa_audio_asset_sample(asset), 44100, families[i], &expected, &error));
        CHECK(first->frame_count == expected->frame_count && first->loop_start == expected->loop_start);
        CHECK(first->channels == expected->channels && first->sample_rate == expected->sample_rate);
        CHECK(!memcmp(first->samples, expected->samples, (size_t)first->frame_count * sizeof(int16_t)));
        CHECK(qa_audio_asset_resample(asset, 32000, families[i], &changed, &error));
        CHECK(changed != first && changed->sample_rate == 32000 && first->sample_rate == 44100);
        CHECK(!memcmp(first->samples, expected->samples, (size_t)first->frame_count * sizeof(int16_t)));
        qa_audio_mixer *mixer = NULL;
        audio_notification_counts counts = {0};
        qa_audio_mixer_options options = {.sample_rate = 44100, .output_channels = 2,
            .initial_voices = 8, .prepared_capacity = 1, .workspace_capacity = 8,
            .observer = count_audio_notification, .observer_user = &counts};
        CHECK(qa_audio_mixer_create(&options, &mixer, &error));
        qa_audio_play sound = {.sample = qa_audio_asset_sample(asset), .asset = asset,
            .family = families[i], .actor = QA_AUDIO_NO_ACTOR, .owner = QA_AUDIO_NO_OWNER,
            .audience = QA_AUDIO_WORLD, .origin_kind = QA_AUDIO_LOCAL, .volume = 1};
        for (size_t turn = 0; turn < 16; ++turn) {
            bool accepted = false;
            CHECK(qa_audio_mixer_play(mixer, &sound, 0, &accepted, &error) && accepted);
            CHECK(qa_audio_mixer_play(mixer, &sound, 100, &accepted, &error) && accepted);
            int16_t output[32]; CHECK(qa_audio_mixer_mix(mixer, output, 16, &error));
            qa_audio_mixer_stop_all(mixer);
        }
        CHECK(counts.starts && counts.starts == counts.stops);
        qa_audio_mixer_destroy(mixer);
        qa_audio_engine *engine=NULL;
        counts=(audio_notification_counts){0};
        qa_application app={0};CHECK(application_event_stream_create(&app,16,&error));
        qa_audio_engine_options engine_options={.sample_rate=44100,.output_channels=2,
            .mix_frames=16,.initial_voices=8,.observer=count_audio_notification,.observer_user=&counts};
        CHECK(qa_audio_engine_create(&engine_options,&engine,&error));
        qa_audio_event event={.kind=QA_AUDIO_EVENT_PLAY,.sound=sound};
        event.sound.audience=0;event.sound.owner=1;
        uint64_t first_event=qa_application_events_next(&app);
        CHECK(qa_application_audio_emit(&app,&event,&error));
        CHECK(qa_application_audio_emit(&app,&(qa_audio_event){.kind=QA_AUDIO_EVENT_STOP_OWNER,
            .sound={.owner=1,.audience=0}},&error));
        event.sound.owner=2;
        CHECK(qa_application_audio_emit(&app,&event,&error));
        CHECK(counts.starts==0);
        qa_audio_listener listener={.seat=0,.actor=QA_AUDIO_NO_ACTOR,.gain=1,
            .axis={{1,0,0},{0,1,0},{0,0,1}}};
        CHECK(qa_audio_engine_listeners(engine,&listener,1,&error));
        qa_application_event_view output;
        for(uint64_t id=first_event;id<qa_application_events_next(&app);++id) {
            CHECK(qa_application_event_read(&app,&(qa_application_event_cursor){.id=id},&output));
            CHECK(output.kind==QA_APPLICATION_EVENT_AUDIO);
            if(id==first_event) CHECK(output.value.audio->kind==QA_AUDIO_EVENT_PLAY && output.value.audio->sound.owner==1);
            if(id==first_event+1) CHECK(output.value.audio->kind==QA_AUDIO_EVENT_STOP_OWNER && output.value.audio->sound.owner==1);
            if(id==first_event+2) CHECK(output.value.audio->kind==QA_AUDIO_EVENT_PLAY && output.value.audio->sound.owner==2);
            CHECK(qa_audio_engine_apply(engine,output.value.audio,&error));
            if(id==first_event+1) CHECK(counts.starts==counts.stops);
        }
        CHECK(qa_application_event_read(&app,&(qa_application_event_cursor){.id=first_event},&output) &&
            output.value.audio->kind==QA_AUDIO_EVENT_PLAY && output.value.audio->sound.owner==1);
        int16_t captured[32];CHECK(qa_audio_engine_mix(engine,captured,16,&error));
        bool audible=false;for(size_t j=0;j<32;++j) audible|=captured[j]!=0;
        CHECK(audible && counts.starts==counts.stops+1);
        size_t played=counts.starts;
        qa_audio_engine_stop_all(engine);CHECK(counts.starts==counts.stops);
        CHECK(qa_audio_engine_apply(engine,&event,&error));
        CHECK(qa_audio_engine_mix(engine,captured,16,&error) && counts.starts==played+1);
        CHECK(qa_audio_engine_apply(engine,&(qa_audio_event){.kind=QA_AUDIO_EVENT_STOP_OWNER,
            .all=true,.sound={.audience=QA_AUDIO_WORLD}},&error));
        CHECK(counts.starts==counts.stops);
        qa_audio_engine_destroy(engine);CHECK(counts.starts==counts.stops);
        application_event_stream_dispose(&app);free(app.unified_persistent);
        if (i + 1 == sizeof(families) / sizeof(*families)) {
            CHECK(first->frame_count == sizeof(held_samples) / sizeof(*held_samples));
            memcpy(held_samples, first->samples, sizeof(held_samples)); held = qa_audio_sample_retain(first); CHECK(held);
        }
        qa_audio_sample_release(first); qa_audio_sample_release(again);
        qa_audio_sample_release(expected); qa_audio_sample_release(changed); qa_audio_asset_release(asset);
    }
    int16_t doppler_source[2177];
    for(size_t i=0;i<sizeof(doppler_source)/sizeof(*doppler_source);++i)
        doppler_source[i]=(int16_t)((i*173)%65001-32500);
    qa_audio_sample *doppler_raw=NULL,*doppler_pcm=NULL;
    CHECK(qa_audio_sample_copy(doppler_source,2177,1,44100,QA_AUDIO_NO_LOOP,&doppler_raw,&error));
    CHECK(qa_audio_resample_source(doppler_raw,44100,QA_GAME_Q3,&doppler_pcm,&error));
    CHECK(doppler_pcm->doppler_period==3072);
    size_t ranges[][2]={{0,0},{0,1},{0,64},{1,65},{63,129},{64,128},{17,2049},{2176,2177},{2177,3072},{2000,3072}};
    for(size_t i=0;i<sizeof(ranges)/sizeof(*ranges);++i){
        int64_t expected=0;
        for(size_t j=ranges[i][0];j<ranges[i][1] && j<doppler_pcm->frame_count;++j)
            expected+=doppler_pcm->samples[j];
        CHECK(qa_audio_sample_doppler_sum(doppler_pcm,ranges[i][0],ranges[i][1])==(double)expected);
    }
    qa_audio_sample_release(doppler_raw);qa_audio_sample_release(doppler_pcm);
    for(size_t i=0;i<sizeof(families)/sizeof(*families);++i){
        qa_audio_asset *absent=NULL;
        CHECK(qa_audio_bank_register(bank,"optional.wav",families[i],&absent,&error) && !absent);
        CHECK(qa_audio_bank_register(bank,"sound/optional.wav",families[i],&absent,&error) && !absent);
    }
    char optional[160];CHECK(snprintf(optional,sizeof(optional),"%s/optional.wav",folder)>0);
    fd=open(optional,O_WRONLY|O_CREAT|O_EXCL,0600);CHECK(fd>=0);
    CHECK(write(fd,wave,sizeof(wave))==(ssize_t)sizeof(wave));CHECK(close(fd)==0);
    CHECK(qa_vfs_mount_directory(view,directory,QA_ARCHIVE_EXACT,false,&mount,&error));
    for(size_t i=0;i<sizeof(families)/sizeof(*families);++i){
        qa_audio_asset *asset=NULL;
        CHECK(qa_audio_bank_register(bank,"optional.wav",families[i],&asset,&error) && asset);
        qa_audio_asset_release(asset);
    }
    char player[160],male[160],jump[192];
    CHECK(snprintf(player,sizeof(player),"%s/player",folder)>0);CHECK(mkdir(player,0700)==0);
    CHECK(snprintf(male,sizeof(male),"%s/male",player)>0);CHECK(mkdir(male,0700)==0);
    CHECK(snprintf(jump,sizeof(jump),"%s/jump1.wav",male)>0);
    fd=open(jump,O_WRONLY|O_CREAT|O_EXCL,0600);CHECK(fd>=0);
    CHECK(write(fd,wave,sizeof(wave))==(ssize_t)sizeof(wave));CHECK(close(fd)==0);
    qa_audio_asset *first=NULL,*again=NULL;
    CHECK(qa_audio_bank_sexed(bank,"*jump1.wav","custom/grunt",&first,&error) && first);
    CHECK(qa_audio_bank_sexed(bank,"*jump1.wav","custom/grunt",&again,&error) && again==first);
    qa_audio_asset_release(first);qa_audio_asset_release(again);
    qa_string_id sound, skin, model;
    CHECK(qa_strings_intern_cstr(names,"*jump1.wav",&sound,&error));
    CHECK(qa_player_skin_bind(names,"custom/grunt",false,&skin,&model,&error));
    CHECK(!strcmp(qa_strings_cstr(names,model),"custom"));
    CHECK(qa_audio_bank_sexed_id(bank,sound,model,&first,&error) && first);
    CHECK(qa_player_skin_bind(names,"custom/other",false,&skin,&model,&error));
    CHECK(qa_audio_bank_sexed_id(bank,sound,model,&again,&error) && again==first);
    qa_audio_asset_release(first);qa_audio_asset_release(again);
    const char environment_json[] = "{\"environments\":[{\"reverbs\":["
        "{\"materials\":[\"WOOD\"],\"preset\":\"room\"},"
        "{\"materials\":[\"wood\"],\"preset\":\"generic\"},"
        "{\"materials\":\"*\",\"preset\":\"padded_cell\"},"
        "{\"materials\":[\"metal\"],\"preset\":\"generic\"}]}]}";
    qa_audio_environments *configuration = NULL;
    CHECK(qa_audio_environments_parse((qa_bytes){(const uint8_t *)environment_json,
        sizeof(environment_json)-1}, names, &configuration, &error));
    qa_string_id floor = qa_strings_find(names, (qa_bytes){(const uint8_t *)"wood", 4});
    CHECK(floor);
    qa_audio_environment *environment = NULL;
    CHECK(qa_audio_environment_create(configuration, audio_material_trace, &floor, &environment, &error));
    qa_audio_environments_destroy(configuration);
    qa_audio_environment_lerp(environment, 0);
    qa_audio_environment_update(environment, qa_v3(0,0,0), 1);
    CHECK(qa_audio_environment_params(environment)->decay_time == qa_audio_reverb_preset(2)->decay_time);
    floor = qa_strings_find(names, (qa_bytes){(const uint8_t *)"metal", 5});
    qa_audio_environment_update(environment, qa_v3(0,0,0), 2);
    CHECK(qa_audio_environment_params(environment)->decay_time == qa_audio_reverb_preset(1)->decay_time);
    CHECK(qa_strings_intern_cstr(names, "unknown-floor", &floor, &error));
    qa_audio_environment_update(environment, qa_v3(0,0,0), 3);
    CHECK(qa_audio_environment_params(environment)->decay_time == qa_audio_reverb_preset(1)->decay_time);
    qa_audio_environment_destroy(environment);
    qa_audio_bank_destroy(bank); qa_strings_destroy(names); qa_vfs_destroy(view); qa_resource_pool_destroy(resources);
    CHECK(unlink(jump)==0);CHECK(rmdir(male)==0);CHECK(rmdir(player)==0);CHECK(unlink(optional)==0);
    CHECK(!memcmp(held->samples, held_samples, sizeof(held_samples))); qa_audio_sample_release(held);
    CHECK(unlink(path) == 0); CHECK(rmdir(folder) == 0); CHECK(rmdir(directory) == 0);
}

static void test_campaign_unit(void)
{
    qa_error error={0};
    qa_strings *strings=NULL;
    qa_string_id content, other_content;
    CHECK(qa_strings_create(&strings,&error));
    CHECK(qa_strings_intern_cstr(strings,"q2-classic-baseq2",&content,&error));
    CHECK(qa_strings_intern_cstr(strings,"q2-rerelease-baseq2",&other_content,&error));
    qa_campaign_location base1, base2, other;
    CHECK(qa_campaign_location_make(strings,content,
        (qa_bytes){(const uint8_t *)"maps/base1.bsp",14},&base1,&error));
    CHECK(qa_campaign_location_make(strings,content,
        (qa_bytes){(const uint8_t *)"base2",5},&base2,&error));
    CHECK(qa_campaign_location_make(strings,other_content,
        (qa_bytes){(const uint8_t *)"base1",5},&other,&error));
    qa_campaign_unit *unit=qa_campaign_unit_create(strings,&error);
    CHECK(unit);
    qa_campaign_visit *visit=NULL;
    CHECK(qa_campaign_unit_stage(unit,base1,true,true,NULL,&visit,&error));
    CHECK(!qa_campaign_visit_restore(visit));
    CHECK(qa_campaign_visit_commit(visit,&error));
    qa_campaign_visit_destroy(visit);

    /* SV_GameMap_f retains the departed level with client actors excluded.
     * This kernel checks ownership and routing of that encoded image. */
    qa_buffer first={.data=malloc(1),.size=1}, second={.data=malloc(1),.size=1};
    CHECK(first.data && second.data);
    first.data[0]=1; second.data[0]=2;
    const uint8_t *first_bytes=first.data, *second_bytes=second.data;
    qa_campaign_world *first_world=NULL, *second_world=NULL;
    CHECK(qa_campaign_world_take(base1,&first,&first_world,&error));
    CHECK(!first.data && !first.size);
    CHECK(qa_campaign_world_take(base2,&second,&second_world,&error));
    CHECK(!second.data && !second.size);
    CHECK(qa_campaign_unit_stage(unit,base2,false,true,first_world,&visit,&error));
    CHECK(!qa_campaign_visit_restore(visit));
    CHECK(qa_campaign_visit_commit(visit,&error));
    qa_campaign_visit_destroy(visit);

    CHECK(qa_campaign_unit_stage(unit,base1,false,true,second_world,&visit,&error));
    CHECK(qa_campaign_visit_restore(visit)==first_world);
    CHECK(qa_campaign_world_bytes(qa_campaign_visit_restore(visit)).data==first_bytes);
    qa_campaign_visit_destroy(visit);
    qa_campaign_location current;
    CHECK(qa_campaign_unit_current(unit,&current) && qa_campaign_location_equal(current,base2));
    CHECK(qa_campaign_unit_stage(unit,base1,false,true,second_world,&visit,&error));
    CHECK(qa_campaign_visit_restore(visit)==first_world);
    CHECK(qa_campaign_visit_commit(visit,&error));
    qa_campaign_visit_destroy(visit);
    qa_campaign_unit_checkpoint checkpoint={0};
    CHECK(qa_campaign_unit_capture(unit,&checkpoint,&error));
    CHECK(checkpoint.count==1 && checkpoint.worlds[0]==second_world);
    CHECK(qa_campaign_world_bytes(checkpoint.worlds[0]).data==second_bytes);
    qa_campaign_unit_checkpoint_free(&checkpoint);

    /* SV_CheckForSavegame skips reloading for deathmatch or sv_noreload,
     * while classic SV_GameMap_f still retains other departed levels. */
    CHECK(qa_campaign_unit_stage(unit,base2,false,false,first_world,&visit,&error));
    CHECK(!qa_campaign_visit_restore(visit));
    CHECK(qa_campaign_visit_commit(visit,&error));
    qa_campaign_visit_destroy(visit);
    CHECK(qa_campaign_unit_capture(unit,&checkpoint,&error));
    CHECK(checkpoint.count==1 && checkpoint.worlds[0]==first_world);
    qa_campaign_unit_checkpoint_free(&checkpoint);

    CHECK(qa_campaign_unit_stage(unit,other,false,true,second_world,&visit,&error));
    CHECK(!qa_campaign_visit_restore(visit));
    CHECK(qa_campaign_visit_commit(visit,&error));
    qa_campaign_visit_destroy(visit);
    CHECK(qa_campaign_unit_capture(unit,&checkpoint,&error) && checkpoint.count==0);
    qa_campaign_unit_checkpoint_free(&checkpoint);
    CHECK(qa_campaign_unit_stage(unit,base1,false,true,NULL,&visit,&error));
    CHECK(qa_campaign_visit_commit(visit,&error));
    qa_campaign_visit_destroy(visit);
    CHECK(qa_campaign_unit_stage(unit,base2,false,true,first_world,&visit,&error));
    CHECK(qa_campaign_visit_commit(visit,&error));
    qa_campaign_visit_destroy(visit);
    CHECK(qa_campaign_unit_stage(unit,base1,true,true,second_world,&visit,&error));
    CHECK(!qa_campaign_visit_restore(visit));
    CHECK(qa_campaign_visit_commit(visit,&error));
    qa_campaign_visit_destroy(visit);
    CHECK(qa_campaign_unit_capture(unit,&checkpoint,&error) && checkpoint.count==0);
    qa_campaign_unit_checkpoint_free(&checkpoint);
    qa_campaign_world_release(first_world); qa_campaign_world_release(second_world);
    qa_campaign_unit_destroy(unit); qa_strings_destroy(strings);
}

static qa_save_image *recovery_image(uint64_t elapsed, uint8_t value)
{
    qa_error error = {0};
    qa_save_record records[QA_SAVE_PROVIDER];
    size_t count = 0;
    for (qa_save_owner_kind kind = QA_SAVE_STRINGS; kind < QA_SAVE_PROVIDER; ++kind) {
        switch (kind) {
        case QA_SAVE_CAMPAIGN: case QA_SAVE_CONNECTIONS: case QA_SAVE_PREDICTION:
        case QA_SAVE_PRESENTATION: case QA_SAVE_AUDIO: case QA_SAVE_INPUT: case QA_SAVE_MEDIA:
        case QA_SAVE_COMMANDS:
            continue;
        default: break;
        }
        records[count++] = (qa_save_record){.owner = {kind, "", "fixture", ""},
            .payload = {&value, 1}};
    }
    qa_save_metadata metadata = {.purpose = QA_SAVE_RECOVERY, .elapsed_ns = elapsed,
        .world_generation = 1, .map = "fixture", .game = "fixture"};
    qa_save_image *image = NULL;
    CHECK(qa_save_image_create(&metadata, records, count, &image, &error));
    return image;
}

static void test_recovery_checkpoints(void)
{
    qa_error error = {0};
    char directory[] = "/tmp/qa-recovery-XXXXXX";
    CHECK(mkdtemp(directory));
    qa_fs_root *root = NULL;
    CHECK(qa_fs_root_open(directory, &root, &error));
    qa_save_image *image = recovery_image(0, 73);
    qa_bytes first, again;
    CHECK(qa_save_image_encode(image, &first, &error));
    CHECK(qa_save_image_encode(image, &again, &error));
    CHECK(first.data == again.data && first.size == again.size);
    qa_recovery *recovery = NULL;
    CHECK(qa_recovery_begin(root, "recovery.qdemo", image, &recovery, &error));
    qa_demo *demo = NULL;
    CHECK(qa_demo_read(root, "recovery.qdemo", true, &demo, &error));
    CHECK(qa_demo_start_time(demo) == 0 && qa_demo_end_time(demo) == 0 && qa_demo_record_count(demo) == 1);
    const qa_demo_record *record = qa_demo_record_at(demo, 0);
    CHECK(record->payload.size == first.size && !memcmp(record->payload.data, first.data, first.size));
    qa_demo_destroy(demo);
    CHECK(qa_save_image_destroy_checked(&image, &error));
    image = recovery_image(UINT64_C(300000000000), 47);
    CHECK(qa_save_image_encode(image, &first, &error));
    CHECK(qa_recovery_checkpoint(recovery, image, &error));
    CHECK(qa_demo_read(root, "recovery.qdemo", true, &demo, &error));
    CHECK(qa_demo_record_count(demo) == 1 && qa_demo_start_time(demo) == UINT64_C(300000000000) &&
        qa_demo_end_time(demo) == qa_demo_start_time(demo));
    record = qa_demo_record_at(demo, 0);
    CHECK(record->payload.size == first.size && !memcmp(record->payload.data, first.data, first.size));
    qa_demo_destroy(demo);
    qa_fs_entry_kind kind;
    qa_fs_identity identity;
    CHECK(qa_fs_root_status(root, "recovery.qdemo", &kind, &identity, &error));
    CHECK(qa_fs_identity_size(&identity) == first.size + 84);
    CHECK(qa_save_image_destroy_checked(&image, &error));
    bool available = false;
    CHECK(qa_recovery_available(root, "recovery.qdemo", &available, &error) && available);
    CHECK(qa_recovery_close_clean(recovery, &error));
    CHECK(qa_recovery_available(root, "recovery.qdemo", &available, &error) && !available);
    qa_recovery_destroy(recovery);
    qa_fs_root_close(root);
    char path[sizeof(directory) + 32];
    CHECK(snprintf(path, sizeof(path), "%s/recovery.qdemo", directory) > 0);
    CHECK(unlink(path) == 0 && rmdir(directory) == 0);
}

static void test_q1_original_codec(void)
{
    qa_error error={0};qa_buffer bytes={0};qa_q1_save_data *decoded=NULL;
    qa_q1_save_data save={.version=5,.map="start",.skill=1,.time=2,.entity_count=600};
    save.entities=calloc(601,sizeof(*save.entities));CHECK(save.entities);
    qa_q1_save_value value={.kind=QA_Q1_SAVE_STRING,.value.text="worldspawn"};
    CHECK(qa_q1_save_record_value(save.entities,"classname",&value,&error));
    value.value.text="a\\n/b\nline";
    CHECK(qa_q1_save_record_value(save.entities,"message",&value,&error));
    value.value.text="player";
    CHECK(qa_q1_save_record_value(save.entities+1,"classname",&value,&error));
    value=(qa_q1_save_value){.kind=QA_Q1_SAVE_FLOAT,.value.number=100};
    CHECK(qa_q1_save_record_value(save.entities+1,"health",&value,&error));
    CHECK(qa_q1_save_comment(&save,"A level",7,23,&error));
    CHECK(strlen(save.comment)==39 && !memcmp(save.comment,"A_level",7));
    CHECK(!strcmp(save.comment+22,"kills:__7/_23____"));
    static const uint8_t trailer[]="/* FTE extension */";
    save.extension=(qa_buffer){.data=(uint8_t *)trailer,.size=sizeof(trailer)-1};
    CHECK(qa_q1_save_encode(&save,&bytes,&error));
    CHECK(qa_q1_save_decode((qa_bytes){bytes.data,bytes.size},&decoded,&error));
    CHECK(decoded->version==5 && decoded->entity_count==600 && !decoded->entities[599].count);
    CHECK(!decoded->extension.size && !strcmp(decoded->comment,save.comment));
    CHECK(!strcmp(decoded->entities[1].pairs[1].value,"100.000000"));
    char *text=NULL;
    CHECK(qa_q1_save_string_decode(decoded->entities[0].pairs[1].value,&text,&error));
    CHECK(!strcmp(text,"a\\n/b\nline"));free(text);
    qa_q1_save_destroy(decoded);qa_buffer_free(&bytes);
    qa_vec3 vector;uint32_t entity;
    CHECK(qa_q1_save_vector_decode("1e2 -2.5 3junk",&vector,&error));
    CHECK(vector.x==100 && vector.y==-2.5f && vector.z==3);
    CHECK(qa_q1_save_entity_decode(" +3junk",&entity,&error) && entity==3);
    CHECK(qa_q1_save_entity_decode("0x10",&entity,&error) && entity==0);
    save.entity_count=601;
    CHECK(!qa_q1_save_encode(&save,&bytes,&error) && error.code==QA_ERROR_UNSUPPORTED && !bytes.data);
    save.entity_count=600;
    char *message=save.entities[0].pairs[1].value;
    char long_value[1025];memset(long_value,'x',sizeof(long_value));long_value[1023]=0;
    save.entities[0].pairs[1].value=long_value;
    CHECK(qa_q1_save_encode(&save,&bytes,&error));qa_buffer_free(&bytes);
    long_value[1023]='x';long_value[1024]=0;
    CHECK(!qa_q1_save_encode(&save,&bytes,&error) && error.code==QA_ERROR_UNSUPPORTED && !bytes.data);
    save.entities[0].pairs[1].value="closing}brace";
    CHECK(!qa_q1_save_encode(&save,&bytes,&error) && error.code==QA_ERROR_UNSUPPORTED && !bytes.data);
    save.entities[0].pairs[1].value="quoted\"value";
    CHECK(!qa_q1_save_encode(&save,&bytes,&error) && error.code==QA_ERROR_UNSUPPORTED && !bytes.data);
    save.entities[0].pairs[1].value=message;
    long_value[1000]=0;
    value=(qa_q1_save_value){.kind=QA_Q1_SAVE_STRING,.value.text=long_value};
    for (unsigned i=0;i<33;++i)
        CHECK(qa_q1_save_record_value(save.entities+2,"message",&value,&error));
    CHECK(!qa_q1_save_encode(&save,&bytes,&error) && error.code==QA_ERROR_UNSUPPORTED && !bytes.data);
    for (size_t i=0;i<601;++i) qa_q1_save_record_destroy(save.entities+i);
    free(save.entities);free(save.comment);
}

void test_q1_gameplay(void);
void test_guest(void);
bool test_recovery_child(int, char **, int *);
void test_recovery(const char *);

typedef struct profiler_clock { double now; unsigned calls; } profiler_clock;
static double test_profiler_clock(void *context)
{
    profiler_clock *clock = context;
    ++clock->calls;
    return clock->now;
}
static void test_profiler_mode_changes(void)
{
    qa_error error = {0}; qa_profiler *profiler = NULL;
    profiler_clock clock = {0};
    CHECK(qa_profiler_create(test_profiler_clock, &clock, 8, &profiler, &error));
    CHECK(qa_profiler_push(profiler, "disabled_outer", &error));
    CHECK(qa_profiler_push(profiler, "disabled_inner", &error));
    CHECK(qa_profiler_enable(profiler, true, &error));
    CHECK(!qa_profiler_enabled(profiler) && qa_profiler_idle(profiler));
    CHECK(qa_profiler_pop(profiler, &error));
    CHECK(!qa_profiler_enabled(profiler));
    CHECK(qa_profiler_push(profiler, "disabled_sibling", &error));
    CHECK(qa_profiler_pop(profiler, &error));
    CHECK(!qa_profiler_enabled(profiler) && clock.calls == 1);
    CHECK(qa_profiler_pop(profiler, &error));
    CHECK(qa_profiler_enabled(profiler) && qa_profiler_idle(profiler));

    clock.now = 1;
    CHECK(qa_profiler_push(profiler, "outer", &error));
    clock.now = 2;
    CHECK(qa_profiler_push(profiler, "inner", &error));
    CHECK(qa_profiler_enable(profiler, false, &error));
    CHECK(qa_profiler_enabled(profiler) && !qa_profiler_idle(profiler));
    clock.now = 5;
    CHECK(qa_profiler_pop(profiler, &error));
    CHECK(qa_profiler_enabled(profiler));
    clock.now = 6;
    CHECK(qa_profiler_push(profiler, "sibling", &error));
    clock.now = 7;
    CHECK(qa_profiler_pop(profiler, &error));
    CHECK(qa_profiler_enabled(profiler));
    clock.now = 8;
    CHECK(qa_profiler_pop(profiler, &error));
    CHECK(!qa_profiler_enabled(profiler) && qa_profiler_idle(profiler));
    CHECK(!qa_profiler_pop(profiler, &error));
    CHECK(error.code == QA_ERROR_ARGUMENT);

    qa_arena scratch = {0}; const qa_timer_report *rows = NULL; size_t count = 0;
    CHECK(qa_profiler_report(profiler, &scratch, &rows, &count, &error));
    CHECK(count == 3);
    for (size_t i = 0; i < count; ++i) {
        CHECK(rows[i].calls == 1);
        if (!strcmp(rows[i].name, "outer")) CHECK(rows[i].total_ms == 7 && rows[i].self_ms == 3);
        else if (!strcmp(rows[i].name, "inner")) CHECK(rows[i].total_ms == 3 && rows[i].self_ms == 3);
        else CHECK(!strcmp(rows[i].name, "sibling") && rows[i].total_ms == 1 && rows[i].self_ms == 1);
    }
    qa_arena_destroy(&scratch);
    CHECK(qa_profiler_enable(profiler, true, &error));
    CHECK(!qa_profiler_pop(profiler, &error));
    CHECK(qa_profiler_push(profiler, "last_request", &error));
    CHECK(qa_profiler_enable(profiler, false, &error));
    CHECK(qa_profiler_enable(profiler, true, &error));
    CHECK(qa_profiler_pop(profiler, &error));
    CHECK(qa_profiler_enabled(profiler));
    CHECK(qa_profiler_destroy(profiler, &error));
}

static void test_source_nonmipped_transparency(void)
{
    qa_error error = {0};
    qa_scene_resources *resources = qa_scene_resources_create(NULL, &error);
    CHECK(resources);
    uint8_t tga[34] = {0};
    tga[2] = 2;
    qa_store_u16le(tga + 12, 2);
    qa_store_u16le(tga + 14, 2);
    tga[16] = 32;
    tga[17] = 8;
    for (size_t i = 0; i < 4; ++i) {
        tga[18 + i * 4] = 30;
        tga[19 + i * 4] = 20;
        tga[20 + i * 4] = 10;
        tga[21 + i * 4] = i ? 255 : 0;
    }
    qa_scene_image_options options = {.family = QA_GAME_Q3, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .usage = QA_IMAGE_USAGE_PICTURE,
        .source_q3 = true,
        .source_upload = {.color = {.device = {.color_bits = 24}, .gamma = 1, .intensity = 1}}};
    qa_scene_image *image = NULL;
    CHECK(qa_scene_image_decode_retained(resources, "nonmipped-font", "font.tga",
        (qa_bytes){tga, sizeof(tga)}, &options, &image, &error));
    CHECK(image->source_q3 && !image->source_mipmap && image->level_count == 1);
    CHECK(image->filter == QA_SCENE_LINEAR && image->kind == QA_SCENE_RGBA8);
    const uint8_t *pixels = image->levels[0].pixels;
    size_t transparent = 0;
    for (size_t i = 0; i < 4; ++i) transparent += pixels[i * 4 + 3] == 0;
    CHECK(transparent == 1);
    qa_scene_image_release(image);
    qa_scene_resources_destroy(resources);
}

static void test_cached_world_surface(void)
{
    qa_error error = {0};
    qa_scene_resources *resources = qa_scene_resources_create(NULL, &error);
    CHECK(resources);
    uint8_t base_pixels[16 * 16 * 4], light_pixels[16];
    for (size_t i = 0; i < 16 * 16; ++i) {
        base_pixels[i * 4] = 64; base_pixels[i * 4 + 1] = 128;
        base_pixels[i * 4 + 2] = 192; base_pixels[i * 4 + 3] = 255;
    }
    for (size_t i = 0; i < 4; ++i) {
        memset(light_pixels + i * 4, 128, 3); light_pixels[i * 4 + 3] = 255;
    }
    qa_scene_image_level base_level = {16, 16, base_pixels, sizeof(base_pixels)}, light_level = {2, 2, light_pixels, sizeof(light_pixels)};
    qa_scene_image *base = NULL, *light = NULL;
    CHECK(qa_scene_image_create(resources, "cached-base", QA_SCENE_RGBA8, &base_level, 1,
        QA_SCENE_REPEAT, QA_SCENE_NEAREST, (qa_vec4){0}, &base, &error));
    CHECK(qa_scene_image_create(resources, "cached-light", QA_SCENE_RGBA8, &light_level, 1,
        QA_SCENE_CLAMP, QA_SCENE_LINEAR, (qa_vec4){0}, &light, &error));
    uint8_t mip_pixels[1364]; qa_scene_image_level levels[5];
    size_t offset = 0;
    for (unsigned mip = 0; mip < 5; ++mip) {
        uint32_t width = 16u >> mip; size_t bytes = (size_t)width * width * 4;
        levels[mip] = (qa_scene_image_level){width, width, mip_pixels + offset, bytes};
        for (size_t i = 0; i < bytes / 4; ++i) {
            memset(mip_pixels + offset + i * 4, mip == 4 ? 200 : 64, 3);
            mip_pixels[offset + i * 4 + 3] = 255;
        }
        offset += bytes;
    }
    qa_scene_image *mipped = NULL;
    CHECK(qa_scene_image_create(resources, "cached-mips", QA_SCENE_RGBA8, levels, 5,
        QA_SCENE_REPEAT, QA_SCENE_NEAREST_MIPMAP_NEAREST, (qa_vec4){0}, &mipped, &error));
    qa_scene_vertex *vertices = calloc(4, sizeof(*vertices));
    uint32_t *indices = malloc(6 * sizeof(*indices));
    CHECK(vertices && indices);
    const qa_vec3 positions[4] = {{-.75f,-.75f,.2f},{.75f,-.75f,.2f},{.75f,.75f,.2f},{-.75f,.75f,.2f}};
    const qa_vec2 uv[4] = {{0,0},{1,0},{1,1},{0,1}};
    const uint32_t triangles[6] = {0,1,2,0,2,3};
    memcpy(indices, triangles, sizeof(triangles));
    for (size_t i = 0; i < 4; ++i) {
        vertices[i] = (qa_scene_vertex){.position = positions[i], .normal = {0,0,1},
            .texcoord = uv[i], .lightmap = {uv[i].x * .5f + .25f, uv[i].y * .5f + .25f}, .color = {1,1,1,1}};
    }
    qa_scene_geometry *geometry = qa_scene_geometry_adopt(&(qa_scene_geometry_input){
        .vertices = vertices, .vertex_count = 4, .indices = indices, .index_count = 6}, &error);
    CHECK(geometry);
    qa_scene_mesh mesh = {.vertices = vertices, .vertex_count = 4, .indices = indices, .index_count = 6,
        .geometry = geometry, .identity = qa_scene_identity(), .revision = 1, .primitive = QA_SCENE_TRIANGLES};
    qaw_brush_geometry brush = {0};
    CHECK(qaw_brush_prepare(&mesh, light, &brush, &error));
    CHECK(brush.draw.present && brush.draw.polygon_vertices == 4 && !brush.draw.part_count);
    qa_cpu_options options; qa_cpu_options_default(&options);
    options.width = options.height = 64; options.owner = 97;
    qa_cpu_renderer *renderer = qa_cpu_create(&options, &error);
    CHECK(renderer && qa_cpu_statistics_enable(renderer, true, &error));
    qa_scene_frame frame;
    CHECK(qa_scene_frame_init(&frame, 97, 1024 * 1024, &error));
    qa_scene_matrix identity; qa_scene_matrix_identity(&identity);
    qa_scene_draw draw = {.mesh = mesh, .model = identity, .mvp = identity,
        .textures = {base, light}, .texture_count = 2, .environment = QA_TEXTURE_MODULATE, .lighting = QA_LIGHT_VERTEX,
        .vertex_inputs = {.constant_color = true, .color = {1,1,1,1}}, .single_coverage = true};
    qa_scene_state_default(&draw.state); draw.state.cull = QA_CULL_NONE;
    qa_material_stage stages[2] = {{.images = &base, .image_count = 1, .tcgen = QA_TC_TEXTURE,
        .rgb = QA_COLOR_IDENTITY, .alpha = QA_COLOR_IDENTITY},
        {.images = &light, .image_count = 1, .lightmap = true, .is_lightmap = true, .tcgen = QA_TC_LIGHTMAP,
         .rgb = QA_COLOR_IDENTITY, .alpha = QA_COLOR_IDENTITY}};
    stages[0].state = stages[1].state = draw.state;
    stages[1].state.blend_source = QA_BLEND_DST_COLOR;
    stages[1].state.blend_destination = QA_BLEND_ZERO;
    qa_material material = {.stages = stages, .stage_count = 2, .cull = QA_CULL_NONE,
        .profile = {.multitexture = true}, .family = QA_GAME_Q3};
    qa_material_context context = {.model = identity, .lightmap = light, .brush = &brush.draw,
        .identity_light = 1, .entity_color = {1,1,1,1}};
    context.view.projection = identity; context.view.axis[0] = (qa_vec3){0,0,-1};
    context.view.axis[1] = (qa_vec3){-1,0,0}; context.view.axis[2] = (qa_vec3){0,1,0};
    qa_scene_brush_surface batch_surface = brush.draw;
    qa_scene_brush_surface batch = {.present = true, .chain = &batch_surface};
    uint8_t reference[64 * 64 * 4];
    for (unsigned pass = 0; pass < 5; ++pass) {
        qa_scene_frame_reset(&frame, pass + 1);
        qa_scene_command view = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = {
            .viewport = {0,0,64,64}, .projection = identity,
            .clear_color = true, .clear_depth = true, .depth = 1}};
        CHECK(qa_scene_frame_emit(&frame, &view, &error));
        if (pass >= 3) {
            draw.textures[0] = mipped;
            draw.mvp.m[0] = draw.mvp.m[5] = 1.0f / 48;
            draw.mvp.m[12] = 1.0f / 64; draw.mvp.m[13] = -1.0f / 64;
            if (pass == 4) draw.brush = brush.draw;
        }
        if (!pass || pass >= 3) CHECK(qa_scene_frame_draw(&frame, &draw, &error));
        else {
            context.brush = pass == 2 ? &batch : &brush.draw;
            CHECK(qa_material_submit(&material, &mesh, &context, &frame, &error));
            CHECK(frame.command_count == 2 && frame.commands[1].data.draw.brush.present);
            CHECK((frame.commands[1].data.draw.brush.chain != NULL) == (pass == 2));
        }
        if (pass == 4) {
            qa_scene_geometry_release(geometry); geometry = NULL;
            qaw_brush_destroy(&brush);
        }
        CHECK(qa_cpu_execute(renderer, &frame, &error));
        qa_bytes pixels = qa_cpu_pixels(renderer);
        CHECK(pixels.size == sizeof(reference));
        if (!pass || pass == 3) {
            memcpy(reference, pixels.data, sizeof(reference));
            CHECK(reference[(32 * 64 + 32) * 4] == (pass == 3 ? 100 : 32));
        }
        else for (size_t i = 0; i < sizeof(reference); ++i)
            CHECK(abs((int)pixels.data[i] - reference[i]) <= 1);
    }
    qa_cpu_statistics statistics;
    CHECK(qa_cpu_statistics_read(renderer, &statistics, &error));
    CHECK(statistics.brush_written && statistics.surface_builds == 2);
    qa_scene_frame_destroy(&frame);
    qaw_brush_destroy(&brush);
    qa_cpu_destroy(renderer);
    qa_scene_geometry_release(geometry);
    qa_scene_image_release(base); qa_scene_image_release(light); qa_scene_image_release(mipped);
    qa_scene_resources_destroy(resources);
}

static void test_retained_patch_levels(void)
{
    qa_error error = {0};
    qa_scene_vertex *vertices = calloc(9, sizeof(*vertices));
    uint32_t *indices = malloc(24 * sizeof(*indices));
    CHECK(vertices && indices);
    for (unsigned y = 0; y < 3; ++y) for (unsigned x = 0; x < 3; ++x)
        vertices[y * 3 + x] = (qa_scene_vertex){.position = {(float)x, (float)y, x == 1 ? .5f : 0},
            .normal = {0,0,1}, .texcoord = {(float)x * .5f, (float)y * .5f}, .color = {1,1,1,1}};
    size_t at = 0;
    for (unsigned y = 0; y < 2; ++y) for (unsigned x = 0; x < 2; ++x) {
        uint32_t a = y * 3 + x, b = a + 3;
        indices[at++] = a; indices[at++] = b; indices[at++] = a + 1;
        indices[at++] = a + 1; indices[at++] = b; indices[at++] = b + 1;
    }
    qa_scene_geometry *geometry = qa_scene_geometry_adopt(&(qa_scene_geometry_input){.vertices = vertices,
        .vertex_count = 9, .indices = indices, .index_count = 24}, &error);
    CHECK(geometry);
    qaw_surface surface = {.vertices = vertices, .indices = indices, .mesh = {.vertices = vertices,
        .indices = indices, .vertex_count = 9, .index_count = 24, .geometry = geometry,
        .identity = qa_scene_identity(), .revision = 1, .primitive = QA_SCENE_TRIANGLES}};
    surface.patch = calloc(1, sizeof(*surface.patch)); CHECK(surface.patch);
    surface.patch->width = surface.patch->height = 3;
    surface.patch->width_error[1] = .25f; surface.patch->height_error[1] = .5f;
    surface.patch->stitched = surface.patch->fixed = true;
    qa_scene_world world = {.surface_count = 1, .surfaces = &surface};
    CHECK(qaw_patch_prepare(&world, &error));
    CHECK(surface.patch->level_count == 3);
    qa_material_context context = {0}; qa_scene_matrix_identity(&context.model);
    context.view.axis[0] = (qa_vec3){1,0,0};
    const float errors[3] = {0,.25f,.5f};
    for (size_t i = 0; i < 3; ++i) {
        qa_scene_mesh first, second;
        qaw_patch_lod(&surface, &context, errors[i], &first);
        CHECK(first.vertex_count == (i == 0 ? 4 : i == 1 ? 6 : 9));
        CHECK(context.source_grid_columns == (i == 0 ? 2 : 3));
        CHECK(context.source_grid_rows == (i < 2 ? 2 : 3));
        CHECK(context.brush && context.brush->present);
        qaw_patch_lod(&surface, &context, errors[i], &second);
        CHECK(first.geometry == second.geometry && first.vertices == second.vertices && first.indices == second.indices);
        CHECK(first.identity == second.identity && first.identity != 0);
    }
    qaw_patch_destroy(surface.patch);
    qa_scene_geometry_release(geometry);
}

static void test_shared_cvar_archive(void)
{
    qa_error error={0};
    qa_cvar_options options={.dialect=QA_RULESET_NETQUAKE,.side=QA_CVAR_SIDE_CLIENT,.role=QA_CVAR_ROLE_ENGINE};
    qa_cvars *engine=qa_cvars_create(&options,&error);
    CHECK(engine && qa_cvars_register(engine,"fov","90",QA_CVAR_ARCHIVE,0,"",&error));
    options.role=QA_CVAR_ROLE_CGAME;
    qa_cvars *client=qa_cvars_create_view(engine,&options,&error);
    CHECK(client && qa_cvars_register(client,"fov","90",QA_CVAR_ARCHIVE,0,"",&error));
    float original=qa_cvars_find(engine,"fov")->number;
    qa_cvars_edit *edit=NULL;
    CHECK(qa_cvars_edit_prepare(engine,&edit,&error));
    qa_cvar_archive_entry entry={.name="fov",.value="110"};
    qa_cvar_archive archive={.entries=&entry,.count=1};
    CHECK(qa_cvar_archive_apply(client,&archive,&error));
    CHECK(qa_cvars_find(engine,"cg_fov")->number==110 && qa_cvars_find(client,"fov")->number==110);
    qa_cvars_edit_abort(edit); edit=NULL;
    CHECK(qa_cvars_find(engine,"fov")->number==original && qa_cvars_find(client,"cg_fov")->number==original);
    CHECK(qa_cvars_edit_prepare(engine,&edit,&error));
    entry.value="120";
    CHECK(qa_cvar_archive_apply(client,&archive,&error) && qa_cvars_edit_ready(edit,&error));
    qa_cvars_edit_publish(edit);
    CHECK(qa_cvars_edit_finish(engine,&error));
    CHECK(qa_cvars_find(engine,"fov")->number==120 && qa_cvars_find(client,"cg_fov")->number==120);
    qa_cvars_destroy(client); qa_cvars_destroy(engine);
}

static void test_cvar_info_storage(void)
{
    const char *text="\\name\\first\\Name\\folded\\name\\last\\empty\\\\orphan";
    qa_bytes key={(const uint8_t *)"name",4},info_span;
    CHECK(qa_info_find(text,key,0,&info_span) && info_span.size==5 && !memcmp(info_span.data,"first",5));
    CHECK(qa_info_find(text,key,QA_INFO_LAST,&info_span) && info_span.size==4 && !memcmp(info_span.data,"last",4));
    CHECK(qa_info_find("Name\\folded",key,QA_INFO_CASE_FOLD,&info_span) && info_span.size==6 && !memcmp(info_span.data,"folded",6));
    CHECK(qa_info_find(text,(qa_bytes){(const uint8_t *)"empty",5},0,&info_span) && info_span.size==0);
    CHECK(!qa_info_find(text,(qa_bytes){(const uint8_t *)"orphan",6},0,&info_span) && info_span.size==0);
    qa_error info_error={0};char too_small[4];
    CHECK(!qa_q3_info_value(text,"NAME",too_small,sizeof(too_small),&info_error));
    for (qa_ruleset_id dialect = QA_RULESET_NETQUAKE; dialect <= QA_RULESET_Q3; ++dialect) {
        qa_error error = {0};
        qa_cvars *cvars = qa_cvars_create(&(qa_cvar_options){.dialect = dialect,
            .side = QA_CVAR_SIDE_SERVER}, &error);
        CHECK(cvars && qa_cvars_register(cvars, "qa_info_first", "first", QA_CVAR_SERVERINFO, 0, "", &error));
        CHECK(qa_cvars_register(cvars, "qa_info_second", "second", QA_CVAR_SERVERINFO, 0, "", &error));
        CHECK(qa_cvars_register(cvars, "qa_info_bytes", "\301text", QA_CVAR_SERVERINFO, 0, "", &error));
        char small[1024], large[8192], value[64];
        CHECK(qa_cvars_info_write(cvars, QA_CVAR_SERVERINFO, sizeof(small), small, &error));
        CHECK(qa_cvars_info_write(cvars, QA_CVAR_SERVERINFO, sizeof(large), large, &error));
        CHECK(qa_q3_info_value(small, "qa_info_first", value, sizeof(value), &error) && !strcmp(value, "first"));
        CHECK(qa_q3_info_value(small, "qa_info_second", value, sizeof(value), &error) && !strcmp(value, "second"));
        CHECK(qa_q3_info_value(small, "qa_info_bytes", value, sizeof(value), &error));
        CHECK(!strcmp(value, dialect == QA_RULESET_Q3 ? "\301text" : "Atext"));
        const char *small_first = strstr(small, "\\qa_info_first\\"), *small_second = strstr(small, "\\qa_info_second\\");
        const char *large_first = strstr(large, "\\qa_info_first\\"), *large_second = strstr(large, "\\qa_info_second\\");
        CHECK(small_first && small_second && large_first && large_second);
        CHECK((small_first < small_second) == (dialect == QA_RULESET_Q3 ?
            large_first > large_second : large_first < large_second));
        qa_buffer owned = {0};
        CHECK(qa_cvars_info(cvars, QA_CVAR_SERVERINFO, sizeof(small), &owned, &error));
        CHECK(owned.size == strlen(small) && !memcmp(owned.data, small, owned.size + 1));
        qa_buffer_free(&owned); qa_cvars_destroy(cvars);
    }
}

static qa_input_seat *shared_input_seat(void *user, const qa_command_context *context)
{
    return context->seat == 0 ? *(qa_input_seat **)user : NULL;
}

static bool shared_input_menu(void *user, qa_input_seat *seat, qa_input_focus focus,
    const qa_input_event *event)
{
    (void)user; (void)seat; (void)focus; (void)event;
    return false;
}

static void test_shared_input_menu_defaults(void)
{
    qa_error error = {0};
    qa_cvars *cvars = qa_cvars_create(&(qa_cvar_options){.dialect = QA_RULESET_Q3,
        .side = QA_CVAR_SIDE_CLIENT}, &error);
    CHECK(cvars);
    for (qa_ruleset_id dialect = QA_RULESET_NETQUAKE; dialect <= QA_RULESET_Q3; ++dialect) {
        CHECK(qa_cvars_select_dialect(cvars, dialect, &error));
        CHECK(qa_cvars_find(cvars, "in_nograb")->integer == 0);
        CHECK(!qa_cvars_find(cvars, "in_nograb")->explicit_value);
        CHECK(qa_cvars_find(cvars, "in_mouse")->integer == 1);
        qa_command_context context = {.dialect = dialect, .origin = QA_COMMAND_SEAT,
            .direct = true, .cvar_view = qa_cvars_view_identity(cvars)};
        qa_console *console = qa_console_create(&(qa_console_options){.context = context,
            .cvars = cvars, .disable_builtins = true}, &error);
        CHECK(console);
        qa_input_seat *seat = qa_input_seat_create(&(qa_input_seat_options){.context = context,
            .console = console, .cvars = cvars, .gamepad = qa_gamepad_defaults()}, &error);
        CHECK(seat);
        qa_input_console *commands = qa_input_console_create(&(qa_input_console_options){
            .console = console, .owner = 1, .user = &seat, .seat = shared_input_seat}, &error);
        CHECK(commands);
        if(dialect==QA_RULESET_Q2_CLASSIC || dialect==QA_RULESET_Q2_RERELEASE) {
            CHECK(qa_cvars_set(cvars,"name","$skin",true,&error));
            CHECK(qa_cvars_set(cvars,"skin","+showscores 900",true,&error));
            CHECK(qa_console_execute_now(console,&context,"$name",&error));
            CHECK(qa_input_seat_action_active(seat,QA_INPUT_SCORES));
            CHECK(qa_console_execute_now(console,&context,"-showscores 900",&error));
            CHECK(!qa_input_seat_action_active(seat,QA_INPUT_SCORES));
            CHECK(qa_console_execute_now(console,&context,"+showscores $hand",&error));
            CHECK(qa_input_seat_action_active(seat,QA_INPUT_SCORES));
            CHECK(qa_console_execute_now(console,&context,"-showscores $hand",&error));
            CHECK(!qa_input_seat_action_active(seat,QA_INPUT_SCORES));
        }
        const char *aliases[] = {"+scores", "+showscores"};
        double time = 0;
        for (size_t alias = 0; alias < sizeof(aliases) / sizeof(*aliases); ++alias) {
            CHECK(qa_input_seat_bind(seat, &(qa_input_binding){.input = {.kind = QA_PHYSICAL_KEY,
                .code = QA_KEY_TAB}, .kind = QA_BIND_COMMAND, .command = aliases[alias]}, &error));
            qa_input_event key = {.kind = QA_INPUT_EVENT_KEY, .time_ms = time += 10, .down = true,
                .input = {.kind = QA_PHYSICAL_KEY, .code = QA_KEY_TAB}};
            size_t drained = 0;
            CHECK(qa_input_seat_event(seat, &key, NULL, &error));
            CHECK(qa_console_drain(console, 4096, &drained, &error));
            CHECK(qa_input_seat_action_active(seat, QA_INPUT_SCORES));
            key.repeat = true; key.time_ms = time += 10;
            CHECK(qa_input_seat_event(seat, &key, NULL, &error));
            CHECK(qa_console_drain(console, 4096, &drained, &error));
            CHECK(qa_input_seat_action_active(seat, QA_INPUT_SCORES));
            key.down = false; key.repeat = false; key.time_ms = time += 10;
            CHECK(qa_input_seat_event(seat, &key, NULL, &error));
            CHECK(qa_console_drain(console, 4096, &drained, &error));
            CHECK(!qa_input_seat_action_active(seat, QA_INPUT_SCORES));
            for (unsigned transition = 0; transition < 2; ++transition) {
                key.down = true; key.time_ms = time += 10;
                CHECK(qa_input_seat_event(seat, &key, NULL, &error));
                CHECK(qa_console_drain(console, 4096, &drained, &error));
                CHECK(qa_input_seat_action_active(seat, QA_INPUT_SCORES));
                qa_input_ui_token token = 0;
                time += 10;
                CHECK(qa_input_seat_ui_push(seat, shared_input_menu, NULL, time, &token, &error));
                CHECK(qa_input_seat_set_focus(seat, QA_INPUT_UI, time, &error));
                CHECK(qa_console_drain(console, 4096, &drained, &error));
                CHECK(qa_input_seat_focus(seat) == QA_INPUT_UI);
                CHECK(!qa_input_seat_action_active(seat, QA_INPUT_SCORES));
                key.down = false; key.time_ms = time += 10;
                CHECK(qa_input_seat_event(seat, &key, NULL, &error));
                CHECK(qa_input_seat_ui_remove(seat, token, time += 10, &error));
                CHECK(qa_input_seat_focus(seat) == QA_INPUT_GAME);
                CHECK(!qa_input_seat_action_active(seat, QA_INPUT_SCORES));
            }
        }
        for (unsigned reuse=0;reuse<2;++reuse) {
            char queued[]="+showscores 900\n",script[]="queued.cfg";
            qa_command_context scripted=context;scripted.script=script;
            CHECK(qa_console_append(console,&scripted,queued,&error));
            queued[0]='X';script[0]='X';
            CHECK(qa_console_defer(console,&error));
            CHECK(qa_console_append(console,&context,"-showscores 900\n",&error));
            CHECK(qa_console_resume(console,&error));
            size_t drained=0;
            CHECK(qa_console_drain(console,1,&drained,&error));
            CHECK(drained==1 && qa_input_seat_action_active(seat,QA_INPUT_SCORES));
            CHECK(qa_console_drain(console,0,&drained,&error));
            CHECK(drained==1 && !qa_input_seat_action_active(seat,QA_INPUT_SCORES));
        }
        qa_input_console_destroy(commands);
        qa_input_seat_destroy(seat);
        qa_console_destroy(console);
    }
    CHECK(qa_cvars_set(cvars, "in_nograb", "1", true, &error));
    for (qa_ruleset_id dialect = QA_RULESET_NETQUAKE; dialect <= QA_RULESET_Q3; ++dialect) {
        CHECK(qa_cvars_select_dialect(cvars, dialect, &error));
        CHECK(qa_cvars_find(cvars, "in_nograb")->integer == 1);
        CHECK(qa_cvars_find(cvars, "in_nograb")->explicit_value);
    }
    qa_cvars_destroy(cvars);
}

static void test_platform_event_retirement(void)
{
    qa_error error = {0};
    qa_platform_events *events = qa_platform_events_create(&error);
    CHECK(events);
    const uint8_t packet[] = {1, 2, 3, 4};
    qa_sys_event pending = {.kind = QA_PLATFORM_EVENT_PACKET, .time_ns = 7};
    qa_sys_event release = {.kind = QA_PLATFORM_EVENT_KEY, .time_ns = 8,
        .data.key = {.code = 44, .down = false}};
    CHECK(qa_platform_events_push(events, &pending, (qa_bytes){packet, sizeof(packet)},
        (qa_bytes){0}) == QA_PLATFORM_EVENT_ACCEPTED);
    for (size_t i = 0; i < 4096; ++i) {
        CHECK(qa_platform_events_push(events, &release, (qa_bytes){0}, (qa_bytes){0}) == QA_PLATFORM_EVENT_ACCEPTED);
        qa_platform_event_cursor cursor = {0}; qa_sys_event event; qa_bytes bytes;
        CHECK(qa_platform_events_read(events, &cursor, &event, &bytes));
        CHECK(event.kind == QA_PLATFORM_EVENT_PACKET && event.time_ns == 7);
        CHECK(bytes.size == sizeof(packet) && !memcmp(bytes.data, packet, sizeof(packet)));
        CHECK(qa_platform_events_read(events, &cursor, &event, &bytes));
        CHECK(event.kind == QA_PLATFORM_EVENT_KEY && !event.data.key.down && event.data.key.code == 44);
        qa_platform_events_consume(events, &cursor);
        CHECK(!qa_platform_events_read(events, &cursor, &event, &bytes));
        CHECK(qa_platform_events_statistics(events).records == 1);
    }
    qa_platform_event_cursor cursor = {0}; qa_sys_event event; qa_bytes bytes;
    CHECK(qa_platform_events_read(events, &cursor, &event, &bytes));
    CHECK(bytes.size == sizeof(packet) && !memcmp(bytes.data, packet, sizeof(packet)));
    qa_platform_events_consume(events, &cursor);
    CHECK(qa_platform_events_statistics(events).records == 0);
    CHECK(qa_platform_events_statistics(events).bytes == 0);
    qa_platform_events_reset(events);
    while (qa_platform_events_push(events, &pending, (qa_bytes){packet, sizeof(packet)},
        (qa_bytes){0}) == QA_PLATFORM_EVENT_ACCEPTED) {}
    CHECK(qa_platform_events_statistics(events).records > 0);
    CHECK(qa_platform_events_push(events, &release, (qa_bytes){0}, (qa_bytes){0}) == QA_PLATFORM_EVENT_ACCEPTED);
    CHECK(qa_platform_events_frame(events, 9) == QA_PLATFORM_EVENT_ACCEPTED);
    cursor = (qa_platform_event_cursor){0};
    size_t packets = 0, releases = 0, clocks = 0;
    while (qa_platform_events_read(events, &cursor, &event, &bytes)) {
        if (event.kind == QA_PLATFORM_EVENT_PACKET) {
            ++packets;
            CHECK(bytes.size == sizeof(packet) && !memcmp(bytes.data, packet, sizeof(packet)));
        } else if (event.kind == QA_PLATFORM_EVENT_KEY) ++releases;
        else if (event.kind == QA_PLATFORM_EVENT_TIME) { ++clocks; CHECK(event.time_ns == 9); CHECK(event.data.time.utc_ms > 0); }
        qa_platform_events_consume(events, &cursor);
    }
    CHECK(packets && releases == 1 && clocks == 1);
    CHECK(qa_platform_events_statistics(events).records == 0);
    qa_platform_events_reset(events);
    uint8_t large[65536] = {0};
    qa_sys_event line = {.kind = QA_PLATFORM_EVENT_CONSOLE_LINE};
    while (qa_platform_events_push(events, &line, (qa_bytes){large, sizeof(large)},
        (qa_bytes){0}) == QA_PLATFORM_EVENT_ACCEPTED) {}
    CHECK(qa_platform_events_statistics(events).records > 0);
    CHECK(qa_platform_events_statistics(events).records < QA_PLATFORM_EVENT_CAPACITY - 1);
    CHECK(qa_platform_events_frame(events, 10) == QA_PLATFORM_EVENT_ACCEPTED);
    cursor = (qa_platform_event_cursor){0};
    clocks = 0;
    while (qa_platform_events_read(events, &cursor, &event, &bytes)) {
        if (event.kind == QA_PLATFORM_EVENT_TIME) { ++clocks; CHECK(event.time_ns == 10); }
        qa_platform_events_consume(events, &cursor);
    }
    CHECK(clocks == 1);
    qa_platform_events_destroy(events);
}

static void test_q2_command_angles(void)
{
    const qa_net_protocol protocols[] = {QA_NET_Q2_34, QA_NET_Q2KEX_2023, QA_NET_Q2KEX_DEMO_2022};
    for (size_t p = 0; p < sizeof(protocols) / sizeof(*protocols); ++p) {
        bool floating = protocols[p] != QA_NET_Q2_34;
        qa_error error = {0}; qa_q2_codec codec;
        CHECK(qa_q2_codec_init(&codec, (qa_net_protocol_id){.kind = protocols[p]}, &error));
        qa_usercmd source = {.kind = floating ? QA_RULESET_Q2_RERELEASE : QA_RULESET_Q2_CLASSIC,
            .angles = {0.1234567f, -17.89123f, 279.65432f}, .angle_words = {-32768, 1, 32767}};
        qa_q2_usercmd first = {.msec = 25}, previous = {0}, second;
        qa_q2_usercmd_angles_from_engine(&first, &source);
        uint8_t bytes[128]; qa_net_writer writer; qa_net_reader reader;
        qa_net_writer_init(&writer, bytes, sizeof(bytes), &error);
        CHECK(qa_q2_write_usercmd(&codec, &writer, NULL, &first));
        if (floating) CHECK(qa_net_writer_size(&writer) == 14);
        else CHECK(qa_net_writer_size(&writer) == 9);
        qa_net_reader_init(&reader, (qa_bytes){bytes, qa_net_writer_size(&writer)}, &error);
        CHECK(qa_q2_read_usercmd(&codec, &reader, NULL, &previous));
        CHECK(qa_net_reader_finish(&reader));
        qa_vec3 actual = qa_q2_usercmd_angles(&previous);
        if (floating) {
            CHECK(!memcmp(&actual, &source.angles, sizeof(actual)));
            source.angles.x += 0.000001f;
            qa_q2_usercmd_angles_from_engine(&first, &source);
            CHECK(first.angles[0] == previous.angles[0]);
        } else CHECK(!memcmp(previous.angles, first.angles, sizeof(first.angles)));
        qa_net_writer_init(&writer, bytes, sizeof(bytes), &error);
        CHECK(qa_q2_write_usercmd(&codec, &writer, &previous, &first));
        if (floating) CHECK(qa_net_writer_size(&writer) == 6 && bytes[0] == 1);
        qa_net_reader_init(&reader, (qa_bytes){bytes, qa_net_writer_size(&writer)}, &error);
        CHECK(qa_q2_read_usercmd(&codec, &reader, &previous, &second));
        CHECK(qa_net_reader_finish(&reader));
        actual = qa_q2_usercmd_angles(&second);
        if (floating) CHECK(!memcmp(&actual, &source.angles, sizeof(actual)));
        else CHECK(!memcmp(second.angles, first.angles, sizeof(first.angles)));
    }
}

static qa_net_send_result dosbox_test_send(void *context,const qa_net_address *to,qa_bytes bytes,qa_error *error)
{ (void)to;(void)bytes;(void)error;++*(size_t *)context;return QA_NET_SEND_ACCEPTED; }
static bool dosbox_test_collect(void *context,uint64_t now,qa_net_transport_event *event,qa_error *error)
{ (void)context;(void)now;(void)error;*event=(qa_net_transport_event){0};return true; }
static void dosbox_test_close(void *context) { (void)context; }
static void test_dosbox_loopback(void)
{
    qa_error error={0};qa_net_dosbox *network=NULL;qa_net_transport *udp=NULL,*a=NULL,*b=NULL;
    size_t sends=0;
    qa_net_address server={.kind=QA_NET_IPV4,.port=213,.host.ipv4={127,0,0,1}};
    qa_net_address local_udp=server;local_udp.port=214;
    qa_net_transport_ops ops={.send=dosbox_test_send,.collect=dosbox_test_collect,.close=dosbox_test_close};
    CHECK(qa_net_transport_create(&local_udp,(qa_net_limits){1424,256},&ops,&sends,&udp,&error));
    CHECK(qa_net_dosbox_create(udp,&server,17,1000000,&network,&error) && sends==1);
    qa_net_address node={.kind=QA_NET_IPX,.port=2,.host.ipx={.network=1,.node={1,2,3,4,5,6}}};
    qa_net_ipx_packet packet={.from=node,.to=node};
    uint8_t bytes[1424];qa_net_writer writer;qa_net_writer_init(&writer,bytes,sizeof(bytes),&error);
    CHECK(qa_net_ipx_encode(&packet,&writer));
    qa_net_transport_event event={.packet={.kind=QA_NET_POLL_PACKET,.from=server,
        .received_ns=18,.payload={bytes,qa_net_writer_size(&writer)}}};
    bool registered=false;CHECK(qa_net_dosbox_dispatch(network,&event,&registered,&error) && registered);
    CHECK(qa_net_dosbox_bind(network,4000,4,&a,&error));
    CHECK(qa_net_dosbox_bind(network,4001,4,&b,&error));
    for(uint32_t i=0;i<256;++i)
        CHECK(qa_net_transport_send(a,qa_net_transport_address(b),(qa_bytes){(const uint8_t *)&i,4},&error)==QA_NET_SEND_ACCEPTED);
    uint32_t extra=256;
    CHECK(!qa_net_transport_send_ready(a,qa_net_transport_address(b)));
    CHECK(qa_net_transport_send(a,qa_net_transport_address(b),(qa_bytes){(const uint8_t *)&extra,4},&error)==QA_NET_SEND_FULL);
    qa_net_address broadcast=*qa_net_transport_address(b);memset(broadcast.host.ipx.node,255,6);
    CHECK(qa_net_transport_send(a,&broadcast,(qa_bytes){(const uint8_t *)&extra,4},&error)==QA_NET_SEND_FULL && sends==1);
    for(uint32_t i=0;i<256;++i) {
        qa_net_transport_event received;qa_net_datagram data;bool present=false;
        CHECK(qa_net_transport_collect(b,20+i,&received,&error));
        CHECK(qa_net_transport_dispatch(b,&received,&data,&present,&error) && present);
        uint32_t value=UINT32_MAX;CHECK(data.kind==QA_NET_POLL_PACKET && data.payload.size==4);
        memcpy(&value,data.payload.data,4);
        CHECK(value==i && data.received_ns==18 && qa_net_address_equal(&data.from,qa_net_transport_address(a),true));
    }
    CHECK(qa_net_transport_send(a,&broadcast,(qa_bytes){(const uint8_t *)&extra,4},&error)==QA_NET_SEND_ACCEPTED && sends==2);
    qa_net_transport_event received;CHECK(qa_net_transport_collect(b,1000,&received,&error));
    uint32_t value=0;memcpy(&value,received.packet.payload.data,4);CHECK(value==256);
    /* Tunnel packets route to the same endpoint and retain the physical stamp. */
    packet.to=*qa_net_transport_address(b);packet.from=*qa_net_transport_address(a);
    packet.from.host.ipx.node[5]=7;packet.payload=(qa_bytes){(const uint8_t *)&extra,4};
    qa_net_writer_init(&writer,bytes,sizeof(bytes),&error);CHECK(qa_net_ipx_encode(&packet,&writer));
    event.packet.payload=(qa_bytes){bytes,qa_net_writer_size(&writer)};event.packet.received_ns=1234;
    CHECK(qa_net_dosbox_dispatch(network,&event,&registered,&error));
    CHECK(qa_net_transport_collect(b,2000,&received,&error) && received.packet.received_ns==1234);
    CHECK(qa_net_address_equal(&received.packet.from,&packet.from,true));
    qa_net_transport_close(a);qa_net_transport_close(b);qa_net_dosbox_destroy(network);
}

static void test_loopback_admission(void)
{
    qa_error error = {0};
    qa_net_loopback *hub;
    qa_net_transport *a, *b;
    CHECK(qa_net_loopback_create((qa_net_limits){64008, 2}, &hub, &error));
    CHECK(qa_net_loopback_bind(hub, "a", &a, &error));
    CHECK(qa_net_loopback_bind(hub, "b", &b, &error));
    uint8_t message[64000];
    for (size_t i = 0; i < sizeof(message); ++i) message[i] = (uint8_t)i;
    CHECK(qa_net_transport_send(a, qa_net_transport_address(b), (qa_bytes){message, sizeof(message)},
        &error) == QA_NET_SEND_ACCEPTED);
    uint8_t next = 42, blocked = 43;
    CHECK(qa_net_transport_send(a, qa_net_transport_address(b), (qa_bytes){&next, 1},
        &error) == QA_NET_SEND_ACCEPTED);
    CHECK(!qa_net_transport_send_ready(a, qa_net_transport_address(b)));
    CHECK(qa_net_transport_send(a, qa_net_transport_address(b), (qa_bytes){&blocked, 1},
        &error) == QA_NET_SEND_FULL);
    CHECK(error.code == QA_OK && qa_net_transport_full_count(a) == 1);
    CHECK(qa_net_transport_ready(b));
    qa_net_transport_event event;
    CHECK(qa_net_transport_collect(b, 1, &event, &error));
    CHECK(event.packet.kind == QA_NET_POLL_PACKET && event.packet.payload.size == sizeof(message));
    CHECK(!memcmp(event.packet.payload.data, message, sizeof(message)));
    CHECK(qa_net_transport_send(a, qa_net_transport_address(b), (qa_bytes){&blocked, 1},
        &error) == QA_NET_SEND_ACCEPTED);
    CHECK(qa_net_transport_collect(b, 2, &event, &error) && event.packet.kind == QA_NET_POLL_PACKET);
    CHECK(event.packet.payload.size == 1 && event.packet.payload.data[0] == next);
    CHECK(qa_net_transport_collect(b, 3, &event, &error) && event.packet.kind == QA_NET_POLL_PACKET);
    CHECK(event.packet.payload.size == 1 && event.packet.payload.data[0] == blocked);
    CHECK(qa_net_transport_collect(b, 4, &event, &error) && event.packet.kind == QA_NET_POLL_EMPTY);
    qa_net_transport_close(a); qa_net_transport_close(b); qa_net_loopback_close(hub);
}
static void test_loopback_nq_signon(void)
{
    const qa_net_protocol protocols[] = {QA_NET_NQ15, QA_NET_FITZ666, QA_NET_RMQ999};
    for (size_t p = 0; p < sizeof(protocols) / sizeof(protocols[0]); ++p) {
        qa_error error = {0};
        qa_net_protocol_id protocol = {.kind = protocols[p]};
        qa_nq_options options = {.standard_quake = true};
        const char *models[192], *sounds[192];
        for (unsigned i = 0; i < 192; ++i) {
            models[i] = "progs/long_model_precache_name_for_signon.mdl";
            sounds[i] = "ambient/long_sound_precache_name_for_signon.wav";
        }
        uint8_t message[32768];
        qa_net_writer writer;
        qa_net_writer_init(&writer, message, sizeof(message), &error);
        qa_nq_message info = {.op = QA_NQ_SERVERINFO, .data.serverinfo = {.protocol = protocol,
            .max_clients = 1, .level = "start", .models = models, .model_count = 192,
            .sounds = sounds, .sound_count = 192}};
        CHECK(qa_nq_write(&writer, protocol, options, &info, NULL, 0));
        qa_nq_message signon = {.op = QA_NQ_SIGNON, .data.value = 1};
        CHECK(qa_nq_write(&writer, protocol, options, &signon, NULL, 0));
        size_t size = qa_net_writer_size(&writer);
        CHECK(size > 8000);
        qa_net_loopback *hub;
        qa_q1_peer server = {.kind = QA_Q1_PEER_NETQUAKE}, client = {.kind = QA_Q1_PEER_NETQUAKE};
        CHECK(qa_net_loopback_create((qa_net_limits){64008, 1}, &hub, &error));
        CHECK(qa_net_loopback_bind(hub, "server", &server.transport, &error));
        CHECK(qa_net_loopback_bind(hub, "client", &client.transport, &error));
        server.remote = *qa_net_transport_address(client.transport);
        client.remote = *qa_net_transport_address(server.transport);
        CHECK(qa_nq_channel_create(64000, 2048, &server.channel.nq, &error));
        CHECK(qa_nq_channel_create(64000, 2048, &client.channel.nq, &error));
        CHECK(qa_nq_channel_queue(server.channel.nq, (qa_bytes){message, size}, &error));
        uint8_t occupied = 99, saved[2056];
        CHECK(qa_net_transport_send(server.transport, &server.remote, (qa_bytes){&occupied, 1},
            &error) == QA_NET_SEND_ACCEPTED);
        bool present;
        qa_bytes packet;
        CHECK(qa_nq_channel_prepare(server.channel.nq, 1, &present, &packet, &error) && present);
        memcpy(saved, packet.data, packet.size);
        CHECK(!qa_q1_peer_send(&server, packet, 1, &error));
        CHECK(qa_net_transport_full_count(server.transport) == 1 && error.code == QA_OK);
        qa_net_transport_event event;
        CHECK(qa_net_transport_collect(client.transport, 2, &event, &error));
        CHECK(event.packet.payload.size == 1 && event.packet.payload.data[0] == occupied);
        CHECK(qa_nq_channel_prepare(server.channel.nq, 2, &present, &packet, &error) && present);
        CHECK(!memcmp(saved, packet.data, packet.size));
        CHECK(qa_q1_peer_send(&server, packet, 2, &error));
        CHECK(qa_net_transport_send(client.transport, &client.remote, (qa_bytes){&occupied, 1},
            &error) == QA_NET_SEND_ACCEPTED);
        unsigned deliveries = 0;
        for (unsigned fragment = 0; !qa_nq_channel_ready(server.channel.nq); ++fragment) {
            CHECK(fragment < 32);
            CHECK(qa_net_transport_collect(client.transport, fragment + 3, &event, &error));
            CHECK(event.packet.kind == QA_NET_POLL_PACKET);
            qa_q1_delivery delivery;
            CHECK(qa_q1_peer_receive(&client, &event.packet.from, event.packet.payload,
                fragment + 3, &delivery, &error));
            if (delivery.present) {
                ++deliveries;
                CHECK(delivery.payload.size == size && !memcmp(delivery.payload.data, message, size));
                qa_nq_decoder *decoder;
                CHECK(qa_nq_decoder_create(protocol, options, &decoder, &error));
                qa_net_reader reader;
                qa_net_reader_init(&reader, delivery.payload, &error);
                qa_nq_message decoded;
                CHECK(qa_nq_read(decoder, &reader, &decoded) && decoded.op == QA_NQ_SERVERINFO);
                CHECK(decoded.data.serverinfo.model_count == 192 && decoded.data.serverinfo.sound_count == 192);
                CHECK(qa_nq_read(decoder, &reader, &decoded) && decoded.op == QA_NQ_SIGNON && decoded.data.value == 1);
                CHECK(qa_net_reader_finish(&reader));
                qa_nq_decoder_destroy(decoder);
            }
            if (!fragment) {
                CHECK(client.reply_send_failed && qa_net_transport_full_count(client.transport) == 1);
                CHECK(qa_net_transport_collect(server.transport, 3, &event, &error));
                CHECK(event.packet.payload.size == 1 && event.packet.payload.data[0] == occupied);
                CHECK(qa_nq_channel_prepare(client.channel.nq, 3, &present, &packet, &error) && present);
                CHECK(qa_q1_peer_send(&client, packet, 3, &error));
            }
            CHECK(qa_net_transport_collect(server.transport, fragment + 4, &event, &error));
            CHECK(event.packet.kind == QA_NET_POLL_PACKET);
            CHECK(qa_q1_peer_receive(&server, &event.packet.from, event.packet.payload,
                fragment + 4, &delivery, &error));
        }
        CHECK(deliveries == 1);
        qa_nq_channel_destroy(server.channel.nq); qa_nq_channel_destroy(client.channel.nq);
        qa_net_transport_close(server.transport); qa_net_transport_close(client.transport);
        qa_net_loopback_close(hub);
    }
}

typedef struct kex_delivery {
    bool occupied, blocked;
    unsigned accepted;
    size_t size;
    uint8_t bytes[QA_KEX_DATAGRAM_BYTES];
} kex_delivery;
static qa_net_send_result kex_capture(void *context, qa_bytes bytes, qa_error *error)
{
    (void)error;
    kex_delivery *out = context;
    if (out->occupied || out->blocked) return QA_NET_SEND_FULL;
    CHECK(bytes.size <= sizeof(out->bytes));
    memcpy(out->bytes, bytes.data, bytes.size);
    out->size = bytes.size;
    out->occupied = true;
    ++out->accepted;
    return QA_NET_SEND_ACCEPTED;
}
static void test_kex_direct_delivery(void)
{
    qa_error error={0};qa_net_loopback *hub;qa_net_transport *server,*client;
    CHECK(qa_net_loopback_create((qa_net_limits){65535,8},&hub,&error));
    CHECK(qa_net_loopback_bind(hub,"server",&server,&error));
    CHECK(qa_net_loopback_bind(hub,"client",&client,&error));
    qa_net_address remote=*qa_net_transport_address(server);
    qa_kex_lan *lobby;
    CHECK(qa_kex_lan_open(client,&(qa_kex_lan_options){.local_players=1,.server=remote},&lobby,&error));
    kex_delivery wire={0};qa_kex_channel *sender;
    CHECK(qa_kex_channel_create(kex_capture,&wire,&sender,&error));
    uint8_t join[64];qa_net_writer writer;
    qa_net_writer_init(&writer,join,sizeof(join),&error);
    CHECK(qa_kex_write_string(&writer,"CRANTIME") && qa_kex_write_varint(&writer,0) && qa_kex_write_varint(&writer,1));
    CHECK(qa_kex_channel_send(sender,128,(qa_bytes){join,qa_net_writer_size(&writer)},QA_KEX_UNSEQUENCED,1,&error)==QA_NET_SEND_ACCEPTED);
    qa_net_datagram event={.kind=QA_NET_POLL_PACKET,.from=remote,.received_ns=1,.payload={wire.bytes,wire.size}},result;
    bool present;
    CHECK(qa_kex_lan_dispatch(lobby,&event,&result,&present,&error) && !present);
    CHECK(qa_kex_lan_admitted(lobby,&remote));
    wire.occupied=false;
    uint8_t message[5000];uint32_t random=1;
    for(size_t i=0;i<sizeof(message);++i) { random=random*1664525u+1013904223u;message[i]=(uint8_t)(random>>24); }
    CHECK(qa_kex_channel_send(sender,0,(qa_bytes){message,sizeof(message)},QA_KEX_SEQUENCED,2,&error)==QA_NET_SEND_ACCEPTED);
    unsigned deliveries=0;
    for(unsigned fragment=0;fragment<4;++fragment) {
        CHECK(qa_kex_channel_tick(sender,fragment+3,&error) && wire.occupied);
        event.received_ns=fragment+3;event.payload=(qa_bytes){wire.bytes,wire.size};
        CHECK(qa_kex_lan_dispatch(lobby,&event,&result,&present,&error));
        if(present) {
            ++deliveries;
            CHECK(result.kind==QA_NET_POLL_PACKET && result.received_ns==event.received_ns);
            CHECK(qa_net_address_equal(&result.from,&remote,true));
            CHECK(result.payload.size==sizeof(message) && !memcmp(result.payload.data,message,sizeof(message)));
        }
        wire.occupied=false;
    }
    CHECK(deliveries==1);
    memset(message,42,512);
    CHECK(qa_kex_channel_send(sender,0,(qa_bytes){message,512},QA_KEX_SEQUENCED,7,&error)==QA_NET_SEND_ACCEPTED && wire.occupied);
    event.received_ns=7;event.payload=(qa_bytes){wire.bytes,wire.size};
    CHECK(qa_kex_lan_dispatch(lobby,&event,&result,&present,&error) && present);
    CHECK(result.payload.size==512 && !memcmp(result.payload.data,message,512));
    event.kind=QA_NET_POLL_DROPPED;event.payload=(qa_bytes){0};
    CHECK(qa_kex_lan_dispatch(lobby,&event,&result,&present,&error) && present && result.kind==QA_NET_POLL_DROPPED);
    qa_kex_channel_destroy(sender);qa_kex_lan_close(lobby);
    qa_net_transport_close(server);qa_net_loopback_close(hub);
}
static void test_kex_send_admission(void)
{
    qa_error error = {0};
    kex_delivery wire = {.blocked = true}, acknowledgements = {.blocked = true};
    qa_kex_channel *sender, *receiver;
    CHECK(qa_kex_channel_create(kex_capture, &wire, &sender, &error));
    CHECK(qa_kex_channel_create(kex_capture, &acknowledgements, &receiver, &error));
    uint8_t message[5000];
    for (size_t i = 0; i < sizeof(message); ++i) message[i] = (uint8_t)i;
    CHECK(qa_kex_channel_send(sender, 128, (qa_bytes){message, sizeof(message)}, QA_KEX_SEQUENCED,
        1, &error) == QA_NET_SEND_ACCEPTED);
    CHECK(wire.accepted == 0);
    wire.blocked = false;
    unsigned deliveries = 0;
    for (unsigned fragment = 0; fragment < 4; ++fragment) {
        CHECK(qa_kex_channel_tick(sender, fragment + 2, &error) && wire.occupied);
        bool present;
        qa_kex_message received;
        CHECK(qa_kex_channel_receive(receiver, (qa_bytes){wire.bytes, wire.size}, fragment + 2,
            &received, &present, &error));
        if (present) {
            ++deliveries;
            CHECK(received.kind == 128 && received.payload.size == sizeof(message));
            CHECK(!memcmp(received.payload.data, message, sizeof(message)));
        }
        wire.occupied = false;
    }
    CHECK(deliveries == 1 && wire.accepted == 4);
    wire.blocked = true;
    CHECK(qa_kex_channel_send(sender, 128, (qa_bytes){message, 32}, QA_KEX_RELIABLE,
        10, &error) == QA_NET_SEND_ACCEPTED);
    for (unsigned retry = 0; retry < 50; ++retry)
        CHECK(qa_kex_channel_tick(sender, UINT64_C(500000000) * retry, &error));
    CHECK(wire.accepted == 4 && qa_kex_channel_reliable_receipt(sender).acknowledged == 0);
    wire.blocked = false;
    CHECK(qa_kex_channel_tick(sender, UINT64_C(25000000000), &error) && wire.occupied);
    bool present;
    qa_kex_message received;
    CHECK(qa_kex_channel_receive(receiver, (qa_bytes){wire.bytes, wire.size}, UINT64_C(25000000000),
        &received, &present, &error) && present);
    CHECK(qa_kex_channel_tick(receiver, UINT64_C(25000000001), &error));
    CHECK(!acknowledgements.occupied);
    acknowledgements.blocked = false;
    CHECK(qa_kex_channel_tick(receiver, UINT64_C(25000000002), &error) && acknowledgements.occupied);
    CHECK(qa_kex_channel_receive(sender, (qa_bytes){acknowledgements.bytes, acknowledgements.size},
        UINT64_C(25000000003), &received, &present, &error));
    CHECK(qa_kex_channel_reliable_receipt(sender).acknowledged == 1);
    qa_kex_channel_destroy(sender);
    qa_kex_channel_destroy(receiver);
}

static void test_nq_send_admission(void)
{
    qa_error error = {0};
    qa_nq_channel *sender, *receiver;
    uint8_t message[32768], saved[1032];
    for (size_t i = 0; i < sizeof(message); ++i) message[i] = (uint8_t)i;
    CHECK(qa_nq_channel_create(sizeof(message), 1024, &sender, &error));
    CHECK(qa_nq_channel_create(sizeof(message), 1024, &receiver, &error));
    CHECK(qa_nq_channel_queue(sender, (qa_bytes){message, sizeof(message)}, &error));
    unsigned deliveries = 0;
    for (unsigned i = 0; i < sizeof(message) / 1024; ++i) {
        bool present;
        qa_bytes first, retry, ack, next;
        CHECK(qa_nq_channel_prepare(sender, i + 1, &present, &first, &error) && present);
        memcpy(saved, first.data, first.size);
        CHECK(qa_nq_channel_prepare(sender, i + 2, &present, &retry, &error) && present);
        CHECK(first.size == retry.size && !memcmp(saved, retry.data, retry.size));
        qa_nq_channel_sent(sender, retry, i + 2);
        qa_q1_delivery delivery;
        CHECK(qa_nq_channel_receive(receiver, retry, i + 2, &delivery, &ack, &error));
        if (delivery.present) {
            ++deliveries;
            CHECK(delivery.reliable && delivery.payload.size == sizeof(message));
            CHECK(!memcmp(delivery.payload.data, message, sizeof(message)));
        }
        memcpy(saved, ack.data, ack.size);
        CHECK(qa_nq_channel_prepare(receiver, i + 3, &present, &retry, &error) && present);
        CHECK(retry.size == 8 && !memcmp(saved, retry.data, retry.size));
        CHECK(qa_nq_channel_prepare(receiver, i + 4, &present, &retry, &error) && present);
        CHECK(retry.size == 8 && !memcmp(saved, retry.data, retry.size));
        qa_nq_channel_sent(receiver, retry, i + 4);
        CHECK(qa_nq_channel_receive(sender, retry, i + 4, &delivery, &next, &error));
        CHECK(!delivery.present);
    }
    CHECK(deliveries == 1 && qa_nq_channel_ready(sender));
    qa_bytes first, retry;
    CHECK(qa_nq_channel_unreliable(sender, (qa_bytes){message, 1}, &first, &error));
    memcpy(saved, first.data, first.size);
    CHECK(qa_nq_channel_unreliable(sender, (qa_bytes){message, 1}, &retry, &error));
    CHECK(qa_nq_channel_unreliable_sequence(sender) == 0 && !memcmp(saved, retry.data, retry.size));
    qa_nq_channel_sent(sender, retry, 100);
    CHECK(qa_nq_channel_unreliable_sequence(sender) == 1);
    qa_nq_channel_destroy(sender);
    qa_nq_channel_destroy(receiver);
}

static void test_q3_send_admission(void)
{
    qa_error error = {0};
    qa_q3_channel *sender, *receiver;
    CHECK(qa_q3_channel_create(QA_Q3_SERVER, 0, &sender, &error));
    CHECK(qa_q3_channel_create(QA_Q3_CLIENT, 0, &receiver, &error));
    uint8_t message[2 * QA_Q3_FRAGMENT_BYTES];
    for (size_t i = 0; i < sizeof(message); ++i) message[i] = (uint8_t)i;
    CHECK(qa_q3_channel_begin(sender, (qa_bytes){message, sizeof(message)}, &error));
    for (unsigned fragment = 0; fragment < 3; ++fragment) {
        bool present;
        qa_bytes first, retry;
        uint8_t saved[QA_Q3_FRAGMENT_BYTES + 10];
        size_t remaining = qa_q3_channel_remaining(sender);
        CHECK(qa_q3_channel_prepare(sender, &present, &first, &error) && present);
        memcpy(saved, first.data, first.size);
        CHECK(qa_q3_channel_outgoing(sender) == 1);
        CHECK(qa_q3_channel_remaining(sender) == remaining);
        CHECK(qa_q3_channel_prepare(sender, &present, &retry, &error) && present);
        CHECK(retry.size == first.size && !memcmp(retry.data, saved, retry.size));
        qa_q3_packet received;
        CHECK(qa_q3_channel_receive(receiver, retry, &received, &error));
        CHECK(received.kind == (fragment == 2 ? QA_Q3_PACKET_MESSAGE : QA_Q3_PACKET_FRAGMENT));
        if (fragment == 2) CHECK(received.payload.size == sizeof(message) &&
            !memcmp(received.payload.data, message, sizeof(message)));
        qa_q3_channel_sent(sender, retry);
    }
    CHECK(!qa_q3_channel_pending(sender) && qa_q3_channel_outgoing(sender) == 2);
    qa_q3_channel_destroy(sender);
    qa_q3_channel_destroy(receiver);
}

static void test_q2_retained_records(void)
{
    qa_error error={0};
    qa_unified_frame_pool *pool=qa_unified_frame_pool_create(0,2,&error);
    CHECK(pool);
    for (size_t pass=0;pass<3;++pass) {
        q2_records batch={.lease=qa_unified_frame_lease_acquire(pool,&error)};
        CHECK(batch.lease);
        uint8_t raw[]={1,2,3};char text[]="notice";
        qa_q2_server_record record={.raw={raw,sizeof(raw)},
            .event={.kind=QA_Q2_SVC_PRINT,.data.print.text=text}};
        for (size_t i=0;i<65;++i) CHECK(q2_record_retain(&batch,&record,&error));
        raw[0]=9;text[0]='x';
        int16_t counts[]={3,7};
        record=(qa_q2_server_record){.event={.kind=QA_Q2_SVC_INVENTORY,
            .data.inventory={.counts=counts,.count=2}}};
        CHECK(q2_record_retain(&batch,&record,&error));counts[0]=0;
        if (pass==2) qa_unified_frame_pool_destroy(&pool);
        for (size_t i=0;i<65;++i) {
            CHECK(batch.records[i].raw.data[0]==1);
            CHECK(!strcmp(batch.records[i].event.data.print.text,"notice"));
        }
        CHECK(batch.records[65].event.data.inventory.counts[0]==3);
        q2_records_free(&batch);
    }
}
static void test_q2_owned_frames(void)
{
    const qa_net_protocol protocols[]={QA_NET_Q2_34,QA_NET_Q2REPRO_1038};
    for (size_t i=0;i<sizeof(protocols)/sizeof(*protocols);++i) {
        qa_error error={0};qa_q2_frame_history *history=NULL;
        CHECK(qa_q2_frame_history_create(2,&history,&error));
        uint8_t area[]={1,4};
        qa_q2_entity entity={.number=i?60000:1,.modelindex=1,.origin={1,2,3}};
        qa_q2_wire_frame frame={.valid=true,.server_frame=1,.delta_frame=-1,.player_count=1,
            .entities=&entity,.entity_count=1};
        frame.players[0].area_bits=(qa_bytes){area,sizeof(area)};
        CHECK(qa_q2_frame_history_accept(history,&frame,&error));
        const qa_q2_wire_frame *stored=qa_q2_frame_history_latest(history);CHECK(stored && stored->lease);
        qa_q2_wire_frame held={0};CHECK(qa_q2_frame_clone(stored,&held,&error));
        qa_q2_wire_frame *leased=qa_unified_frame_lease_alloc(stored->lease,1,sizeof(*leased),_Alignof(qa_q2_wire_frame),&error);
        CHECK(leased && qa_q2_frame_clone(stored,leased,&error));
        held.entities[0].origin[0]=9;CHECK(stored->entities[0].origin[0]==1);
        qa_q2_frame_history_clear(history);qa_q2_frame_history_destroy(history);history=NULL;
        CHECK(held.entities[0].origin[0]==9 && held.players[0].area_bits.data[1]==4);
        qa_q2_frame_free(&held);
        CHECK(leased->entities[0].origin[0]==1);qa_q2_frame_free(leased);
        CHECK(qa_q2_frame_history_create(2,&history,&error));
        qa_q2_codec encoder,decoder;
        CHECK(qa_q2_codec_init(&encoder,(qa_net_protocol_id){.kind=protocols[i]},&error));
        CHECK(qa_q2_codec_init(&decoder,(qa_net_protocol_id){.kind=protocols[i]},&error));
        uint8_t bytes[4096];qa_net_writer writer;qa_net_writer_init(&writer,bytes,sizeof(bytes),&error);
        CHECK(qa_q2_frame_write(&encoder,&writer,&frame,NULL,(qa_q2_entity_span){0},1));
        qa_net_reader reader;qa_net_reader_init(&reader,(qa_bytes){bytes,qa_net_writer_size(&writer)},&error);
        CHECK(qa_net_read_u8(&reader)==20);
        CHECK(qa_q2_frame_history_read(history,&decoder,&reader,(qa_q2_entity_span){0},&stored));
        CHECK(stored->valid && stored->lease && stored->entity_count==1 && stored->entities[0].number==entity.number);
        CHECK(stored->entities[0].origin[2]==3 && stored->players[0].area_bits.size==sizeof(area));
        CHECK(!memcmp(stored->players[0].area_bits.data,area,sizeof(area)));
        qa_q2_frame_history_destroy(history);
    }
}
typedef struct player_media_fixture { qa_strings *strings; size_t body_reads; } player_media_fixture;
static bool player_media_available(void *context, qa_string_id name, qa_q2_player_media_kind kind,
    bool *found, qa_error *error)
{
    (void)error;
    player_media_fixture *fixture = context;
    const char *path = qa_strings_cstr(fixture->strings, name);
    if (kind != QA_Q2_PLAYER_MEDIA_MODEL || strstr(path, "tris.md2")) ++fixture->body_reads;
    *found = !strcmp(path, "players/male/tris.md2") || !strcmp(path, "players/male/grunt.pcx") ||
        !strcmp(path, "/players/male/grunt_i.pcx") || !strcmp(path, "players/male/weapon.md2") ||
        !strcmp(path, "players/female/tris.md2") || !strcmp(path, "players/female/athena.pcx") ||
        !strcmp(path, "/players/female/athena_i.pcx") || !strcmp(path, "players/female/weapon.md2") ||
        !strcmp(path, "players/cyborg/tris.md2") || !strcmp(path, "players/cyborg/oni911.pcx") ||
        !strcmp(path, "/players/cyborg/oni911_i.pcx");
    return true;
}
static void test_q2_player_media(void)
{
    qa_error error = {0}; qa_strings *strings = NULL;
    CHECK(qa_strings_create(&strings, &error));
    qa_string_id weapon; CHECK(qa_strings_intern_cstr(strings, "weapon.md2", &weapon, &error));
    player_media_fixture fixture = {.strings = strings};
    const char *infos[] = {"Player\\female/missing", "Player\\missing/missing", "Player\\cyborg/oni911"};
    const char *skins[] = {"players/female/athena.pcx", "players/male/grunt.pcx", "players/cyborg/oni911.pcx"};
    for (size_t i = 0; i < 3; ++i) {
        qa_hud_q2_player_ids names; qa_q2_player_media media = {0};
        CHECK(qa_hud_q2_player_ids_bind(strings, infos[i], true, &names, &error));
        CHECK(qa_q2_player_media_resolve(strings, &names, &weapon, 1, true, 0, false, true,
            player_media_available, &fixture, &media, &error));
        CHECK(media.body_valid && media.weapon_valid);
        CHECK(!strcmp(qa_strings_cstr(strings, media.skin), skins[i]));
        if (i == 2) CHECK(!strcmp(qa_strings_cstr(strings, media.weapons[0]), "players/male/weapon.md2"));
        fixture.body_reads = 0; qa_string_id skin = media.skin, model = media.model;
        CHECK(qa_q2_player_media_resolve(strings, &names, &weapon, 1, true, 0, false, false,
            player_media_available, &fixture, &media, &error));
        CHECK(!fixture.body_reads && media.skin == skin && media.model == model);
    }
    qa_hud_q2_player_ids names; qa_q2_player_media media = {0};
    CHECK(qa_hud_q2_player_ids_bind(strings, infos[0], false, &names, &error));
    CHECK(qa_q2_player_media_resolve(strings, &names, &weapon, 1, false, 0, false, true,
        player_media_available, &fixture, &media, &error));
    CHECK(media.body_valid && media.weapon_valid && !strcmp(qa_strings_cstr(strings, media.skin), skins[1]));
    qa_strings_destroy(strings);
}

static void test_q2_weapon_names(void)
{
    qa_error error = {0}; qa_strings *strings = NULL;
    CHECK(qa_strings_create(&strings, &error));
    const qa_net_protocol protocols[] = {QA_NET_Q2_34, QA_NET_Q2PRIVATE_4038};
    qa_string_id *config = calloc(QA_Q2_MAX_CONFIGSTRINGS, sizeof(*config)); CHECK(config);
    for (size_t i = 0; i < 2; ++i) {
        qa_q2_codec codec; qa_q2_config_layout layout;
        CHECK(qa_q2_codec_init(&codec, (qa_net_protocol_id){.kind = protocols[i]}, &error));
        CHECK(qa_q2_config_layout_read(&codec, &layout, &error));
        memset(config, 0, QA_Q2_MAX_CONFIGSTRINGS * sizeof(*config));
        qa_hud_q2_weapons incremental = {0}, bound;
        for (uint32_t j = 1; j <= 35; ++j) {
            char name[32]; snprintf(name, sizeof(name), "#weapon%u.md2", j);
            CHECK(qa_strings_intern_cstr(strings, name, config + layout.models + j, &error));
            CHECK(qa_hud_q2_weapon_add(strings, &layout, config[layout.models + j], &incremental, &error));
        }
        CHECK(qa_hud_q2_weapons_bind(strings, &layout, config, &bound, &error));
        CHECK(bound.count == (i ? 32u : 20u) && bound.count == incremental.count);
        CHECK(!memcmp(bound.configs, incremental.configs, sizeof(bound.configs)));
        CHECK(!strcmp(qa_strings_cstr(strings, bound.names[0]), "weapon.md2"));
        if (i) {
            CHECK(qa_strings_intern_cstr(strings, "1|2|3|4|5|6|7|8", config + layout.wheel_weapons, &error));
            qa_hud_q2_weapons_warning(strings, &layout, config, layout.wheel_weapons, &bound);
            CHECK(bound.warnings[0] == 7);
            CHECK(qa_strings_intern_cstr(strings, "1|2|3|4|5|6|0", config + layout.wheel_weapons, &error));
            qa_hud_q2_weapons_warning(strings, &layout, config, layout.wheel_weapons, &bound);
            CHECK(bound.warnings[0] == 0);
            CHECK(qa_strings_intern_cstr(strings, "incomplete", config + layout.wheel_weapons, &error));
            qa_hud_q2_weapons_warning(strings, &layout, config, layout.wheel_weapons, &bound);
            CHECK(bound.warnings[0] == 0);
        }
    }
    free(config); qa_strings_destroy(strings);
}

static void test_native_import_slot_identity(void)
{
    CHECK(qa_native_q2_import_dispatch(QA_NATIVE_Q2_GAME_API3,12)==QA_NATIVE_Q2_IMPORT_trace);
    CHECK(qa_native_q2_import_dispatch(QA_NATIVE_Q2_GAME_API3,4)==QA_NATIVE_Q2_IMPORT_sound);
    CHECK(qa_native_q2_import_dispatch(QA_NATIVE_Q2_CGAME_API2023,0)==QA_NATIVE_Q2_IMPORT_Com_Print);
    CHECK(qa_native_q2_import_dispatch(QA_NATIVE_Q2_CGAME_API2023,1)==QA_NATIVE_Q2_IMPORT_get_configstring);
    CHECK(qa_native_q2_import_dispatch(QA_NATIVE_Q2_CGAME_API2023,2)==QA_NATIVE_Q2_IMPORT_Com_Error);
    CHECK(qa_native_q2_import_dispatch(QA_NATIVE_Q2_GAME_API3,UINT32_MAX)==QA_NATIVE_Q2_IMPORT_NONE);
}

typedef struct q3_command_test {
    int32_t sequence;
    size_t calls, received;
    application_q3_command_cursor *cursor;
} q3_command_test;
static uint64_t q3_test_generation(void *context) { (void)context; return 1; }
static bool q3_test_ok(void *context,qa_error *error) { (void)context;(void)error;return true; }
static bool q3_test_download_size(void *context,int32_t size,int32_t *out,qa_error *error)
{ (void)context;(void)error;*out=size;return true; }
static bool q3_test_gamestate(void *context,const qa_q3_gamestate *state,qa_error *error)
{ (void)state;return q3_test_ok(context,error); }
static bool q3_test_text(void *context,const char *text,qa_error *error)
{ (void)text;return q3_test_ok(context,error); }
static bool q3_test_snapshot(void *context,const qa_q3_snapshot *snapshot,int32_t ping,qa_error *error)
{ (void)snapshot;(void)ping;return q3_test_ok(context,error); }
static bool q3_test_download(void *context,const qa_q3_download *download,qa_error *error)
{ (void)download;return q3_test_ok(context,error); }
static bool q3_test_command(void *context,int32_t sequence,const qa_q3_tokens *tokens,qa_error *error)
{ (void)error;CHECK(tokens->count>0);q3_command_test *state=context;state->sequence=sequence;++state->calls;return true; }
static bool q3_test_command_received(void *context,const qa_q3_command *command,qa_error *error)
{
    q3_command_test *state=context;
    if(!application_q3_command_admit_received(state->cursor,command,error)) return false;
    ++state->received;return true;
}
static bool q3_test_local(void *context) { (void)context;return false; }
static void test_arena_progress_bound_store(void)
{
    qa_error error={0};
    qa_cvars *cvars=qa_cvars_create(&(qa_cvar_options){.dialect=QA_RULESET_Q3},&error);
    CHECK(cvars!=NULL);
    qa_arena_progress progress;
    CHECK(qa_arena_progress_init(&progress,cvars,(qa_arena_catalog){4,6,4,5},0,&error));
    int32_t current;
    CHECK(qa_arena_progress_current(&progress,&current,&error) && current==4);
    CHECK(qa_cvars_set(cvars,"g_spScores3","\\l4\\1\\l0\\1\\l1\\1",true,&error));
    CHECK(qa_arena_progress_current(&progress,&current,&error) && current==2);
    qa_arena_best best;
    CHECK(qa_arena_progress_best(&progress,0,&best,&error) && best.rank==1 && best.skill==3);
    CHECK(qa_cvars_set(cvars,"name","player",true,&error));
    CHECK(qa_arena_progress_current(&progress,&current,&error) && current==2);
    CHECK(qa_cvars_set(cvars,"g_spScores5","\\l4\\1\\l0\\1\\l1\\1\\l2\\1\\l3\\1",true,&error));
    CHECK(qa_arena_progress_current(&progress,&current,&error) && current==5);
    CHECK(qa_arena_progress_reset(&progress,&error));
    CHECK(qa_arena_progress_current(&progress,&current,&error) && current==4);
    CHECK(qa_arena_progress_unlock_levels(&progress,&error));
    CHECK(qa_arena_progress_current(&progress,&current,&error) && current==5);
    bool unlocked=false;
    CHECK(qa_arena_progress_movie(&progress,1,&unlocked,&error) && unlocked);
    CHECK(qa_arena_progress_unlock_medals(&progress,&error));
    int32_t awards;
    CHECK(qa_arena_progress_award(&progress,3,&awards,&error) && awards==100);
    qa_arena_progress rebound;
    qa_arena_progress_bind(&rebound,cvars,progress.catalog);
    CHECK(qa_arena_progress_current(&rebound,&current,&error) && current==5);
    qa_cvars_destroy(cvars);
}

static void test_q3_reliable_event_references(void)
{
    qa_error error={0};
    qa_event_ring *ring=qa_event_ring_create(8192,4096,4,&error);
    CHECK(ring!=NULL);
    qa_event_transaction transaction;
    CHECK(qa_event_ring_begin(ring,&transaction));
    char *text=qa_event_ring_alloc(&transaction,2501,1,&error);
    CHECK(text!=NULL);
    for(size_t i=0;i<2500;++i) text[i]=(char)('a'+i%26);
    text[2500]=0;
    uint64_t id=qa_event_ring_commit(&transaction,text);
    CHECK(id!=0);
    qa_event_lease *lease=qa_event_ring_retain(ring,id);
    CHECK(lease!=NULL);
    qa_q3_reliable histories[2]={0};
    for(size_t reader=0;reader<2;++reader)
        for(uint32_t fragment=0;fragment<3;++fragment)
            CHECK(qa_q3_reliable_admit(histories+reader,QA_Q3_SERVER,lease,text,42,fragment,&error));
    qa_event_ring_retire(ring,id+1);
    CHECK(qa_event_ring_at(ring,id)==NULL);
    qa_event_lease_release(lease);
    qa_event_ring_destroy(&ring);
    bool clamped=false;
    CHECK(qa_q3_reliable_ack(histories,QA_Q3_SERVER,3,&clamped,&error));
    CHECK(!clamped && histories[0].event_cursor==id+1);
    qa_q3_reliable_clear(histories);
    qa_q3_big_configstring assembled = {0};
    for(int32_t sequence=1;sequence<=3;++sequence) {
        qa_q3_tokens tokens;
        CHECK(qa_q3_tokenize(qa_q3_reliable_lookup(histories+1,sequence),&tokens,&error));
        CHECK(tokens.count==3);
        CHECK(!strcmp(qa_q3_token(&tokens,0),sequence==1 ? "bcs0" : sequence==2 ? "bcs1" : "bcs2"));
        CHECK(!strcmp(qa_q3_token(&tokens,1),"42"));
        size_t offset=(size_t)(sequence-1)*999,count=2500-offset;
        if(count>999) count=999;
        CHECK(strlen(qa_q3_token(&tokens,2))==count);
        CHECK(!memcmp(qa_q3_token(&tokens,2),text+offset,count));
        CHECK(histories[1].slots[sequence].text==text && !histories[1].storage);
        bool complete;
        CHECK(qa_q3_big_configstring_feed(&assembled,
            &tokens, &complete, &error));
        CHECK(complete == (sequence == 3));
        if (complete) {
            CHECK(qa_q3_tokenize(assembled.text, &tokens, &error));
            CHECK(tokens.count == 3 && !strcmp(qa_q3_token(&tokens, 0), "cs"));
            CHECK(!strcmp(qa_q3_token(&tokens, 1), "42"));
            CHECK(!strcmp(qa_q3_token(&tokens, 2), text));
        }

    }
    qa_q3_tokens fragment;
    bool complete;
    qa_q3_big_configstring_clear(&assembled);
    CHECK(qa_q3_tokenize("bcs1 42 tail", &fragment, &error));
    CHECK(!qa_q3_big_configstring_feed(&assembled,
        &fragment, &complete, &error));
    assembled.length = sizeof(assembled.text) - 2;
    CHECK(qa_q3_tokenize("bcs2 42 tail", &fragment, &error));
    CHECK(!qa_q3_big_configstring_feed(&assembled, &fragment, &complete, &error));
    qa_q3_big_configstring_clear(&assembled);
    qa_q3_gamestate applied = {.string_bytes = 1};
    qa_q3_server_command_result dispatch;
    CHECK(qa_q3_server_command_apply(NULL, &applied, &assembled, 1,
        "cs 1 \"\\sv_serverid\\2147483648\"", &fragment, &dispatch, &error));
    CHECK(dispatch.kind == QA_Q3_SERVER_SYSTEM_INFO && dispatch.config_index == 1);
    int32_t server_id = 0;
    qa_q3_usercmd commands[2] = {{.serverTime = 71}, {.serverTime = 72}};
    CHECK(qa_q3_client_command_state_update(dispatch.kind, dispatch.effect_text,
        &server_id, commands, 2, &error));
    CHECK(server_id == INT32_MAX && commands[1].serverTime == 72);
    CHECK(qa_q3_server_command_apply(NULL, &applied, &assembled, 2,
        "cs 1 \"\\sv_serverid\\2147483648\"", &fragment, &dispatch, &error));
    CHECK(dispatch.kind == QA_Q3_SERVER_CONFIGSTRING);
    CHECK(qa_q3_server_command_apply(NULL, &applied, &assembled, 3,
        "map_restart", &fragment, &dispatch, &error));
    CHECK(dispatch.kind == QA_Q3_SERVER_MAP_RESTART);
    CHECK(qa_q3_client_command_state_update(dispatch.kind, dispatch.effect_text,
        &server_id, commands, 2, &error));
    CHECK(commands[0].serverTime == 0 && commands[1].serverTime == 0);
    int32_t consumed = 1;
    CHECK(qa_q3_server_command_ack(NULL, &applied, 3, &consumed, &error));
    CHECK(consumed == 3 && applied.command_sequence == 3);
    CHECK(qa_q3_parse_integer(" -999999999999") == INT32_MIN);
    CHECK(qa_q3_parse_integer("+42suffix") == 42);
    qa_q3_reliable full={0};
    bool drop=false;
    lease=histories[1].slots[1].lease;
    for(size_t i=0;i<QA_Q3_RELIABLE;++i) {
        CHECK(qa_q3_reliable_server_admit(&full,lease,text,42,0,&drop,&error));
        CHECK(!drop);
    }
    CHECK(qa_q3_reliable_server_admit(&full,lease,text,42,0,&drop,&error));
    CHECK(drop && full.sequence==QA_Q3_RELIABLE);
    CHECK(qa_q3_reliable_ack(&full,QA_Q3_SERVER,full.sequence,&clamped,&error));
    CHECK(qa_q3_reliable_server_admit(&full,lease,text,42,0,&drop,&error));
    CHECK(!drop && full.sequence==QA_Q3_RELIABLE+1);
    full.sequence=INT32_MAX;
    CHECK(qa_q3_reliable_server_admit(&full,lease,text,42,0,&drop,&error));
    CHECK(drop);
    qa_q3_reliable_clear(&full);
    qa_q3_reliable_clear(histories+1);
}

static void test_q3_received_commands(void)
{
    qa_error error={0};qa_application app={0},restored_app={0};
    CHECK(application_event_stream_create(&app,64,&error));
    CHECK(application_event_stream_create(&restored_app,64,&error));
    application_q3_command_cursor cursor={0},restored_cursor={0};
    application_q3_command_init(&cursor,&app);
    application_q3_command_init(&restored_cursor,&restored_app);
    q3_command_test state={.cursor=&cursor},restored_state={.cursor=&restored_cursor};
    qa_q3_client_hooks hooks={.context=&state,.generation=q3_test_generation,.clear_active=q3_test_ok,
        .download_size=q3_test_download_size,.gamestate=q3_test_gamestate,.system_info=q3_test_text,
        .snapshot=q3_test_snapshot,.download=q3_test_download,.command=q3_test_command,
        .command_received=q3_test_command_received,
        .map_restart=q3_test_ok,.disconnect=q3_test_text,.level_shot=q3_test_ok,.local_server_running=q3_test_local};
    qa_q3_client_peer *peer=NULL,*restored=NULL;
    CHECK(qa_q3_client_peer_create_demo((qa_q3_identity){0},QA_Q3_ARENA,&hooks,&peer,&error));
    const char *chunks[]={"bcs0 42 \"alpha \"","bcs1 42 \"beta \"","bcs2 42 \"gamma\""};
    for(int32_t sequence=1;sequence<=131;++sequence) {
        char command[64];CHECK(snprintf(command,sizeof(command),"print sample-%d",sequence)>0);
        const char *expected=sequence<=3?chunks[sequence-1]:command;
        uint8_t bytes[4096];qa_q3_writer writer;qa_q3_writer_init(&writer,bytes,sizeof(bytes),false,&error);
        CHECK(qa_q3_server_begin(&writer,0));
        CHECK(qa_q3_server_command(&writer,sequence,expected));
        CHECK(qa_q3_server_end(&writer));
        CHECK(qa_q3_client_peer_message(peer,sequence,(qa_bytes){bytes,qa_q3_writer_size(&writer)},sequence,&error));
        CHECK(state.received==(size_t)sequence && cursor.sequence==sequence);
        const char *text=NULL;
        CHECK(application_q3_command_lookup(&cursor,sequence,&text,&error) && !strcmp(text,expected));
        qa_q3_command received={.sequence=sequence};
        CHECK(snprintf(received.text,sizeof(received.text),"%s",text)>0);
        CHECK(qa_q3_client_peer_command_apply(peer,&received,&error));
        app.event_local_cursor=app.event_source_cursor=qa_application_events_next(&app);
        bool clamped=false;
        CHECK(application_q3_command_ack(&cursor,sequence,&clamped,&error) && !clamped);
        application_events_retire(&app);
        CHECK(qa_application_events_first(&app)==qa_application_events_next(&app));
        if(sequence==3) CHECK(!strcmp(qa_q3_configstring(qa_q3_client_peer_gamestate(peer),42),"alpha beta gamma"));
    }
    CHECK(state.sequence==131 && state.calls==129);
    for(int32_t sequence=68;sequence<=131;++sequence) {
        const char *text=NULL;char expected[64];
        CHECK(snprintf(expected,sizeof(expected),"print sample-%d",sequence)>0);
        CHECK(application_q3_command_lookup(&cursor,sequence,&text,&error) && !strcmp(text,expected));
    }
    const char *text=NULL;qa_error expired={0};
    CHECK(!application_q3_command_lookup(&cursor,67,&text,&expired) && expired.code==QA_ERROR_FORMAT);
    qa_buffer saved={0};CHECK(qa_q3_client_peer_checkpoint(peer,&saved,&error));
    hooks.context=&restored_state;
    CHECK(qa_q3_client_peer_restore((qa_bytes){saved.data,saved.size},(qa_q3_identity){0},&hooks,&restored,&error));
    CHECK(restored_state.received==QA_Q3_RELIABLE && restored_cursor.sequence==131);
    for(int32_t sequence=68;sequence<=131;++sequence) {
        char expected[64];CHECK(snprintf(expected,sizeof(expected),"print sample-%d",sequence)>0);
        CHECK(application_q3_command_lookup(&restored_cursor,sequence,&text,&error) && !strcmp(text,expected));
    }
    restored_app.event_local_cursor=restored_app.event_source_cursor=qa_application_events_next(&restored_app);
    bool clamped=false;
    CHECK(application_q3_command_ack(&restored_cursor,131,&clamped,&error) && !clamped);
    application_events_retire(&restored_app);
    CHECK(qa_application_events_first(&restored_app)==qa_application_events_next(&restored_app));
    CHECK(application_q3_command_lookup(&restored_cursor,131,&text,&error));
    qa_q3_command received={.sequence=131};
    CHECK(snprintf(received.text,sizeof(received.text),"%s",text)>0);
    CHECK(qa_q3_client_peer_command_apply(restored,&received,&error));
    CHECK(restored_state.sequence==131 && restored_state.calls==1);
    CHECK(!strcmp(qa_q3_configstring(qa_q3_client_peer_gamestate(restored),42),"alpha beta gamma"));
    qa_buffer_free(&saved);qa_q3_client_peer_destroy(peer);qa_q3_client_peer_destroy(restored);
    application_q3_command_clear(&cursor);application_q3_command_clear(&restored_cursor);
    application_event_stream_dispose(&app);application_event_stream_dispose(&restored_app);
    free(app.unified_persistent);free(restored_app.unified_persistent);
}

static void test_q3_command_cursors(void)
{
    qa_application app={0};qa_error error={0};
    CHECK(qa_session_create(&(qa_session_options){.actor_capacity=128,.component_capacity=1},&app.session,&error));
    CHECK(application_event_stream_create(&app,128,&error));
    application_q3_command_cursor first={0},second={0},moved={0},restored={0};
    application_q3_command_init(&first,&app);application_q3_command_init(&second,&app);
    application_q3_command_init(&moved,&app);application_q3_command_init(&restored,&app);
    char command[]="print first";bool overflow=false,clamped=false;
    CHECK(application_q3_command_admit(&first,command,UINT32_MAX,0,&overflow,&error) && !overflow);
    command[6]='x';
    CHECK(application_q3_command_admit(&second,"print second",UINT32_MAX,0,&overflow,&error) && !overflow);
    const char *text=NULL;
    CHECK(application_q3_command_lookup(&first,1,&text,&error) && !strcmp(text,"print first"));
    CHECK(application_q3_command_lookup(&second,1,&text,&error) && !strcmp(text,"print second"));
    app.event_local_cursor=app.event_source_cursor=qa_application_events_next(&app);
    application_events_retire(&app);CHECK(qa_application_events_first(&app)==1);
    application_q3_command_move(&moved,&first);
    CHECK(first.sequence==0 && !first.delivery.application && moved.sequence==1);
    CHECK(application_q3_command_ack(&moved,1,&clamped,&error) && !clamped);
    application_events_retire(&app);CHECK(qa_application_events_first(&app)==2);
    CHECK(application_q3_command_lookup(&second,1,&text,&error) && !strcmp(text,"print second"));
    CHECK(application_q3_command_ack(&second,1,&clamped,&error) && !clamped);
    application_events_retire(&app);CHECK(qa_application_events_first(&app)==3);
    CHECK(application_q3_command_lookup(&moved,1,&text,&error) && !strcmp(text,"print first"));

    for(int32_t sequence=2;sequence<=65;++sequence) {
        char value[64];CHECK(snprintf(value,sizeof(value),"print local-%d",sequence)>0);
        CHECK(application_q3_command_admit(&moved,value,UINT32_MAX,0,&overflow,&error) && !overflow);
    }
    uint64_t next=qa_application_events_next(&app);
    CHECK(application_q3_command_admit(&moved,"print blocked",UINT32_MAX,0,&overflow,&error) && overflow);
    CHECK(moved.sequence==65 && qa_application_events_next(&app)==next);
    CHECK(application_q3_command_lookup(&moved,2,&text,&error) && !strcmp(text,"print local-2"));
    app.event_local_cursor=app.event_source_cursor=next;
    application_events_retire(&app);
    uint64_t pending=qa_application_events_first(&app);
    CHECK(application_q3_command_ack(&moved,33,&clamped,&error) && !clamped);
    application_events_retire(&app);
    CHECK(qa_application_events_first(&app)>pending && qa_application_events_first(&app)<next);
    CHECK(application_q3_command_admit(&moved,"print local-66",UINT32_MAX,0,&overflow,&error) && !overflow);
    qa_error expired={0};
    CHECK(!application_q3_command_lookup(&moved,2,&text,&expired) && expired.code==QA_ERROR_FORMAT);
    app.event_local_cursor=app.event_source_cursor=qa_application_events_next(&app);
    CHECK(application_q3_command_ack(&moved,66,&clamped,&error) && !clamped);
    application_events_retire(&app);
    CHECK(qa_application_events_first(&app)==qa_application_events_next(&app));
    CHECK(application_q3_command_lookup(&moved,66,&text,&error) && !strcmp(text,"print local-66"));
    CHECK(application_q3_command_lookup(&second,1,&text,&error) && !strcmp(text,"print second"));

    restored.sequence=restored.acknowledged=66;
    for(size_t slot=0;slot<QA_Q3_RELIABLE;++slot) {
        CHECK(application_q3_command_lookup_slot(&moved,slot,&text,&error));
        CHECK(application_q3_command_restore_slot(&restored,slot,text,&error));
    }
    app.event_local_cursor=app.event_source_cursor=qa_application_events_next(&app);
    application_events_retire(&app);
    CHECK(qa_application_events_first(&app)==qa_application_events_next(&app));
    for(int32_t sequence=3;sequence<=66;++sequence) {
        char expected[64];CHECK(snprintf(expected,sizeof(expected),"print local-%d",sequence)>0);
        CHECK(application_q3_command_lookup(&restored,sequence,&text,&error) && !strcmp(text,expected));
    }
    app.operation=APPLICATION_PERSISTING;
    qa_buffer saved={0};CHECK(application_events_save_capture(&app,&saved,&error));
    CHECK(application_events_save_restore(&app,(qa_bytes){saved.data,saved.size},&error));
    qa_buffer_free(&saved);app.operation=APPLICATION_IDLE;
    for(int32_t sequence=3;sequence<=66;++sequence) {
        char expected[64];CHECK(snprintf(expected,sizeof(expected),"print local-%d",sequence)>0);
        CHECK(application_q3_command_lookup(&restored,sequence,&text,&error) && !strcmp(text,expected));
        CHECK(application_q3_command_lookup(&moved,sequence,&text,&error) && !strcmp(text,expected));
    }
    CHECK(application_q3_command_lookup(&second,1,&text,&error) && !strcmp(text,"print second"));
    CHECK(application_q3_command_admit(&restored,"print local-67",UINT32_MAX,0,&overflow,&error) && !overflow);
    CHECK(application_q3_command_lookup(&restored,67,&text,&error) && !strcmp(text,"print local-67"));
    app.event_local_cursor=app.event_source_cursor=qa_application_events_next(&app);
    CHECK(application_q3_command_ack(&restored,67,&clamped,&error) && !clamped);
    application_events_retire(&app);
    CHECK(qa_application_events_first(&app)==qa_application_events_next(&app));
    application_q3_command_clear(&first);application_q3_command_clear(&second);
    application_q3_command_clear(&moved);application_q3_command_clear(&restored);
    CHECK(!app.event_subscribers && app.unified_persistent_count==0);
    application_event_stream_dispose(&app);free(app.unified_persistent);
    CHECK(qa_session_destroy(app.session,&error));
}

static bool player_feedback_test(void)
{
    qa_player_feedback first={.damage=true,.armor=1.25f,.blood=4.5f,.origin={1,2,3}},
        second={.damage=true,.armor=2.5f,.blood=5.25f,.origin={4,5,6},
            .set_angle=true,.angles={15,90,0}}, view={0},wire={0};
    qa_player_feedback_merge(&view,&first); qa_player_feedback_merge(&view,&second);
    qa_player_feedback_merge(&wire,&first); qa_player_feedback_merge(&wire,&second);
    CHECK(view.armor==3.75f && view.blood==9.75f && view.damage && view.set_angle);
    CHECK(view.origin[0]==4 && view.angles[1]==90);
    CHECK(wire.armor==view.armor && wire.blood==view.blood && wire.origin[2]==view.origin[2]);
    qa_unified_presentation_payload source={.kind=QA_UNIFIED_PRESENTATION_PLAYER_FEEDBACK,.value.feedback=view},copy={0};
    qa_error error={0};
    CHECK(qa_unified_presentation_payload_clone(&source,&copy,&error));
    CHECK(copy.value.feedback.armor==3.75f && copy.value.feedback.origin[1]==5 && copy.value.feedback.angles[0]==15);
    qa_unified_presentation_payload_dispose(&copy);
    return true;
}

int main(int argc, char **argv)
{
    if(!player_feedback_test()) return 1;
    int recovery_status;
    if (test_recovery_child(argc, argv, &recovery_status)) return recovery_status;
    test_errors_and_buffers();
    test_source_text();
    test_q2_hud_configuration();
    test_compiled_sound_names();
    test_shared_angle_basis();
    test_shared_bounds_and_transform();
    test_shared_vector_geometry();
    test_shared_angle_wrapping();
    test_shared_direction_angles();
    test_native_import_slot_identity();
    test_q2_weapon_names();
    test_q2_player_media();
    test_literal_tokens();
    test_platform_event_retirement();
    test_q2_command_angles();
    test_loopback_admission();
    test_dosbox_loopback();
    test_loopback_nq_signon();
    test_kex_send_admission();
    test_kex_direct_delivery();
    test_nq_send_admission();
    test_q3_send_admission();
    test_arena_progress_bound_store();
    test_q3_reliable_event_references();
    test_q3_received_commands();
    test_q3_command_cursors();
    test_shared_cvar_archive();
    test_cvar_info_storage();
    test_shared_input_menu_defaults();
    test_profiler_mode_changes();
    test_source_nonmipped_transparency();
    test_cached_world_surface();
    test_retained_patch_levels();
    test_json_caller_storage();
    test_binary();
    test_spans();
    test_arena();
    test_reserved_scene_frame();
    test_metadata_retention();
    test_files();
    test_localization_lookup_generation();
    test_ring_console_capture();
    test_shared_effect_cursor();
    test_shared_audio_preparation();
    test_retained_movement_result();
    test_campaign_unit();
    test_recovery_checkpoints();
    test_q1_original_codec();
    test_q1_effect_state();
    test_q1_lightstyle_event();
    test_q2_owned_frames();
    test_q2_retained_records();
    test_q1_gameplay();
    test_guest();
    test_recovery(argv[0]);
    puts("core tests passed");
    return EXIT_SUCCESS;
}

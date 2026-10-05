#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "qa/arena.h"
#include "qa/binary.h"
#include "qa/campaign.h"
#include "qa/recovery.h"
#include "qa/q1_save.h"

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

static void test_recovery_rotation(void)
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
    CHECK(!qa_recovery_checkpoint_due(recovery));
    CHECK(qa_save_image_destroy_checked(&image, &error));
    uint8_t payload[16384] = {73};
    qa_net_protocol_id protocol = {QA_NET_UNIFIED_1, 0, 0};
    for (unsigned i = 0; i < 256; ++i)
        CHECK(qa_recovery_append(recovery, QA_DEMO_JOURNAL, 0, protocol,
            (qa_bytes){payload, sizeof(payload)}, &error));
    CHECK(qa_recovery_checkpoint_due(recovery));
    CHECK(qa_recovery_append(recovery, QA_DEMO_ADVANCE, 1, protocol, (qa_bytes){0}, &error));
    image = recovery_image(1, 47);
    CHECK(qa_save_image_encode(image, &first, &error));
    CHECK(qa_recovery_checkpoint(recovery, image, &error));
    CHECK(!qa_recovery_checkpoint_due(recovery));
    qa_demo *demo = NULL;
    CHECK(qa_demo_read(root, "recovery.qdemo", true, &demo, &error));
    CHECK(qa_demo_record_count(demo) == 1 && qa_demo_start_time(demo) == 1);
    const qa_demo_record *record = qa_demo_record_at(demo, 0);
    CHECK(record->payload.size == first.size && !memcmp(record->payload.data, first.data, first.size));
    qa_demo_destroy(demo);
    qa_fs_entry_kind kind;
    qa_fs_identity identity;
    CHECK(qa_fs_root_status(root, "recovery.qdemo", &kind, &identity, &error));
    CHECK(qa_fs_identity_size(&identity) == first.size + 84);
    CHECK(qa_save_image_destroy_checked(&image, &error));
    uint64_t elapsed = 1;
    for (unsigned i = 0; i < 16; ++i) {
        CHECK(qa_recovery_append(recovery, QA_DEMO_ADVANCE, UINT64_C(300000000000),
            protocol, (qa_bytes){0}, &error));
        CHECK(qa_recovery_checkpoint_due(recovery));
        elapsed += UINT64_C(300000000000);
        image = recovery_image(elapsed, (uint8_t)i);
        CHECK(qa_recovery_checkpoint(recovery, image, &error));
        CHECK(qa_save_image_destroy_checked(&image, &error));
        CHECK(qa_demo_read(root, "recovery.qdemo", true, &demo, &error));
        CHECK(qa_demo_record_count(demo) == 1 && qa_demo_start_time(demo) == elapsed);
        qa_demo_destroy(demo);
    }
    bool available = false;
    CHECK(qa_recovery_available(root, "recovery.qdemo", &available, &error) && available);
    CHECK(!qa_recovery_append(recovery, QA_DEMO_JOURNAL, 0, protocol,
        (qa_bytes){payload, 8u * 1024u * 1024u}, &error));
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

int main(int argc, char **argv)
{
    int recovery_status;
    if (test_recovery_child(argc, argv, &recovery_status)) return recovery_status;
    test_errors_and_buffers();
    test_binary();
    test_spans();
    test_arena();
    test_files();
    test_campaign_unit();
    test_recovery_rotation();
    test_q1_original_codec();
    test_q1_gameplay();
    test_guest();
    test_recovery(argv[0]);
    puts("core tests passed");
    return EXIT_SUCCESS;
}

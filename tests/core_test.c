#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "qa/arena.h"
#include "qa/native.h"
#include "qa/native_guest.h"
#include "../src/compat/native/guest/abi.h"
#include "../src/compat/native/guest/memory_string.h"
#include "qa/pool.h"
#include "qa/operation.h"
#include "qa/random.h"
#include "qa/network_unified_frame_pool.h"
#include "qa/network_unified_session.h"
#include "qa/network_unified_control.h"
#include "qa/unified_frame_metadata.h"
#include "qa/unified_frame_events.h"
#include "qa/unified_frame_prediction.h"
#include "qa/console_cvars_prepare.h"
#include "qa/console_buffer.h"
#include "qa/console_release.h"
#include "qa/console_dedicated_save.h"
#include "qa/console_io.h"
#include "qa/field.h"
#include "qa/settings.h"
#include "qa/binary.h"
#include "qa/json.h"
#include "qa/json_writer.h"
#include "qa/campaign.h"
#include "qa/arena_progress.h"
#include "qa/recovery.h"
#include "qa/persistence_fields.h"
#include "qa/localization.h"
#include "qa/lightstyles.h"
#include "qa/text.h"
#include "qa/font.h"
#include "qa/image.h"
#include "qa/common_parse.h"
#include "qa/q2_sound.h"
#include "qa/hud_q2.h"
#include "qa/game_q2_original_save.h"
#include "qa/hud.h"
#include "../src/ui/internal.h"
#include "qa/vfs.h"
#include "qa/audio.h"
#include "qa/audio_save.h"
#include "qa/cinematic.h"
#include "qa/q3_cinematic_handles.h"
#include "../src/audio/mixer_internal.h"
#include "qa/movement.h"
#include "qa/navigation_asset.h"
#include "qa/q1_save.h"
#include "qa/q1_text.h"
#include "qa/campaign_q1.h"
#include "qa/scene.h"
#include "qa/render_cpu.h"
#include "../src/render/resource_index_private.h"
#include "../src/render/scene/world/internal.h"
#include "../src/render/scene/world/legacy/internal.h"
#include "../src/render/scene/world/q3/patch.h"
#include "qa/tools.h"
#include "qa/input.h"
#include "qa/platform_events.h"
#include "qa/network_q3.h"
#include "qa/network_q3_save.h"
#include "qa/network_q1_channel.h"
#include "qa/network_q1_nq.h"
#include "qa/network_qw_source.h"
#include "../src/world/collision/internal.h"
#include "qa/network_q2_kex.h"
#include "qa/network_q2_messages.h"
#include "qa/network_q2_mvd.h"
#include "qa/network_q2_unicast.h"
#include "../src/app/frontend/selected_effects_q1_temporary.h"
#include "../src/app/frontend/shared_video.h"
#include "../src/network/q2/session_internal.h"
#include "../src/app/application/internal.h"
#include "../src/app/application/unified_q3_events.h"
#include "../src/app/application/events_save.h"
#include "../src/network/unified/frame_internal.h"
#include "../src/bots/runtime/internal.h"
#include "qa/bot_observations_save.h"
#include "qa/console_save.h"
#include "../src/console/commands_private.h"
#include "../src/bots/chat/internal.h"

#include <fcntl.h>
#include <limits.h>
#include <math.h>
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

static void test_aas_geometry_compaction(void)
{
    qa_error error={0};
    const qa_vec3 vertices[]={{0,0,0},{1,0,0},{2,0,0},{2,1,0},{2,0,1},{99,99,99}};
    const qa_aas_plane planes[]={{{1,0,0},5,0}};
    const qa_aas_edge edges[]={{{0,0}},{{0,1}},{{1,2}},{{2,0}},{{2,3}},{{3,4}},{{4,2}}};
    const int32_t edge_index[]={1,2,3,-4,5,6},face_index[]={1,-2,2};
    const qa_aas_face faces[]={
        {0},
        {.plane=0,.flags=0,.edge_count=3,.first_edge=0,.front_area=1,.back_area=2},
        {.plane=0,.flags=2,.edge_count=3,.first_edge=3,.front_area=1,.back_area=2}
    };
    const qa_aas_area areas[]={
        {0},
        {.number=1,.face_count=2,.first_face=0,.bounds={{0,0,0},{10,10,10}},.center={7,5,5}},
        {.number=2,.face_count=1,.first_face=2,.bounds={{0,0,0},{10,10,10}},.center={3,5,5}}
    };
    const qa_aas_setting settings[]={{0},{.contents=8,.reach_count=5},{.contents=16}};
    const qa_aas_node nodes[]={{0},{.plane=0,.children={-1,-2}}};
    const qa_aas_reach reaches[]={
        {.area=2,.face=-2,.edge=-4,.start={2,0,0},.end={3,0,0},.travel_type=1,.travel_time=10},
        {.area=2,.face=1,.edge=1,.travel_type=1,.travel_time=20},
        {.area=2,.face=0x12345678,.edge=-1234,.travel_type=0x1000000|11,.travel_time=30},
        {.area=2,.face=123456,.edge=-654321,.travel_type=18,.travel_time=40},
        {.area=2,.face=INT32_MIN,.edge=INT32_MAX,.travel_type=19,.travel_time=50}
    };
    qa_aas_view source={.version=5,.bsp_checksum=123,
        .count={[QA_AAS_VERTICES]=6,[QA_AAS_PLANES]=1,[QA_AAS_EDGES]=7,
            [QA_AAS_EDGE_INDEX]=6,[QA_AAS_FACES]=3,[QA_AAS_FACE_INDEX]=3,
            [QA_AAS_AREAS]=3,[QA_AAS_SETTINGS]=3,[QA_AAS_REACHABILITY]=5,[QA_AAS_NODES]=2},
        .vertices=vertices,.planes=planes,.edges=edges,.edge_index=edge_index,.faces=faces,
        .face_index=face_index,.areas=areas,.settings=settings,.reachability=reaches,.nodes=nodes};
    qa_nav_asset *optimized=NULL,*decoded=NULL;
    qa_buffer original={0},unchanged={0},packed={0},roundtrip={0};
    CHECK(qa_aas_write(&source,&original,&error));
    CHECK(qa_aas_optimize(&source,&optimized,&error));
    const qa_aas_view *view=qa_nav_asset_aas(optimized);
    CHECK(view && view->count[QA_AAS_VERTICES]==4 && view->count[QA_AAS_EDGES]==4 &&
        view->count[QA_AAS_FACES]==2 && view->count[QA_AAS_EDGE_INDEX]==3 &&
        view->count[QA_AAS_FACE_INDEX]==2);
    CHECK(view->face_index[0]==-1 && view->face_index[1]==1 &&
        view->edge_index[0]==-1 && view->edge_index[1]==2 && view->edge_index[2]==3);
    CHECK(view->faces[1].flags==2 && view->faces[1].first_edge==0 && view->faces[1].edge_count==3);
    CHECK(view->areas[1].number==1 && view->areas[1].first_face==0 && view->areas[1].face_count==1 &&
        view->areas[2].number==2 && view->areas[2].first_face==1 && view->areas[2].face_count==1);
    CHECK(view->reachability[0].face==-1 && view->reachability[0].edge==-1 &&
        view->reachability[1].face==0 && view->reachability[1].edge==0);
    CHECK(!memcmp(view->reachability+2,reaches+2,3*sizeof(*reaches)));
    CHECK(view->reachability[0].area==reaches[0].area &&
        qa_vec_equal(view->reachability[0].start,reaches[0].start) &&
        qa_vec_equal(view->reachability[0].end,reaches[0].end) &&
        view->reachability[0].travel_time==reaches[0].travel_time);
    CHECK(!memcmp(view->settings,settings,sizeof(settings)) && !memcmp(view->nodes,nodes,sizeof(nodes)) &&
        !memcmp(view->planes,planes,sizeof(planes)));
    for(unsigned i=0;i<2;++i) {
        qa_vec3 point={i?7.0f:3.0f,5,5};uint32_t before,after;
        CHECK(qa_aas_point_area(&source,point,&before,&error));
        CHECK(qa_aas_point_area(view,point,&after,&error) && before==after && before==2-i);
    }
    CHECK(qa_aas_write(&source,&unchanged,&error));
    CHECK(original.size==unchanged.size && !memcmp(original.data,unchanged.data,original.size));
    CHECK(qa_aas_write(view,&packed,&error));
    CHECK(qa_nav_asset_read((qa_bytes){packed.data,packed.size},&source.bsp_checksum,&decoded,&error));
    CHECK(qa_aas_write(qa_nav_asset_aas(decoded),&roundtrip,&error));
    CHECK(packed.size==roundtrip.size && !memcmp(packed.data,roundtrip.data,packed.size));
    qa_nav_asset_release(decoded);qa_nav_asset_release(optimized);
    qa_buffer_free(&original);qa_buffer_free(&unchanged);qa_buffer_free(&packed);qa_buffer_free(&roundtrip);
    qa_aas_reach invalid_reaches[5];memcpy(invalid_reaches,reaches,sizeof(reaches));
    invalid_reaches[0].face=99;qa_aas_view invalid=source;invalid.reachability=invalid_reaches;
    optimized=NULL;
    CHECK(!qa_aas_optimize(&invalid,&optimized,&error) && !optimized && error.code==QA_ERROR_FORMAT);
    const qa_aas_view empty={.version=5,.bsp_checksum=99};
    CHECK(qa_aas_optimize(&empty,&optimized,&error));view=qa_nav_asset_aas(optimized);
    CHECK(view && view->version==empty.version && view->bsp_checksum==empty.bsp_checksum);
    for(unsigned i=0;i<QA_AAS_LUMP_COUNT;++i) CHECK(!view->count[i]);
    CHECK(qa_aas_write(view,&packed,&error));decoded=NULL;
    CHECK(qa_nav_asset_read((qa_bytes){packed.data,packed.size},&empty.bsp_checksum,&decoded,&error));
    CHECK(qa_nav_asset_aas(decoded)->count[QA_AAS_FACES]==0);
    qa_nav_asset_release(decoded);qa_nav_asset_release(optimized);qa_buffer_free(&packed);
}

static void test_typewriter_centers(void)
{
    qa_error error = {0};
    qa_application application = {0};
    qa_ui ui = {0};
    qa_hud *hud = NULL;
    CHECK(qa_hud_create(&(qa_hud_options){.ui = &ui, .application = &application}, &hud, &error));
    qa_hud_center_policy typewriter = {.character_ns = 10};
    CHECK(qa_hud_center_print(hud, "A", 100, 20, typewriter, &error));
    CHECK(qa_hud_center_print(hud, "\xc3\xa9\xe7\x8c\xab", 101, 20, typewriter, &error));
    CHECK(qa_hud_center_print(hud, "third", 102, 20, typewriter, &error));
    const qa_hud_center_state *center = qa_hud_center_read(hud, 129);
    CHECK(center && !strcmp(center->text, "A") && center->starts_ns == 100 && center->duration_ns == 30);
    center = qa_hud_center_read(hud, 130);
    CHECK(center && center->starts_ns == 130 && center->duration_ns == 40);
    CHECK(qa_hud_center_length(center, 139) == 0);
    CHECK(qa_hud_center_length(center, 140) == 2);
    CHECK(qa_hud_center_length(center, 150) == 5);
    center = qa_hud_center_read(hud, 170);
    CHECK(center && !strcmp(center->text, "third") && center->starts_ns == 170);
    CHECK(qa_hud_center_print(hud, "later", 1000, 20, typewriter, &error));
    center = qa_hud_center_read(hud, 999);
    CHECK(center && !strcmp(center->text, "later") && center->starts_ns == 1000);
    CHECK(qa_hud_center_alpha(center, 999, center->duration_ns) == 0);
    CHECK(qa_hud_center_print(hud, "queued", 1001, 20, typewriter, &error));
    CHECK(qa_hud_center_print(hud, "replacement", 1002, 20,
        (qa_hud_center_policy){.instant = true}, &error));
    center = qa_hud_center_read(hud, 2000);
    CHECK(center && !strcmp(center->text, "replacement"));
    CHECK(qa_hud_center_print(hud, "discarded", 2001, 20, typewriter, &error));
    CHECK(qa_hud_clear_center(hud, &error));
    CHECK(!qa_hud_center_read(hud, 3000));
    CHECK(qa_hud_center_print(hud, "new map", 1, 20, typewriter, &error));
    center = qa_hud_center_read(hud, 1);
    CHECK(center && !strcmp(center->text, "new map") && center->starts_ns == 1);
    CHECK(qa_hud_clear_center(hud, &error));
    CHECK(qa_hud_center_print(hud, "", 100, 20, typewriter, &error));
    CHECK(qa_hud_center_print(hud, "after blank", 101, 20, typewriter, &error));
    CHECK(!qa_hud_center_read(hud, 119));
    center = qa_hud_center_read(hud, 120);
    CHECK(center && center->starts_ns == 120 && !strcmp(center->text, "after blank"));
    CHECK(qa_hud_destroy(hud, &error));
}

static void test_saved_player_disguise(void)
{
    qa_error error = {0};
    qa_actor_slot_checkpoint slots[2] = {
        {.generation = 1, .owner = 1, .definition = 1, .active = true,
            .player = {.disguised = true}},
        {.generation = 2, .owner = 1, .definition = 1, .active = true,
            .player = {.present = true, .bot = true, .disguised = true, .name = 3}}
    };
    qa_actor_checkpoint saved = {.slots = slots, .count = 2, .capacity = 2}, decoded = {0};
    qa_buffer bytes = {0};
    CHECK(qa_save_actors_encode(&saved, &bytes, &error));
    CHECK(qa_save_actors_decode((qa_bytes){bytes.data, bytes.size}, &decoded, &error));
    CHECK(decoded.count == 2 && decoded.slots[0].player.disguised &&
        !decoded.slots[0].player.present);
    CHECK(decoded.slots[1].player.disguised && decoded.slots[1].player.present &&
        decoded.slots[1].player.bot && decoded.slots[1].player.name == 3);
    qa_actor_checkpoint_free(&decoded);
    qa_buffer_free(&bytes);
}

static void test_unicast_claim_storage(void)
{
    qa_q2_unicast_cache *cache = NULL;
    qa_error error = {0};
    qa_q2_unicast_claim claim = {.client = {.owner = 1, .generation = 2, .slot = 3},
        .connection_epoch = 4, .source = 5, .map_revision = 6,
        .source_frame = 7, .source_time_ns = 8, .key = 1};
    bool duplicate;
    CHECK(qa_q2_unicast_cache_create(&cache, &error));
    for (uint32_t key = 1; key <= 80; ++key) {
        claim.key = key;
        CHECK(qa_q2_unicast_reserve(cache, &error));
        CHECK(qa_q2_unicast_remember(cache, &claim, &error));
    }
    for (uint32_t key = 1; key <= 80; ++key) {
        claim.key = key;
        CHECK(qa_q2_unicast_check(cache, &claim, &duplicate, &error) && duplicate);
    }
    qa_q2_unicast_claim other = claim;
    other.client.owner += UINT64_C(1) << 32;
    CHECK(qa_q2_unicast_check(cache, &other, &duplicate, &error) && !duplicate);
    CHECK(qa_q2_unicast_remember(cache, &other, &error));
    CHECK(qa_q2_unicast_check(cache, &claim, &duplicate, &error) && duplicate);
    qa_q2_unicast_claim previous = claim;
    ++claim.connection_epoch;
    CHECK(qa_q2_unicast_remember(cache, &claim, &error));
    CHECK(qa_q2_unicast_check(cache, &previous, &duplicate, &error) && !duplicate);
    CHECK(qa_q2_unicast_check(cache, &other, &duplicate, &error) && duplicate);
    qa_q2_unicast_remove_client(cache, claim.client);
    CHECK(qa_q2_unicast_check(cache, &claim, &duplicate, &error) && !duplicate);
    CHECK(qa_q2_unicast_check(cache, &other, &duplicate, &error) && duplicate);
    ++other.source_frame;
    CHECK(qa_q2_unicast_remember(cache, &other, &error));
    previous = other;
    --previous.source_frame;
    CHECK(qa_q2_unicast_check(cache, &previous, &duplicate, &error) && !duplicate);
    qa_q2_unicast_remove_source(cache, other.source);
    CHECK(qa_q2_unicast_check(cache, &other, &duplicate, &error) && !duplicate);
    qa_q2_unicast_cache_destroy(cache);
}

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

static void test_shared_team_values(void)
{
    qa_error error={0};qa_session *session=NULL;
    CHECK(qa_session_create(&(qa_session_options){.actor_capacity=1,.component_capacity=1},&session,&error));
    qa_strings *strings=qa_session_strings(session);
    qa_string_id q2,q3,named,leading,red,folded,blue;
    CHECK(qa_strings_intern_cstr(strings,"q2",&q2,&error));
    CHECK(qa_strings_intern_cstr(strings,"q3",&q3,&error));
    CHECK(qa_strings_intern_cstr(strings,"q2:256",&named,&error));
    CHECK(qa_strings_intern_cstr(strings,"q2:0256",&leading,&error));
    CHECK(qa_strings_intern_cstr(strings,"team:red",&red,&error));
    CHECK(qa_strings_intern_cstr(strings,"TeAm:ReD",&folded,&error));
    CHECK(qa_strings_intern_cstr(strings,"q2:2",&blue,&error));
    qa_team_ref numeric=qa_team_number(q2,256),maximum=qa_team_number(q2,INT32_MAX),none={0};
    size_t count=qa_strings_count(strings);
    CHECK(qa_team_present(numeric) && qa_team_valid(strings,maximum));
    CHECK(!qa_team_present(qa_team_number(q2,0)) && !qa_team_present(qa_team_number(q2,-1)));
    CHECK(qa_team_equal(strings,none,qa_team_named(0)));
    CHECK(qa_team_equal(strings,numeric,qa_team_named(named)));
    CHECK(qa_team_equal(strings,qa_team_named(named),numeric));
    CHECK(!qa_team_equal(strings,numeric,qa_team_named(leading)));
    CHECK(!qa_team_equal(strings,numeric,qa_team_number(q3,256)));
    CHECK(!qa_team_equal(strings,numeric,maximum));
    CHECK(qa_team_color_number(strings,qa_team_named(red),QA_TEAM_COLOR_EXACT)==1);
    CHECK(qa_team_color_number(strings,qa_team_named(folded),QA_TEAM_COLOR_EXACT)==0);
    CHECK(qa_team_color_number(strings,qa_team_named(folded),QA_TEAM_COLOR_ALIASES)==1);
    CHECK(qa_team_color_number(strings,qa_team_named(blue),QA_TEAM_COLOR_ALIASES)==2);
    CHECK(qa_team_color_number(strings,qa_team_number(q2,2),QA_TEAM_COLOR_ALIASES)==2);
    CHECK(qa_team_color_number(strings,qa_team_number(q3,1),QA_TEAM_COLOR_EXACT)==0);
    CHECK(qa_team_color_number(strings,maximum,QA_TEAM_COLOR_ALIASES)==0);
    CHECK(qa_strings_count(strings)==count);
    qa_team_ref teams[]={none,qa_team_named(red),numeric,maximum};
    for(size_t i=0;i<sizeof(teams)/sizeof(*teams);++i) {
        qa_source_save_io io={0};qa_buffer saved={0},legacy={0};qa_team_id name;
        CHECK(qa_team_name(strings,teams[i],&name,&error));
        CHECK(qa_source_save_writer(&io,session,&error));
        CHECK(qa_source_save_string(&io,&name) && qa_source_save_finish(&io,&legacy));
        CHECK(qa_source_save_writer(&io,session,&error));
        CHECK(qa_source_save_team(&io,&teams[i]) && qa_source_save_finish(&io,&saved));
        CHECK(saved.size==legacy.size && !memcmp(saved.data,legacy.data,saved.size));
        qa_team_ref restored={0};
        CHECK(qa_source_save_reader(&io,session,(qa_bytes){legacy.data,legacy.size},&error));
        CHECK(qa_source_save_team(&io,&restored) && qa_source_save_finish(&io,NULL));
        CHECK(qa_team_equal(strings,teams[i],restored));
        qa_buffer_free(&saved);qa_buffer_free(&legacy);
    }
    CHECK(qa_session_destroy(session,&error));
}

static void test_source_scalars(void)
{
    CHECK(qa_source_to_i32(16777217.0) == 16777217);
    CHECK(qa_source_to_i32(INT32_MAX) == INT32_MAX);
    CHECK(qa_source_to_i32(-2147483648.0) == INT32_MIN);
    CHECK(qa_source_to_i32(2147483648.0) == INT32_MIN);
    CHECK(qa_source_to_i32(INFINITY) == INT32_MIN);
    CHECK(qa_source_to_i32(NAN) == INT32_MIN);
    CHECK(qa_source_to_i32(1.9) == 1 && qa_source_to_i32(-1.9) == -1);
    const double values[] = {0.0, -0.0, 0.5, 100.0, 16777217.0, -16777217.0,
        INT32_MIN, INT32_MAX, 0.66};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i) {
        qa_error error = {0}; qa_source_save_io io = {0}; qa_buffer saved = {0};
        double value = values[i], restored = 0;
        CHECK(qa_source_save_writer(&io, NULL, &error));
        CHECK(qa_source_save_scalar(&io, &value) && qa_source_save_finish(&io, &saved));
        CHECK(qa_source_save_reader(&io, NULL, (qa_bytes){saved.data, saved.size}, &error));
        CHECK(qa_source_save_scalar(&io, &restored) && qa_source_save_finish(&io, NULL));
        CHECK(restored == values[i] && signbit(restored) == signbit(values[i]));
        float legacy_value = (float)values[i];
        if ((double)legacy_value == values[i]) {
            qa_buffer legacy = {0};
            CHECK(qa_source_save_writer(&io, NULL, &error));
            CHECK(qa_source_save_f32(&io, &legacy_value) && qa_source_save_finish(&io, &legacy));
            CHECK(saved.size == legacy.size && !memcmp(saved.data, legacy.data, saved.size));
            qa_buffer_free(&legacy);
        }
        CHECK(qa_source_save_reader(&io, NULL, (qa_bytes){saved.data, saved.size - 1}, &error));
        CHECK(!qa_source_save_scalar(&io, &restored));
        qa_source_save_dispose(&io); qa_buffer_free(&saved);
    }
    const int32_t counters[] = {0, 100, 16777217, INT32_MIN, INT32_MAX};
    for (size_t i = 0; i < sizeof(counters) / sizeof(*counters); ++i) {
        qa_error error = {0}; qa_source_save_io io = {0}; qa_buffer saved = {0};
        int32_t value = counters[i], restored = 0;
        CHECK(qa_source_save_writer(&io, NULL, &error));
        CHECK(qa_source_save_i32_scalar(&io, &value) && qa_source_save_finish(&io, &saved));
        CHECK(qa_source_save_reader(&io, NULL, (qa_bytes){saved.data, saved.size}, &error));
        CHECK(qa_source_save_i32_scalar(&io, &restored) && qa_source_save_finish(&io, NULL));
        CHECK(restored == counters[i]);
        qa_buffer_free(&saved);
    }
    qa_error error = {0}; qa_source_save_io io = {0}; double invalid = INFINITY;
    CHECK(qa_source_save_writer(&io, NULL, &error));
    CHECK(!qa_source_save_scalar(&io, &invalid));
    qa_source_save_dispose(&io);
}

static void test_module_observation_storage(void)
{
    qa_error error={0};qa_session *session=NULL;
    CHECK(qa_session_create(&(qa_session_options){.actor_capacity=1,.component_capacity=1},&session,&error));
    qa_bot_runtime first={.options={.observations=QA_BOT_OBSERVATION_MODULE},.time=1},
        restored={.options={.observations=QA_BOT_OBSERVATION_MODULE}};
    qa_bot_entity_update update={.origin={1,2,3}};
    CHECK(qa_bot_runtime_update_entity(&first,INT32_MIN,&update,&error));
    CHECK(qa_bot_runtime_update_entity(&first,INT32_MAX,&update,&error));
    for(int32_t number=10;number<90;++number)
        CHECK(qa_bot_runtime_update_entity(&first,number,&update,&error));
    CHECK(qa_bot_runtime_update_entity(&first,INT32_MIN,NULL,&error));
    CHECK(qa_bot_runtime_update_entity(&first,INT32_MIN,&update,&error));
    qa_bot_entity_info row;bool found;int32_t previous=INT32_MAX;
    CHECK(qa_bot_runtime_entity_after(&first,NULL,&row,&found,&error) && found && row.number==INT32_MAX);
    CHECK(qa_bot_runtime_entity_after(&first,&previous,&row,&found,&error) && found && row.number==10);
    qa_buffer saved={0},copy={0};
    CHECK(qa_bot_observations_capture(session,&first,&saved,&error));
    CHECK(qa_bot_observations_restore(session,&restored,(qa_bytes){saved.data,saved.size},&error));
    CHECK(qa_bot_observations_capture(session,&restored,&copy,&error));
    CHECK(saved.size==copy.size && !memcmp(saved.data,copy.data,saved.size));
    qa_buffer_free(&saved);qa_buffer_free(&copy);
    restored.time=first.time;
    CHECK(qa_bot_runtime_update_entity(&first,15,NULL,&error));
    CHECK(qa_bot_runtime_update_entity(&restored,15,NULL,&error));
    CHECK(qa_bot_runtime_update_entity(&first,-5,&update,&error));
    CHECK(qa_bot_runtime_update_entity(&restored,-5,&update,&error));
    CHECK(qa_bot_observations_capture(session,&first,&saved,&error));
    CHECK(qa_bot_observations_capture(session,&restored,&copy,&error));
    CHECK(saved.size==copy.size && !memcmp(saved.data,copy.data,saved.size));
    CHECK(qa_bot_runtime_invalidate_entities(&restored,&error));
    CHECK(qa_bot_runtime_entity(&restored,INT32_MIN,&row,&found,&error) && found && !row.valid);
    restored.time=2;
    CHECK(qa_bot_runtime_update_entity(&restored,INT32_MIN,&update,&error));
    CHECK(qa_bot_runtime_entity(&restored,INT32_MIN,&row,&found,&error) && found && row.valid && row.update_interval==0);
    qa_buffer_free(&saved);qa_buffer_free(&copy);
    bot_runtime_observations_close(&first);bot_runtime_observations_close(&restored);
    CHECK(qa_session_destroy(session,&error));
}

static bool saved_console_context(void *user,uint64_t registry,const qa_command_context *saved,
    qa_command_context *restored,qa_error *error)
{
    (void)user;(void)registry;(void)error;*restored=*saved;return true;
}

static void test_chat_initial_aliases(void)
{
    qa_error error={0};qa_bot_library library={0};
    bot_chat_initial_resource first={.initial={.pointer=7},.published=true};
    bot_chat_initial_resource second={.initial={.pointer=7}};
    qa_bot_chat_asset older={.initial_source=&first},newer={.initial_source=&second};
    chat_asset_admit(&library,&older);chat_asset_admit(&library,&newer);
    CHECK(chat_initial_alias_admit(&library.chat_initial_aliases,7,&newer,&error));
    CHECK(chat_initial_alias_admit(&library.chat_initial_aliases,7,&older,&error));
    size_t count=library.chat_initial_aliases.count;
    CHECK(chat_initial_alias_admit(&library.chat_initial_aliases,7,&older,&error));
    CHECK(library.chat_initial_aliases.count==count);
    CHECK(chat_initial_asset_find(&library,7,false)==&newer);
    CHECK(chat_initial_asset_find(&library,7,true)==&older);
    second.published=true;
    CHECK(chat_initial_asset_find(&library,7,true)==&newer);
    second.initial.pointer=8;
    CHECK(chat_initial_asset_find(&library,7,false)==&older);
    CHECK(!chat_initial_asset_find(&library,8,false));
    CHECK(chat_initial_alias_admit(&library.chat_initial_aliases,8,&newer,&error));
    CHECK(chat_initial_asset_find(&library,8,true)==&newer);
    chat_initial_alias_retire(&library.chat_initial_aliases,&newer);
    CHECK(!chat_initial_asset_find(&library,8,false) && chat_initial_asset_find(&library,7,true)==&older);
    CHECK(library.chat_initial_aliases.count==1);
    qa_id_index_destroy(&library.chat_initial_aliases);
}

static void test_physical_prediction_command(void)
{
    qa_input_command_intent intent={.angles={0,90,0},.move={201.25/512,0,-200.75/512},
        .actions=UINT64_C(1)<<QA_INPUT_CROUCH,.game_focus=true};
    for(qa_ruleset_id kind=QA_RULESET_NETQUAKE;kind<=QA_RULESET_Q3;++kind) {
        qa_input_command_frame frame={.kind=kind,.sequence=UINT32_MAX,.server_frame=-1,
            .numeric_policy=QA_INPUT_COMMAND_PHYSICAL_PREDICTION,.weapon=239,
            .server_time_ms=123,.acknowledged_server_seconds=4.5,
            .delta_angles={10,20,30},.delta_angle_words={-32768,32767,-5}};
        qa_usercmd command;
        qa_usercmd_build(&intent,&frame,300,&command);
        CHECK(command.sequence==UINT32_MAX && command.server_frame==-1 && command.weapon==239);
        CHECK(command.milliseconds==300 && command.duration_ns==0);
        CHECK(command.forward_move==(kind==QA_RULESET_Q3?64:201.25f));
        CHECK(command.up_move==(kind==QA_RULESET_Q3?-64:kind==QA_RULESET_Q2_RERELEASE?0:-200.75f));
        CHECK(command.buttons==(kind==QA_RULESET_Q2_RERELEASE?16u:0u));
        if(kind==QA_RULESET_NETQUAKE)CHECK(command.acknowledged_server_seconds==4.5);
        if(kind==QA_RULESET_Q2_CLASSIC)CHECK(command.angle_words[0]==32768 && command.angle_words[1]==-16383 && command.angle_words[2]==5);
        if(kind==QA_RULESET_Q2_RERELEASE)CHECK(command.angles.x==-10 && command.angles.y==70 && command.angles.z==-30);
        if(kind==QA_RULESET_Q3) {
            CHECK(command.angle_words[0]==32768 && command.angle_words[1]==49153 && command.angle_words[2]==5);
            frame.delta_angle_words[0]=INT32_MIN;frame.delta_angle_words[1]=65535;frame.delta_angle_words[2]=INT32_MAX;
            qa_usercmd_build(&intent,&frame,300,&command);
            CHECK(command.angle_words[0]==INT32_MIN && command.angle_words[1]==16385 && command.angle_words[2]==-INT32_MAX);
        }
        frame.numeric_policy=QA_INPUT_COMMAND_SOURCE;
        qa_usercmd_build(&intent,&frame,300,&command);
        CHECK(command.milliseconds==100 && command.duration_ns==300000000);
        CHECK(command.forward_move==(kind==QA_RULESET_Q3?127:kind==QA_RULESET_Q2_RERELEASE?201.25f:201));
        CHECK(command.weapon==(kind==QA_RULESET_Q3?239:0));
        frame.has_light_level=true;
        frame.light_level=0;
        qa_usercmd_build(&intent,&frame,300,&command);
        CHECK(command.has_light_level && command.light_level==0);
        frame.light_level=37;
        qa_usercmd_build(&intent,&frame,300,&command);
        CHECK(command.has_light_level && command.light_level==37);
        qa_usercmd unknown=command;
        unknown.has_light_level=false;
        CHECK(!qa_usercmd_equal(&command,&unknown));
        qa_input_command_basis from={.kind=kind},to={.kind=QA_RULESET_Q3};
        qa_usercmd converted;
        qa_input_command_convert(&command,NULL,&from,&to,(qa_input_axis_rule){0},&converted);
        CHECK(converted.has_light_level && converted.light_level==37);
    }
}
static void test_console_restore_pages(void)
{
    qa_error error={0};qa_session *session=NULL;
    CHECK(qa_session_create(&(qa_session_options){.actor_capacity=1,.component_capacity=1},&session,&error));
    qa_command_context context={.session=17,.dialect=QA_RULESET_Q3,.origin=QA_COMMAND_LOCAL};
    qa_console *console=qa_console_create(&(qa_console_options){.context=context,.disable_builtins=true},&error);
    CHECK(console);
    context.script="current.cfg";
    qa_command_context caller=context;caller.script="parent.cfg";
    const char text[]="echo ready\n";
    command_chunk *first=qac_console_chunk_create(console,&context,&caller,text,sizeof(text)-1,false,&error);
    command_chunk *empty=qac_console_chunk_create(console,&context,&caller,"",0,false,&error);
    command_chunk *completion=qac_console_chunk_create(console,&context,&caller,NULL,0,false,&error);
    CHECK(first && empty && completion);
    first->offset=5;first->next=empty;empty->next=completion;
    completion->completion=completion->success=true;
    console->head=first;console->tail=completion;console->queued_bytes=first->length-first->offset;
    qa_buffer saved={0},copy={0};CHECK(qa_console_save_capture(console,session,&saved,&error));
    size_t held_slot,held_count=console->command_pages.capacity-console->command_pages.active;
    CHECK(qa_pool_take_run(&console->command_pages,held_count,&held_slot));
    size_t active=console->command_pages.active;
    qa_console_save_resolvers resolve={.command_context=saved_console_context};
    CHECK(!qa_console_save_restore(console,session,&resolve,(qa_bytes){saved.data,saved.size-1},&error));
    CHECK(console->head==first && first->offset==5 && console->command_pages.active==active && console->command_storage.sealed);
    CHECK(qa_console_save_restore(console,session,&resolve,(qa_bytes){saved.data,saved.size},&error));
    CHECK(console->head->storage==&console->command_pages && console->head->offset==5);
    CHECK(!strcmp(console->head->context.script,"current.cfg") && !strcmp(console->head->caller.script,"parent.cfg"));
    CHECK(console->head->next->text && !console->head->next->text[0] && !console->tail->text && console->tail->completion);
    CHECK(qa_console_save_capture(console,session,&copy,&error));
    CHECK(copy.size==saved.size && !memcmp(copy.data,saved.data,saved.size));
    qa_buffer_free(&copy);qa_pool_release_run(&console->command_pages,held_slot,held_count);
    size_t capacity=console->command_pages.capacity;
    for(size_t i=0;i<4;++i) {
        CHECK(qa_console_save_restore(console,session,&resolve,(qa_bytes){saved.data,saved.size},&error));
        CHECK(console->command_pages.capacity==capacity && console->command_storage.sealed);
    }
    qa_buffer_free(&saved);qa_console_destroy(console);CHECK(qa_session_destroy(session,&error));
}

typedef struct release_command_counts { size_t completed, failed; } release_command_counts;
static bool release_count_command(void *user,const qa_command_invocation *command,qa_error *error)
{
    (void)command;(void)error;
    ++((release_command_counts *)user)->completed;
    return true;
}
static bool release_fail_command(void *user,const qa_command_invocation *command,qa_error *error)
{
    (void)command;
    ++((release_command_counts *)user)->failed;
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"command rejected");
    return false;
}
static void test_console_failed_release(void)
{
    qa_error error={0};release_command_counts counts={0};
    qa_command_context context={.owner=1,.dialect=QA_RULESET_Q3,.origin=QA_COMMAND_LOCAL};
    qa_console *console=qa_console_create(&(qa_console_options){.context=context},&error);
    CHECK(console);
    CHECK(qa_console_register(console,"releasecount","",1,false,release_count_command,&counts,&error));
    CHECK(qa_console_register(console,"releasefail","",1,false,release_fail_command,&counts,&error));
    CHECK(qa_console_append(console,&context,"releasecount\n",&error));
    qa_console_release *release=NULL;qa_console_release_outcome outcome;
    CHECK(qa_console_release_prepare(console,&context,"releasefail; releasecount\n",&release,&error));
    CHECK(!qa_console_release_advance(release,&outcome,&error));
    CHECK(counts.failed==1 && counts.completed==0 && qa_console_pending(console));
    error=(qa_error){0};
    CHECK(qa_console_release_abort(release,&outcome,&error) && outcome==QA_CONSOLE_RELEASE_FAILED);
    CHECK(qa_console_drain(console,0,NULL,&error));
    CHECK(counts.failed==1 && counts.completed==1);
    release=NULL;
    CHECK(qa_console_release_prepare(console,&context,"wait 2; releasecount\n",&release,&error));
    CHECK(qa_console_release_advance(release,&outcome,&error));
    CHECK(qa_console_release_entered(release) && qa_console_pending(console));
    CHECK(!qa_console_release_abort(release,&outcome,&error));
    error=(qa_error){0};
    for (size_t i=0;i<4 && outcome!=QA_CONSOLE_RELEASE_COMPLETED;++i)
        CHECK(qa_console_release_advance(release,&outcome,&error));
    CHECK(outcome==QA_CONSOLE_RELEASE_COMPLETED && counts.failed==1 && counts.completed==2);
    CHECK(qa_console_release_abort(release,&outcome,&error));
    CHECK(qa_console_destroy_ready(console));qa_console_destroy(console);
}

typedef struct console_precedence_counts {
    size_t forwarded, called;
    qa_command_result forward_result, callback_result;
} console_precedence_counts;
static qa_command_result console_precedence_forward(void *user,
    const qa_command_invocation *command,qa_error *error)
{
    (void)command;(void)error;
    console_precedence_counts *counts=user;
    ++counts->forwarded;return counts->forward_result;
}
static qa_command_result console_precedence_callback(void *user,
    const qa_command_invocation *command,qa_error *error)
{
    (void)command;(void)error;
    console_precedence_counts *counts=user;
    ++counts->called;return counts->callback_result;
}
static void test_console_command_precedence(void)
{
    qa_error error={0};
    qa_cvars *cvars=qa_cvars_create(&(qa_cvar_options){.dialect=QA_RULESET_Q3},&error);
    CHECK(cvars);
    CHECK(qa_cvars_register(cvars,"qa_forward_collision","original",0,1,"",&error));
    CHECK(qa_cvars_register(cvars,"qa_callback_collision","original",0,1,"",&error));
    CHECK(qa_cvars_register(cvars,"qa_plain_cvar","original",0,1,"",&error));
    CHECK(qa_cvars_register(cvars,"qa_declared_receiver","original",0,1,"",&error));
    CHECK(qa_cvars_register(cvars,"qa_foreign_callback","original",0,1,"",&error));
    qa_command_context context={.owner=1,.dialect=QA_RULESET_Q3,.origin=QA_COMMAND_LOCAL,
        .cvar_view=qa_cvars_view_identity(cvars)};
    console_precedence_counts counts={.forward_result=QA_COMMAND_HANDLED,.callback_result=QA_COMMAND_HANDLED};
    qa_console *console=qa_console_create(&(qa_console_options){.context=context,.cvars=cvars,
        .user=&counts,.forward=console_precedence_forward},&error);
    CHECK(console);
    CHECK(qa_console_register(console,"qa_forward_collision","",1,false,NULL,NULL,&error));
    CHECK(qa_console_register(console,"qa_forward_only","",1,false,NULL,NULL,&error));
    qa_console_contribution contribution={.receiver=1,.lifetime_owner=2,.cvar_view=context.cvar_view,
        .callback=console_precedence_callback,.user=&counts};
    CHECK(qa_console_contribute(console,"qa_callback_collision",&contribution,&error));
    CHECK(qa_console_execute_now(console,&context,"qa_forward_collision sent",&error));
    CHECK(counts.forwarded==1 && !counts.called);
    CHECK(!strcmp(qa_cvars_find(cvars,"qa_forward_collision")->value,"original"));
    counts.forward_result=QA_COMMAND_UNHANDLED;
    CHECK(qa_console_execute_now(console,&context,"qa_forward_collision fallback",&error));
    CHECK(counts.forwarded==2 && !counts.called);
    CHECK(!strcmp(qa_cvars_find(cvars,"qa_forward_collision")->value,"fallback"));
    CHECK(qa_console_execute_now(console,&context,"qa_callback_collision sent",&error));
    CHECK(counts.forwarded==2 && counts.called==1);
    CHECK(!strcmp(qa_cvars_find(cvars,"qa_callback_collision")->value,"original"));
    counts.callback_result=QA_COMMAND_UNHANDLED;
    CHECK(qa_console_execute_now(console,&context,"qa_callback_collision fallback",&error));
    CHECK(counts.forwarded==2 && counts.called==2);
    CHECK(!strcmp(qa_cvars_find(cvars,"qa_callback_collision")->value,"fallback"));
    CHECK(qa_console_execute_now(console,&context,"qa_plain_cvar changed",&error));
    CHECK(counts.forwarded==2 && counts.called==2);
    CHECK(!strcmp(qa_cvars_find(cvars,"qa_plain_cvar")->value,"changed"));
    CHECK(qa_console_execute_now(console,&context,"qa_forward_only declined",&error));
    CHECK(counts.forwarded==3 && counts.called==2);
    CHECK(!qa_cvars_find(cvars,"qa_forward_only"));
    CHECK(qa_console_register(console,"qa_declared_receiver","",2,false,NULL,NULL,&error));
    counts.forward_result=QA_COMMAND_HANDLED;
    CHECK(qa_console_execute_now(console,&context,"qa_declared_receiver sent",&error));
    CHECK(counts.forwarded==4 && counts.called==2);
    CHECK(!strcmp(qa_cvars_find(cvars,"qa_declared_receiver")->value,"original"));
    contribution.receiver=2; contribution.lifetime_owner=3;
    CHECK(qa_console_contribute(console,"qa_foreign_callback",&contribution,&error));
    CHECK(qa_console_execute_now(console,&context,"qa_foreign_callback fallback",&error));
    CHECK(counts.forwarded==4 && counts.called==2);
    CHECK(!strcmp(qa_cvars_find(cvars,"qa_foreign_callback")->value,"fallback"));
    qa_console_destroy(console);qa_cvars_destroy(cvars);
}

static void test_shared_video_mode_recovery(void)
{
    qa_error error={0};qa_cvar_video_mode mode;
    qa_cvar_video_query query={.member="gl_mode",.dialect=QA_RULESET_Q2_CLASSIC,.index=5};
    CHECK(frontend_shared_video_resolve(NULL,&query,&mode,&error));
    CHECK(mode.index==5 && mode.width==960 && mode.height==720);
    query.index=10;query.width=960;query.height=720;
    CHECK(frontend_shared_video_resolve(NULL,&query,&mode,&error));
    CHECK(mode.index==5 && mode.width==960 && mode.height==720);
    query.width=query.height=0;
    CHECK(frontend_shared_video_resolve(NULL,&query,&mode,&error));
    CHECK(mode.index==3 && mode.width==640 && mode.height==480);
    query.member="r_mode";query.index=10;
    CHECK(frontend_shared_video_resolve(NULL,&query,&mode,&error));
    CHECK(mode.index==10 && mode.width==2048 && mode.height==1536);
    query.member="sw_mode";query.index=-1;query.width=320;query.height=200;
    CHECK(frontend_shared_video_resolve(NULL,&query,&mode,&error));
    CHECK(mode.index==-1 && mode.width==320 && mode.height==200);
    query.width=32;
    CHECK(!frontend_shared_video_resolve(NULL,&query,&mode,&error));
}

static void test_shared_campaign_modes(void)
{
    CHECK(qa_mode_from_game_type(QA_GAME_TYPE_CAMPAIGN) == QA_MODE_SINGLE_PLAYER);
    CHECK(qa_mode_from_game_type(QA_GAME_TYPE_COOPERATIVE) == QA_MODE_COOPERATIVE);
    CHECK(qa_mode_from_game_type(QA_GAME_TYPE_HARVESTER) == QA_MODE_HARVESTER);
    CHECK(qa_mode_from_game_type(-1) == QA_MODE_FFA);
}

static void test_debug_geometry_wire(void)
{
    qa_debug_line lines[] = {
        {.start = {1, 2, 3}, .end = {4, 5, 6}, .color = {1, 0.5f, 0, 0.75f}, .depth_test = true, .width = 1},
        {.start = {-1, -2, -3}, .end = {-4, -5, -6}, .color = {0, 1, 0.5f, 1}}};
    qa_unified_presentation_payload value = {.kind = QA_UNIFIED_PRESENTATION_DEBUG_GEOMETRY,
        .value.debug = {.lines = lines, .count = 2,
            .time_ns = UINT64_C(9007199254740993), .lifetime_ns = UINT64_C(1250000000)}};
    qa_unified_presentation_payload decoded = {0};
    qa_error error = {0}; qa_buffer wire = {0};
    CHECK(qa_unified_record_delta_encode(&qa_unified_presentation_payload_layout,
        &value, NULL, 65536, &wire, NULL, &error));
    CHECK(qa_unified_record_delta_decode(&qa_unified_presentation_payload_layout,
        (qa_bytes){wire.data, wire.size}, NULL, &decoded, NULL, NULL, NULL, NULL, NULL, &error));
    CHECK(decoded.kind == value.kind && decoded.value.debug.count == 2);
    CHECK(decoded.value.debug.time_ns == value.value.debug.time_ns);
    CHECK(decoded.value.debug.lifetime_ns == value.value.debug.lifetime_ns);
    CHECK(qa_unified_record_equal(&qa_unified_presentation_payload_layout,
        &value, &decoded, NULL, NULL));
    qa_unified_record_dispose(&qa_unified_presentation_payload_layout, &decoded);
    qa_buffer_free(&wire);
}

static void test_prediction_continuations(void)
{
    qa_error error={0};
    qa_unified_weapon_state weapon={.kind=QA_GAME_Q2,.gun_frame=4,.state=3};
    qa_unified_animation_state animation={.family=QA_GAME_Q2,.frame=95,.end_frame=100};
    qa_unified_frame_prediction previous={0};
    for (unsigned mask=0;mask<4;++mask) {
        qa_unified_frame_prediction source={.actor={.registry=7,.slot=1,.generation=2},
            .sequence=12,.command_time_ms=125,.state={.kind=QA_RULESET_Q2_RERELEASE},
            .view_angles={1,2,3},.view_height=22,
            .weapon=(mask&1)?&weapon:NULL,.animation=(mask&2)?&animation:NULL};
        qa_unified_frame_prediction decoded={0}; qa_buffer wire={0};
        CHECK(qa_unified_record_delta_encode(&qa_unified_prediction_layout,
            &source,mask?&previous:NULL,65536,&wire,NULL,&error));
        CHECK(qa_unified_record_delta_decode(&qa_unified_prediction_layout,
            (qa_bytes){wire.data,wire.size},mask?&previous:NULL,&decoded,
            NULL,NULL,NULL,NULL,NULL,&error));
        CHECK(qa_unified_record_equal(&qa_unified_prediction_layout,&source,&decoded,NULL,NULL));
        CHECK(decoded.state.kind==QA_RULESET_Q2_RERELEASE && decoded.sequence==12 &&
            decoded.command_time_ms==125 && decoded.view_height==22 && decoded.view_angles.y==2);
        CHECK(!!decoded.weapon==!!(mask&1) && !!decoded.animation==!!(mask&2));
        qa_unified_record_dispose(&qa_unified_prediction_layout,&previous);
        previous=decoded; qa_buffer_free(&wire);
    }
    qa_unified_record_dispose(&qa_unified_prediction_layout,&previous);
}

static bool debug_clock_read(void *context, const qa_debug_clock *clock, double *out) {
    if (clock->provider != 1) return false;
    *out = *(double *)context;
    return true;
}
static void test_debug_geometry_lifetime(void)
{
    qa_debug_store *store = NULL; qa_error error = {0};
    qa_arena scratch; qa_arena_init(&scratch, 1024);
    const qa_debug_line *snapshot = NULL; size_t count = 0;
    qa_debug_line line = {.start = {1, 0, 0}, .end = {2, 0, 0}, .color = {1, 1, 1, 1}};
    CHECK(qa_debug_store_create(2, &store, &error));
    CHECK(qa_debug_store_submit(store, &line, 1, 0, 250000, (qa_debug_clock){0}, &error));
    CHECK(qa_debug_store_snapshot(store, 0.1, 1, NULL, NULL, &scratch, &snapshot, &count, &error) && count == 1);
    CHECK(qa_debug_store_snapshot(store, 0.25, 1, NULL, NULL, &scratch, &snapshot, &count, &error) && count == 0);
    CHECK(qa_debug_store_submit(store, &line, 1, 0, UINT64_C(6000000000000000), (qa_debug_clock){0}, &error));
    CHECK(qa_debug_store_snapshot(store, (double)UINT32_MAX + 1, 2, NULL, NULL, &scratch, &snapshot, &count, &error) && count == 1);
    qa_debug_store_clear(store);
    CHECK(qa_debug_store_submit(store, &line, 1, 10, 0, (qa_debug_clock){0}, &error));
    CHECK(qa_debug_store_snapshot(store, 10, 3, NULL, NULL, &scratch, &snapshot, &count, &error) && count == 1);
    CHECK(qa_debug_store_snapshot(store, 10, 3, NULL, NULL, &scratch, &snapshot, &count, &error) && count == 1);
    CHECK(qa_debug_store_snapshot(store, 10, 4, NULL, NULL, &scratch, &snapshot, &count, &error) && count == 0);
    double source_time = 10;
    CHECK(qa_debug_store_submit(store, &line, 1, source_time, 1000000, (qa_debug_clock){.provider = 1}, &error));
    CHECK(qa_debug_store_snapshot(store, 1000, 5, debug_clock_read, &source_time, &scratch, &snapshot, &count, &error) && count == 1);
    CHECK(qa_debug_store_snapshot(store, 2000, 6, debug_clock_read, &source_time, &scratch, &snapshot, &count, &error) && count == 1);
    source_time = 11;
    CHECK(qa_debug_store_snapshot(store, 2001, 7, debug_clock_read, &source_time, &scratch, &snapshot, &count, &error) && count == 0);
    qa_debug_store_destroy(store); qa_arena_destroy(&scratch);
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
    qa_vec3 axis[3]={{1,0,0},{0,1,1},{0,0,2}};
    qa_vec3 component=qa_axes_angles(axis,QA_AXES_FLOAT_COMPONENTS);
    qa_vec3 basis=qa_axes_angles(axis,QA_AXES_DOUBLE_BASIS);
    CHECK(component.x==0 && component.y==0 && fabsf(component.z-26.565051f)<1e-5f);
    CHECK(basis.x==0 && basis.y==0 && fabsf(basis.z-45)<1e-5f);
    qa_vec3 original=qa_v3(17.5f,83.125f,-42.75f);
    qa_angle_axis(original,axis,QA_ANGLE_FLOAT_MATH);
    basis=qa_axes_angles(axis,QA_AXES_DOUBLE_BASIS);
    CHECK(fabsf(basis.x-original.x)<1e-5f && fabsf(basis.y-original.y)<1e-5f &&
        fabsf(basis.z-original.z)<1e-5f);
}
static void test_shared_bounds_and_transform(void)
{
    qa_bounds unit={qa_v3(0,0,0),qa_v3(1,1,1)};
    qa_bounds contact=qa_bounds_translate(unit,qa_v3(1,0,0));
    CHECK(qa_bounds_overlap(unit,contact) && !qa_bounds_overlap_strict(unit,contact));
    CHECK(qa_bounds_overlap_strict(unit,qa_bounds_translate(unit,qa_v3(.5f,0,0))));
    CHECK(!qa_bounds_overlap(unit,qa_bounds_translate(unit,qa_v3(2,0,0))));
    qa_bounds centered={qa_v3(-1,-1,-1),qa_v3(1,1,1)};
    CHECK(qa_bounds_intersects_segment(centered,qa_v3(-2,0,0),qa_v3(2,0,0)));
    CHECK(qa_bounds_intersects_segment(centered,qa_v3(-2,0,0),qa_v3(0,2,0)));
    CHECK(!qa_bounds_intersects_segment(centered,qa_v3(-2,.5f,0),qa_v3(.5f,2,0)));
    CHECK(qa_bounds_intersects_segment(centered,qa_v3(1,1,1),qa_v3(1,1,1)));
    CHECK(!qa_bounds_intersects_segment(centered,qa_v3(1.01f,1,1),qa_v3(1.01f,1,1)));
    CHECK(qa_bounds_intersects_segment((qa_bounds){0},qa_v3(-1,0,0),qa_v3(1,0,0)));
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
    qa_scene_frame frame={0};
    qa_scene_view world={.seat=1,.viewport={320,0,320,200},
        .projection=qa_scene_projection(90,75,1,100)};
    qa_scene_rect_f left_hud={0,170,320,30};
    qa_scene_weapon_occupation left={.seat=0,.rects=&left_hud,.count=1};
    qa_scene_frame_weapon_bind(&frame,&left);
    qa_scene_view weapon=qa_scene_weapon_view(&frame,world);
    CHECK(!memcmp(&weapon.projection,&world.projection,sizeof(world.projection)));
    qa_scene_rect_f right_hud={320,170,320,30};
    qa_scene_weapon_occupation right={.seat=1,.rects=&right_hud,.count=1};
    qa_scene_frame_weapon_bind(&frame,&right);
    weapon=qa_scene_weapon_view(&frame,world);
    qa_vec4 before=qa_scene_matrix_point(world.projection,qa_v3(0,0,-5));
    qa_vec4 after=qa_scene_matrix_point(weapon.projection,qa_v3(0,0,-5));
    CHECK(after.y/after.w>before.y/before.w && after.x==before.x &&
        after.z==before.z && after.w==before.w);
    CHECK(weapon.projection.m[0]==world.projection.m[0] &&
        !memcmp(&weapon.viewport,&world.viewport,sizeof(world.viewport)));
}
static void construction_actor_released(void *state,qa_session *session,qa_actor_record actor)
{
    (void)session; (void)actor;
    ++*(unsigned *)state;
}
static void test_component_construction_actors(void)
{
    qa_error error={0}; qa_session *session=NULL;
    CHECK(qa_session_create(&(qa_session_options){.actor_capacity=2,.component_capacity=1},&session,&error));
    qa_string_id owner,definition;
    CHECK(qa_strings_intern_cstr(qa_session_strings(session),"construction-owner",&owner,&error));
    CHECK(qa_strings_intern_cstr(qa_session_strings(session),"construction-actor",&definition,&error));
    unsigned released=0;
    qa_component source={.owner=owner,.state=&released,.clock=qa_clock_defaults(QA_RULESET_Q2_CLASSIC),
        .actor_released=construction_actor_released};
    qa_component_admission *admission=NULL;
    CHECK(qa_session_prepare_component(session,&source,0,&admission,&error));
    const qa_clock_state *prepared=qa_component_admission_clock(admission);
    CHECK(prepared && prepared->frame.provider==owner && prepared->frame.kind==QA_RULESET_Q2_CLASSIC);
    qa_clock_state clock;
    CHECK(!qa_session_clock(session,owner,&clock));
    qa_actor_id actor;
    CHECK(qa_session_actor_allocation_ready(session,owner));
    CHECK(qa_session_allocate(session,owner,definition,true,0,&actor,&error));
    qa_actor_owner execution;
    CHECK(qa_session_execution(session,actor,&execution) && execution==owner);
    qa_component_admission_abort(admission);
    CHECK(!qa_actors_get(qa_session_actors(session),actor) && released==1);
    CHECK(!qa_session_actor_allocation_ready(session,owner));
    CHECK(qa_session_prepare_component(session,&source,0,&admission,&error));
    CHECK(qa_session_allocate(session,owner,definition,true,0,&actor,&error));
    CHECK(qa_component_admission_commit(admission,&error));
    CHECK(qa_session_clock(session,owner,&clock) && clock.frame.provider==owner);
    CHECK(qa_actors_get(qa_session_actors(session),actor) && released==1);
    CHECK(qa_session_remove(session,owner,&error) && released==2);
    CHECK(qa_session_destroy(session,&error));
}
static void test_shared_vector_geometry(void)
{
    qa_vec3 v=qa_v3(3,4,0);
    CHECK(qa_vec_normalize_length(&v)==5 && v.x==3.f/5 && v.y==4.f/5 && v.z==0);
    v=qa_v3(0,-0.f,0);
    CHECK(qa_vec_normalize_length(&v)==0 && signbit(v.y));
    CHECK(qa_vec_normalize_length_rule(&v,QA_LENGTH_DOUBLE_SQRT_CLEAR_ZERO)==0 &&
        !signbit(v.x) && !signbit(v.y) && !signbit(v.z));
    v=qa_v3(0x1p-100f,-0x1p-100f,0);
    CHECK(qa_vec_normalize_length_rule(&v,QA_LENGTH_DOUBLE_SQRT_CLEAR_ZERO)==0 &&
        qa_vec_equal(v,qa_v3(0,0,0)) && !signbit(v.y));
    float narrowed=7;
    CHECK(qa_float_round_finite(-0.,&narrowed) && signbit(narrowed));
    CHECK(qa_float_round_finite((double)FLT_MAX+0x1p102,&narrowed) && narrowed==FLT_MAX);
    CHECK(qa_float_round_finite(-(double)FLT_MAX-0x1p102,&narrowed) && narrowed==-FLT_MAX);
    narrowed=7;
    CHECK(!qa_float_round_finite(0x1.ffffffp127,&narrowed) && narrowed==7);
    CHECK(!qa_float_round_finite(INFINITY,&narrowed) && narrowed==7);
    CHECK(!qa_float_round_finite(NAN,&narrowed) && narrowed==7);
    qa_vec3 axis[3],identity[3]={{1,0,0},{0,1,0},{0,0,1}},product[3];
    qa_angle_axis(qa_v3(12,37,21),axis,QA_ANGLE_VM_MATH);
    qa_vec3 world=qa_axis_from_local(qa_v3(3,-4,5),axis,QA_AXIS_ORDERED_SUM);
    CHECK(qa_vec_length(qa_vec_sub(qa_axis_to_local(world,axis),qa_v3(3,-4,5)))<2e-6f);
    qa_axis_multiply(axis,identity,product);
    CHECK(!memcmp(product,axis,sizeof(axis)));
    qa_axis_multiply(product,identity,product);
    CHECK(!memcmp(product,axis,sizeof(axis)));
    qa_vec3 normal=qa_vec_normalize(qa_v3(1,2,3)),side=qa_vec_perpendicular(normal,QA_PERPENDICULAR_RECIPROCAL_PROJECTION);
    CHECK(fabsf(qa_vec_dot(normal,side))<1e-6f && fabsf(qa_vec_length(side)-1)<1e-6f);
    qa_vec3 rotated=qa_vec_rotate_around(qa_v3(0,0,1),qa_v3(1,0,0),90,QA_ROTATION_MATRIX);
    CHECK(fabsf(rotated.x)<1e-6f && fabsf(rotated.y-1)<1e-6f && rotated.z==0);
    rotated=qa_vec_rotate_euler_xyz(qa_v3(1,2,3),qa_v3(90,90,90));
    CHECK(qa_vec_length(qa_vec_sub(rotated,qa_v3(3,2,-1)))<1e-6f);
    rotated=qa_vec_rotate_euler_xyz(qa_v3(1,2,3),qa_v3(-90,-90,-90));
    CHECK(qa_vec_length(qa_vec_sub(rotated,qa_v3(3,-2,1)))<1e-6f);
    CHECK(fabsf(qa_inverse_sqrt(4)-.5f)<.001f);
    CHECK(fabsf(qa_vec_length(qa_vec_fast_normalize(qa_v3(3,4,0)))-1)<.002f);
}
static void test_shared_angle_wrapping(void)
{
    CHECK(qa_vec_equal(qa_v3(0,1,2),qa_v3(-0.f,1,2)));
    CHECK(!qa_vec_equal(qa_v3(NAN,1,2),qa_v3(NAN,1,2)));
    CHECK(qa_bounds_equal((qa_bounds){{0,1,2},{3,4,5}},(qa_bounds){{-0.f,1,2},{3,4,5}}));
    CHECK(!qa_bounds_equal((qa_bounds){{0,1,2},{3,4,5}},(qa_bounds){{0,1,2},{3,4,6}}));
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
    CHECK(qa_angle_lerp(350,10,.5f,QA_ANGLE_LERP_SINGLE_TURN)==360);
    CHECK(qa_angle_lerp(0,720,.5f,QA_ANGLE_LERP_SINGLE_TURN)==180);
    CHECK(qa_angle_lerp(0,720,.5f,QA_ANGLE_LERP_NORMALIZED_DELTA)==0);
    CHECK(qa_angle_lerp(0,180,.5f,QA_ANGLE_LERP_NORMALIZED_DELTA)==90);
    CHECK(qa_angle_lerp(0,-180,.5f,QA_ANGLE_LERP_NORMALIZED_DELTA)==-90);
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
    float integer_zero=qa_vec_yaw(qa_v3(1,-0.f,0),QA_DIRECTION_INTEGER,true);
    CHECK(integer_zero==0 && !signbit(integer_zero));
    unsigned qc_flags=QA_VECTOR_WRAPPED|QA_VECTOR_SQUARED_HORIZONTAL|
        QA_VECTOR_DOUBLE_SQRT|QA_VECTOR_VERTICAL_CARDINAL;
    qa_vec3 qc=qa_vec_angles(qa_v3(1,1,1),QA_DIRECTION_INTEGER,qc_flags);
    CHECK(qc.x==35 && qc.y==45 && qc.z==0);
    qc=qa_vec_angles(qa_v3(0,0,0),QA_DIRECTION_INTEGER,qc_flags);
    CHECK(qc.x==270 && qc.y==0 && !signbit(qc.y));
    qc=qa_vec_angles(qa_v3(1,-0.f,-0.f),QA_DIRECTION_INTEGER,qc_flags);
    CHECK(qc.x==0 && qc.y==0 && !signbit(qc.x) && !signbit(qc.y));
    qa_vec3 native=qa_vec_angles(qa_v3(0,-1,-1),QA_DIRECTION_NATIVE_FLOAT,
        QA_VECTOR_SOURCE|QA_VECTOR_DOUBLE_SQRT);
    CHECK(native.x== -315 && native.y==270 && native.z==0);
    native=qa_vec_angles(qa_v3(0,0,0),QA_DIRECTION_NATIVE_FLOAT,
        QA_VECTOR_SOURCE|QA_VECTOR_DOUBLE_SQRT);
    CHECK(native.x== -270 && native.y==0 && native.z==0);
    qa_vec3 axis[3];qa_angle_axis(wrapped,axis,QA_ANGLE_FLOAT_MATH);
    CHECK(qa_vec_length(qa_vec_sub(axis[0],qa_vec_normalize(input)))<1e-6f);
}
static void test_field_completion(void)
{
    qa_error error={0};
    qa_text_field *field=qa_text_field_create(64,16,&error);
    CHECK(field);
    char first[]="food", second[]="Foo", duplicate[]="foo";
    struct { const char *name; unsigned unused; } names[]={
        {first,1},{second,2},{duplicate,3}};
    qa_text_field_set(field,"fo \"caf\xc3\xa9\"");
    qa_text_field_cursor(field,2);
    CHECK(qa_text_field_complete(field,(qa_field_completion_names){
        .data=&names[0].name,.count=3,.stride=sizeof(names[0])},false,&error));
    qa_field_view view=qa_text_field_read(field);
    CHECK(!strcmp(view.text,"/Foo \"caf\xc3\xa9\"") && view.cursor==4);
    memset(first,'x',4); memset(second,'x',3); memset(duplicate,'x',3);
    CHECK(qa_text_field_complete(field,(qa_field_completion_names){0},false,&error));
    CHECK(!strcmp(qa_text_field_read(field).text,"/food \"caf\xc3\xa9\""));
    CHECK(qa_text_field_complete(field,(qa_field_completion_names){0},true,&error));
    CHECK(!strcmp(qa_text_field_read(field).text,"/Foo \"caf\xc3\xa9\""));
    qa_text_field_set(field,"\xc3\xa9");
    const char *unicode[]={"\xc3\xa9lan"};
    CHECK(qa_text_field_complete(field,(qa_field_completion_names){
        .data=unicode,.count=1,.stride=sizeof(*unicode)},false,&error));
    view=qa_text_field_read(field);
    CHECK(!strcmp(view.text,"/\xc3\xa9lan") && view.cursor==5 && view.length==5);
    qa_text_field_destroy(field);
    field=qa_text_field_create(4,4,&error);
    CHECK(field);
    qa_text_field_set(field,"foo");
    const char *long_name[]={"food"};
    CHECK(qa_text_field_complete(field,(qa_field_completion_names){
        .data=long_name,.count=1,.stride=sizeof(*long_name)},false,&error));
    view=qa_text_field_read(field);
    CHECK(!strcmp(view.text,"foo") && !view.completion);
    qa_text_field_destroy(field);
}
static void test_source_text(void)
{
    CHECK(qa_i32_mul(INT32_MAX,2)==-2);
    CHECK(qa_i32_mul(INT32_MIN,-1)==INT32_MIN);
    CHECK(qa_i32_add(INT32_MAX,1)==INT32_MIN);
    CHECK(qa_i32_add(INT32_MIN,-1)==INT32_MAX);
    CHECK(qa_i32_sub(INT32_MIN,1)==INT32_MAX);
    CHECK(qa_i32_sub(INT32_MAX,-1)==INT32_MIN);
    CHECK(qa_i32_sub(INT32_MIN,INT32_MIN)==0);
    CHECK(qa_i32_add(-1,1)==0);
    CHECK(qa_parse_decimal_i32(" \t-2147483649x",QA_DECIMAL_ASCII_CLAMP)==INT32_MIN);
    CHECK(qa_parse_decimal_i32("+2147483648",QA_DECIMAL_ASCII_CLAMP)==INT32_MAX);
    CHECK(qa_parse_decimal_i32("2147483648",QA_DECIMAL_SIGNED_WRAP)==INT32_MIN);
    CHECK(qa_parse_decimal_i32("4294967296",QA_DECIMAL_SIGNED_WRAP)==0);
    CHECK(qa_parse_decimal_i32("-4294967295",QA_DECIMAL_SIGNED_WRAP)==1);
    CHECK(qa_parse_decimal_i32("\xff\x01+12.5",QA_DECIMAL_SIGNED_WRAP)==12);
    CHECK(qa_parse_decimal_i32("\xff\x01+12.5",QA_DECIMAL_CONTROL_WRAP)==0);
    CHECK(qa_parse_decimal_i32("\x01+12.5",QA_DECIMAL_CONTROL_WRAP)==12);
    CHECK(qa_parse_decimal_i32("\x01+12.5",QA_DECIMAL_ASCII_WRAP)==0);
    CHECK(qa_parse_decimal_i32("999999999999999999999999999",QA_DECIMAL_ASCII_CLAMP)==INT32_MAX);
    CHECK(qa_parse_decimal_i32("-x",QA_DECIMAL_SIGNED_WRAP)==0);
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
    const uint8_t upper[]={0xc3,0x84,0xc4,0xb0,0xce,0xa3,0xce,0x9f,0xce,0xa3};
    const uint8_t lower[]={0xc3,0xa4,'i',0xcc,0x87,0xcf,0x83,0xce,0xbf,0xcf,0x82};
    qa_buffer owned={0},borrowed={0};qa_arena storage={0};
    CHECK(qa_utf8_lower((qa_bytes){upper,sizeof(upper)},&owned,&error));
    CHECK(owned.size==sizeof(lower) && !memcmp(owned.data,lower,sizeof(lower)) && !owned.data[owned.size]);
    CHECK(qa_utf8_lower_arena((qa_bytes){upper,sizeof(upper)},&storage,&borrowed,&error));
    CHECK(borrowed.size==owned.size && !memcmp(borrowed.data,owned.data,owned.size+1));
    qa_buffer_free(&owned);qa_arena_reset(&storage);
    CHECK(qa_utf8_lower_arena((qa_bytes){0},&storage,&borrowed,&error) && !borrowed.size && !borrowed.data[0]);
    qa_arena_destroy(&storage);
}
static void test_shared_random(void)
{
    const uint32_t expected[]={3499211612u,581869302u,3890346734u,3586334585u,545404204u};
    qa_mt19937 game,presentation;
    qa_mt19937_seed(&game,5489);
    qa_mt19937_seed(&presentation,1);
    for (size_t i=0;i<sizeof(expected)/sizeof(*expected);++i)
        CHECK(qa_mt19937_next(&game)==expected[i]);
    CHECK(game.draws==5 && presentation.draws==0);
    CHECK(qa_mt19937_next(&presentation)==1791095845u);
    CHECK(game.draws==5 && presentation.draws==1);
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
static void test_q2_donor_save_metadata(void)
{
    const char game[]="{\"stamp\":\"quake-2-ts:g_save:v1\",\"maxclients\":1,\"maxentities\":1024,\"clients\":[{}]}";
    const char level_game[]="{\"level\":{\"mapname\":\"base1\"},\"edicts\":[{\"index\":0,\"data\":{\"classname\":\"worldspawn\"}}]}";
    CHECK(qa_q2_game_original_builtin_game((qa_bytes){(const uint8_t *)game,sizeof(game)-1}));
    const char unknown[]="{\"stamp\":\"unsupported-source\",\"clients\":[]}";
    CHECK(!qa_q2_game_original_builtin_game((qa_bytes){(const uint8_t *)unknown,sizeof(unknown)-1}));
    const char rerelease[]=" \n{\"save_version\":1,\"game\":{\"maxclients\":1,\"maxentities\":8192,\"autosaved\":true},\"clients\":[{}]}";
    CHECK(qa_q2_game_original_builtin_game((qa_bytes){(const uint8_t *)rerelease,sizeof(rerelease)-1}));
    const char future[]="{\"save_version\":2,\"game\":{},\"clients\":[]}";
    CHECK(!qa_q2_game_original_builtin_game((qa_bytes){(const uint8_t *)future,sizeof(future)-1}));
    qa_error error={0};
    size_t configs=(size_t)QA_Q2_SAVE_CONFIGSTRINGS*QA_Q2_SAVE_CONFIGSTRING_BYTES;
    qa_buffer donor={.size=configs+QA_Q2_SAVE_AREA_PORTALS};
    donor.data=calloc(donor.size,1);CHECK(donor.data);
    memcpy(donor.data,"Donor world",12);
    donor.data[configs]=1;
    donor.data[donor.size-1]=2;
    qa_q2_save_level level={.name="base1"}, retail={0};
    CHECK(qa_q2_save_level_decode((qa_bytes){donor.data,donor.size},&level,&error));
    CHECK(!strcmp(qa_q2_save_configstring(&level,0),"Donor world"));
    CHECK(level.portal_open[0]==1 && level.portal_open[1]==0 && level.portal_open[QA_Q2_SAVE_AREA_PORTALS-1]==1);
    qa_buffer encoded={0};
    CHECK(qa_q2_save_level_encode(&level,&encoded,&error));
    CHECK(encoded.size==configs+QA_Q2_SAVE_AREA_PORTALS*4);
    CHECK(qa_q2_save_level_decode((qa_bytes){encoded.data,encoded.size},&retail,&error));
    CHECK(!strcmp(qa_q2_save_configstring(&retail,0),"Donor world"));
    for (size_t i=0;i<QA_Q2_SAVE_AREA_PORTALS;++i)
        CHECK(retail.portal_open[i]==level.portal_open[i]);
    qa_q2_save_level invalid={0};
    CHECK(!qa_q2_save_level_decode((qa_bytes){donor.data,donor.size-1},&invalid,&error));
    qa_q2_save_level_dispose(&retail);qa_buffer_free(&encoded);

    char directory[]="/tmp/qa-q2-donor-XXXXXX",slot[128];
    CHECK(mkdtemp(directory));
    CHECK(snprintf(slot,sizeof(slot),"%s/slot",directory)>0);
    qa_fs_root *root=NULL;
    CHECK(qa_fs_root_open(directory,&root,&error));
    CHECK(qa_fs_root_replace(root,"slot",(qa_bytes){(const uint8_t *)"previous shared save",20},1,&error));
    level.game=(qa_buffer){.data=(uint8_t *)strdup(level_game),.size=sizeof(level_game)-1};
    qa_q2_save_data save={.server={.comment="Donor",.map_command="base1"},
        .game={.data=(uint8_t *)strdup(game),.size=sizeof(game)-1},.levels=&level,.level_count=1};
    CHECK(level.game.data && save.game.data);
    CHECK(qa_q2_save_directory_write(root,"slot",&save,1,&error));
    qa_fs_entry_kind kind=QA_FS_MISSING;
    CHECK(qa_fs_root_status(root,"slot",&kind,NULL,&error) && kind==QA_FS_DIRECTORY);
    CHECK(qa_q2_save_directory_write(root,"slot",&save,2,&error));
    CHECK(qa_fs_root_replace(root,"slot/base1.sv2",(qa_bytes){donor.data,donor.size},2,&error));
    qa_q2_save_data *read=NULL;
    CHECK(qa_q2_save_directory_read(root,"slot",&read,&error));
    CHECK(read->game.size==sizeof(game)-1 && !memcmp(read->game.data,game,sizeof(game)-1));
    CHECK(qa_q2_game_original_builtin_game((qa_bytes){read->game.data,read->game.size}));
    CHECK(read->level_count==1 && read->levels[0].game.size==sizeof(level_game)-1 &&
        !memcmp(read->levels[0].game.data,level_game,sizeof(level_game)-1));
    for (size_t i=0;i<QA_Q2_SAVE_AREA_PORTALS;++i)
        CHECK(read->levels[0].portal_open[i]==level.portal_open[i]);
    qa_q2_save_destroy(read);
    CHECK(qa_fs_root_remove(root,"slot/server.ssv",&error));
    CHECK(qa_fs_root_remove(root,"slot/game.ssv",&error));
    CHECK(qa_fs_root_remove(root,"slot/base1.sav",&error));
    CHECK(qa_fs_root_remove(root,"slot/base1.sv2",&error));
    qa_fs_root_close(root);
    CHECK(rmdir(slot)==0 && rmdir(directory)==0);
    qa_buffer_free(&save.game);qa_q2_save_level_dispose(&level);qa_buffer_free(&donor);
}

static void test_json_caller_storage(void)
{
    static const struct { uint8_t bytes[4]; size_t size; } malformed[] = {
        {{0xc0,0x80},2}, {{0xe0,0x80,0x80},3}, {{0xed,0xa0,0x80},3},
        {{0xf4,0x90,0x80,0x80},4}, {{0xf0,0x9f},2}, {{0x80},1}};
    for (size_t i=0;i<sizeof(malformed)/sizeof(*malformed);++i) {
        qa_bytes bytes={malformed[i].bytes,malformed[i].size};
        size_t cursor=0;uint32_t scalar=123;qa_error failure={0};qa_buffer quote={0};
        CHECK(!qa_utf8_next_strict(bytes,&cursor,&scalar) && !cursor && scalar==123);
        CHECK(!qa_json_quote(bytes,&quote,&failure) && !quote.data);
        uint8_t json[6]={'"'};
        memcpy(json+1,bytes.data,bytes.size);json[bytes.size+1]='"';
        qa_json_document *invalid=NULL;
        CHECK(!qa_json_parse(QA_JSON_STRICT,(qa_bytes){json,bytes.size+2},&invalid,&failure) && !invalid);
    }
    const uint8_t valid[]={0xef,0xbf,0xbd,0xf4,0x8f,0xbf,0xbf};
    size_t cursor=0;uint32_t scalar=0;
    CHECK(qa_utf8_next_strict((qa_bytes){valid,sizeof(valid)},&cursor,&scalar) && scalar==0xfffd);
    CHECK(qa_utf8_next_strict((qa_bytes){valid,sizeof(valid)},&cursor,&scalar) && scalar==0x10ffff);
    const char source[]="\"A\\u0000B\\ud83d\\ude00\\n\"";
    const uint8_t expected[]={'A',0,'B',0xf0,0x9f,0x98,0x80,'\n',0};
    qa_json_document *document=NULL; qa_error error={0};
    CHECK(qa_json_parse(QA_JSON_STRICT,(qa_bytes){(const uint8_t *)source,sizeof(source)-1},&document,&error));
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

    const char *special[]={"NaN","Infinity","+Infinity","-Infinity"};
    for (size_t i=0;i<sizeof(special)/sizeof(*special);++i) {
        qa_bytes bytes={(const uint8_t *)special[i],strlen(special[i])};
        document=NULL;
        CHECK(!qa_json_parse(QA_JSON_STRICT,bytes,&document,&error) && !document);
        CHECK(qa_json_parse(QA_JSON_SPECIAL_FLOATS,bytes,&document,&error));
        double number=0;
        CHECK(qa_json_number(document,qa_json_root(document),&number,&error));
        CHECK(i==0 ? isnan(number) : isinf(number) && (signbit(number)!=0)==(i==3));
        int64_t signed_value=17; uint64_t unsigned_value=19;
        CHECK(!qa_json_i64(document,qa_json_root(document),&signed_value,&error) && signed_value==17);
        CHECK(!qa_json_u64(document,qa_json_root(document),&unsigned_value,&error) && unsigned_value==19);
        qa_json_destroy(document);
        qa_json_writer writer={0}; qa_buffer output={0};
        qa_json_writer_number(&writer,number);
        CHECK(!qa_json_writer_finish(&writer,&output,&error) && !output.data);
        qa_json_writer_destroy(&writer);
    }
    const char *invalid_special[]={"-NaN","+NaN","nan","inf","-inf","NaNx","Infinityx"};
    for (size_t i=0;i<sizeof(invalid_special)/sizeof(*invalid_special);++i) {
        qa_bytes bytes={(const uint8_t *)invalid_special[i],strlen(invalid_special[i])};
        document=NULL;
        CHECK(!qa_json_parse(QA_JSON_SPECIAL_FLOATS,bytes,&document,&error) && !document);
    }
    const char *overflow[]={"1e999","-1e999"};
    const qa_json_policy policies[]={QA_JSON_STRICT,QA_JSON_SPECIAL_FLOATS};
    for (size_t policy=0;policy<sizeof(policies)/sizeof(*policies);++policy)
        for (size_t i=0;i<sizeof(overflow)/sizeof(*overflow);++i) {
            qa_bytes bytes={(const uint8_t *)overflow[i],strlen(overflow[i])};
            document=NULL;
            CHECK(qa_json_parse(policies[policy],bytes,&document,&error));
            double number=23;
            CHECK(!qa_json_number(document,qa_json_root(document),&number,&error) && number==23);
            qa_json_destroy(document);
        }
    qa_json_writer writer={.special_floats=true}; qa_buffer output={0};
    const double numbers[]={NAN,INFINITY,-INFINITY,-0.0,3.5};
    qa_json_writer_object(&writer);
    qa_json_writer_key(&writer,"values"); qa_json_writer_array(&writer);
    for (size_t i=0;i<sizeof(numbers)/sizeof(*numbers);++i)
        qa_json_writer_number(&writer,numbers[i]);
    qa_json_writer_end(&writer);
    qa_json_writer_key(&writer,"integer"); qa_json_writer_u64(&writer,7);
    qa_json_writer_end(&writer);
    CHECK(qa_json_writer_finish(&writer,&output,&error));
    document=NULL;
    CHECK(!qa_json_parse(QA_JSON_STRICT,(qa_bytes){output.data,output.size},&document,&error) && !document);
    CHECK(qa_json_parse(QA_JSON_SPECIAL_FLOATS,(qa_bytes){output.data,output.size},&document,&error));
    qa_json_id root=qa_json_root(document), array=qa_json_get(document,root,"values");
    CHECK(qa_json_size(document,array)==sizeof(numbers)/sizeof(*numbers));
    for (size_t i=0;i<sizeof(numbers)/sizeof(*numbers);++i) {
        qa_json_id id=qa_json_at(document,array,i); double number=0;
        CHECK(qa_json_number(document,id,&number,&error));
        CHECK(isnan(numbers[i]) ? isnan(number) : number==numbers[i] && (signbit(number)!=0)==(signbit(numbers[i])!=0));
        if (i<3) {
            const char *literal=i==0 ? "NaN" : i==1 ? "Infinity" : "-Infinity";
            qa_bytes token=qa_json_source(document,id);
            CHECK(token.size==strlen(literal) && !memcmp(token.data,literal,token.size));
        }
    }
    uint64_t integer=0;
    CHECK(qa_json_u64(document,qa_json_get(document,root,"integer"),&integer,&error) && integer==7);
    qa_json_destroy(document); qa_buffer_free(&output); qa_json_writer_destroy(&writer);
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

static void test_ordered_lightstyles(void)
{
    qa_lightstyles first = {.count = 256}, second = {.count = 256};
    qa_lightstyles merged = {.count = 256};
    qa_lightstyles_set(&first, 4, 19);
    uint64_t revision = first.revision;
    qa_lightstyles_set(&first, 4, 19);
    CHECK(first.revision == revision && !qa_lightstyles_ordered_present(&first, 4));
    qa_lightstyles_set_ordered(&first, 4, 19, 3);
    CHECK(first.revision == revision + 1);
    qa_lightstyles_set_ordered(&first, 4, 19, 3);
    CHECK(first.revision == revision + 1);
    qa_lightstyles_set_ordered(&second, 4, 21, 2);
    qa_lightstyles_set_ordered(&second, 255, QA_STRING_NONE, 0);
    qa_lightstyles_merge_ordered(&merged, &first);
    qa_lightstyles_merge_ordered(&merged, &second);
    CHECK(merged.patterns[4] == 19 && merged.sequences[4] == 3);
    CHECK(qa_lightstyles_ordered_present(&merged, 255));
    CHECK(!qa_lightstyles_ordered_present(&merged, 5));
    qa_lightstyles_set_ordered(&second, 4, QA_STRING_NONE, 3);
    qa_lightstyles_merge_ordered(&merged, &second);
    CHECK(merged.patterns[4] == QA_STRING_NONE && merged.sequences[4] == 3);
    qa_lightstyles_set_ordered(&second, 4, 22, 1);
    CHECK(second.patterns[4] == 22 && second.sequences[4] == 1);
    second = (qa_lightstyles){.count = 256};
    CHECK(!qa_lightstyles_ordered_present(&second, 4));
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
    qa_scene_resources *resources=qa_scene_resources_create(NULL,&error);
    CHECK(resources);
    const uint8_t pixel[]={10,20,30,255};
    qa_scene_image_level level={1,1,pixel,sizeof pixel};
    qa_scene_image *image=NULL;
    CHECK(qa_scene_image_create(resources,"shared-frame-image",QA_SCENE_RGBA8,&level,1,
        QA_SCENE_CLAMP,QA_SCENE_NEAREST,(qa_vec4){0},&image,&error));
    qa_scene_frame peer;
    CHECK(qa_scene_frame_init(&frame,19,1024*1024,&error));
    CHECK(qa_scene_frame_init(&peer,20,1024*1024,&error));
    for (unsigned pass=0;pass<2;++pass) {
        qa_scene_frame_reset(&frame,pass+1);
        qa_scene_frame_reset(&peer,pass+1);
        CHECK(qa_scene_frame_image(&frame,image,&error));
        CHECK(qa_scene_frame_image(&peer,image,&error));
        CHECK(qa_scene_frame_image(&frame,image,&error));
        CHECK(frame.image_count==1 && peer.image_count==1);
        qa_scene_frame_reset(&peer,pass+2);
        CHECK(qa_scene_frame_image(&peer,image,&error));
        CHECK(qa_scene_frame_image(&frame,image,&error));
        CHECK(frame.image_count==1 && peer.image_count==1);
    }
    qa_scene_image_release(image);
    qa_scene_frame_destroy(&peer);
    CHECK(frame.images[0]->levels[0].bytes==sizeof pixel &&
        !memcmp(frame.images[0]->levels[0].pixels,pixel,sizeof pixel));
    qa_scene_frame_destroy(&frame);
    qa_scene_resources_destroy(resources);
}

typedef struct operation_pool_request {
    max_align_t alignment;
    unsigned depth;
} operation_pool_request;
typedef struct operation_pool_check {
    qa_operation *operation;
    qa_operation_next saved;
    unsigned calls;
    bool repeat;
} operation_pool_check;
static bool operation_pool_canonical(void *context,const void *request,void *result,qa_error *error)
{
    (void)error;
    operation_pool_check *check=context;
    CHECK((uintptr_t)request%_Alignof(max_align_t)==0);
    ++check->calls;
    *(unsigned *)result=((const operation_pool_request *)request)->depth;
    return true;
}
static bool operation_pool_replace(void *context,const void *request,qa_operation_next next,
    void *result,qa_error *error)
{
    operation_pool_check *check=context;
    const operation_pool_request *outer=request;
    unsigned depth=outer->depth;
    if (depth<8) {
        operation_pool_request nested={0}; nested.depth=depth+1;
        unsigned value;
        CHECK(qa_operation_dispatch(check->operation,&nested,&value,operation_pool_canonical,
            check,NULL,NULL,error));
        CHECK(value==depth+1 && outer->depth==depth);
    }
    CHECK(qa_operation_continue(next,request,result,error));
    if (!depth) {
        check->saved=next;
        if (check->repeat) CHECK(!qa_operation_continue(next,request,result,error));
    }
    return true;
}
static void test_operation_pool(void)
{
    qa_error error={0}; operation_pool_check check={0};
    CHECK(qa_operation_create(sizeof(operation_pool_request),sizeof(unsigned),&check.operation,&error));
    qa_operation_hook hook={.owner=1,.name=1,.kind=QA_OPERATION_REPLACE,
        .context=&check,.call.replace=operation_pool_replace};
    qa_operation_registration registration;
    CHECK(qa_operation_register(check.operation,&hook,&registration,&error));
    operation_pool_request request={0}; unsigned value;
    CHECK(qa_operation_dispatch(check.operation,&request,&value,operation_pool_canonical,
        &check,NULL,NULL,&error));
    CHECK(value==0 && check.calls==9);
    CHECK(!qa_operation_continue(check.saved,&request,&value,&error));
    check.repeat=true;
    CHECK(!qa_operation_dispatch(check.operation,&request,&value,operation_pool_canonical,
        &check,NULL,NULL,&error));
    CHECK(check.calls==18);
    CHECK(qa_operation_unregister(check.operation,registration));
    CHECK(qa_operation_destroy(check.operation,&error));
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

    qa_arena_init_pool(&left,&pages);
    qa_pool records={0};
    CHECK(qa_pool_prepare(&records,&left,2,32,16,&error));
    size_t record_slots[6];uint8_t *record_values[6];
    for(size_t i=0;i<2;++i){
        record_values[i]=qa_pool_take(&records,record_slots+i);
        CHECK(record_values[i] && record_slots[i]==i);
        memset(record_values[i],(int)(0x40+i),records.stride);
    }
    CHECK(qa_pool_extend(&records,&left,2,&error));
    record_values[2]=qa_pool_take(&records,record_slots+2);
    CHECK(record_values[2] && record_slots[2]==2);
    record_values[2][0]=0x42;
    qa_pool_release(&records,record_slots[0]);
    CHECK(qa_pool_take(&records,record_slots)==record_values[0] && record_slots[0]==0);
    CHECK(qa_pool_extend(&records,&left,2,&error));
    for(size_t i=3;i<6;++i){
        record_values[i]=qa_pool_take(&records,record_slots+i);
        CHECK(record_values[i] && record_values[i]==qa_pool_at(&records,record_slots[i]));
        record_values[i][0]=(uint8_t)(0x40+i);
    }
    CHECK(record_slots[3]==4 && record_slots[4]==5 && record_slots[5]==3);
    CHECK(records.capacity==6 && records.active==6);
    for(size_t i=0;i<6;++i)CHECK(record_values[i][0]==0x40+i);
    size_t retained_head=records.head;
    CHECK(!qa_pool_extend(&records,&left,4096,&error));
    CHECK(records.head==retained_head && records.capacity==6 && records.active==6);
    for(size_t i=0;i<6;++i)CHECK(record_values[i][0]==0x40+i);
    for(size_t i=0;i<6;++i)qa_pool_release(&records,record_slots[i]);
    CHECK(records.active==0);
    qa_pool_reset(&records);
    retained_head=records.head;
    size_t retained_peak=records.peak,retained_overflow=records.overflow;
    CHECK(!qa_pool_run_available(&records,0));
    CHECK(qa_pool_run_available(&records,1) && qa_pool_run_available(&records,2));
    CHECK(!qa_pool_run_available(&records,3));
    CHECK(records.head==retained_head && records.active==0 &&
        records.peak==retained_peak && records.overflow==retained_overflow);
    CHECK(!qa_pool_take_run(&records,3,&slot));
    CHECK(records.active==0);
    CHECK(qa_pool_take_run(&records,2,&slot)==record_values[0] && slot==0);
    CHECK(qa_pool_run_available(&records,2) && !qa_pool_run_available(&records,3));
    CHECK(!qa_pool_grow_run(&records,&slot,2,3) && slot==0);
    CHECK(records.active==2 && record_values[1][0]==0x41);
    qa_pool_release_run(&records,slot,2);
    qa_pool_reset(&records);
    for(size_t i=0;i<records.capacity;++i)
        CHECK(qa_pool_take(&records,&slot)==qa_pool_at(&records,i) && slot==i);
    qa_arena_destroy(&left);
    CHECK(pages.active==0);
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
static void test_render_resource_identity(void)
{
    qa_error error={0};
    qa_render_resource_index index={0};
    int values[7]={0},owners[2]={0};
    const struct {uint64_t identity,revision;const void *owner;} keys[]={
        {3,7,NULL},{UINT64_C(0x100000003),7,NULL},
        {3,UINT64_C(0x10000000007),NULL},{3,7,&owners[0]},
        {0,7,&owners[0]},{0,7,&owners[1]}
    };
    for(size_t i=0;i<sizeof(keys)/sizeof(*keys);++i) {
        CHECK(render_resource_reserve(&index,index.count+1,&error));
        render_resource_put(&index,keys[i].identity,keys[i].revision,keys[i].owner,values+i);
        for(size_t j=0;j<=i;++j)
            CHECK(render_resource_get(&index,keys[j].identity,keys[j].revision,keys[j].owner)==values+j);
    }
    render_resource_put(&index,3,7,NULL,values+6);
    CHECK(index.count==6 && render_resource_get(&index,3,7,NULL)==values+6);
    render_resource_remove(&index,3,7,NULL);
    CHECK(index.count==5 && !render_resource_get(&index,3,7,NULL));
    CHECK(render_resource_get(&index,3,UINT64_C(0x10000000007),NULL)==values+2);
    CHECK(render_resource_get(&index,0,7,&owners[0])==values+4);
    size_t capacity=index.slots.capacity;
    render_resource_clear(&index);
    CHECK(!index.count && index.slots.capacity==capacity);
    CHECK(!render_resource_get(&index,0,7,&owners[0]));
    CHECK(render_resource_reserve(&index,1,&error));
    render_resource_put(&index,0,7,&owners[1],values+5);
    CHECK(render_resource_get(&index,0,7,&owners[1])==values+5);
    render_resource_destroy(&index);
}

static void test_literal_tokens(void)
{
    qa_error error={0}; qa_command_tokens owned={0},pooled={0};
    qa_arena literal_storage={0}; qa_arena_init(&literal_storage,0);
    char first[]="print",second[]="quoted; text";
    char *values[]={first,second};
    qa_command_tokens literal={.count=2,.values=values,.args_text="quoted; text"};
    CHECK(qa_command_tokens_copy(&literal,&owned,false,qa_arena_alloc_callback,&literal_storage,&error));
    qa_unified_frame_pool *pool=qa_unified_frame_pool_create(128*1024,2,&error); CHECK(pool);
    qa_unified_frame_lease *lease=qa_unified_frame_lease_acquire(pool,&error); CHECK(lease);
    CHECK(qa_command_tokens_copy(&literal,&pooled,false,tokens_frame_allocate,lease,&error));
    qa_command_tokens joined={0},preserved={0};
    literal.args_text="";
    CHECK(qa_command_tokens_copy(&literal,&preserved,false,tokens_frame_allocate,lease,&error));
    CHECK(qa_command_tokens_copy(&literal,&joined,true,tokens_frame_allocate,lease,&error));
    CHECK(!*preserved.args_text && !strcmp(joined.args_text,"quoted; text"));
    CHECK(joined.count==2 && !strcmp(joined.values[1],"quoted; text"));
    qa_command_tokens_free(&joined);qa_command_tokens_free(&preserved);
    literal.args_text="quoted; text";
    for (qa_ruleset_id dialect=QA_RULESET_NETQUAKE;dialect<=QA_RULESET_Q3;++dialect) {
        qa_arena token_storage={0}; qa_arena_init(&token_storage,0);
        qa_command_tokens arena={0},frame={0},span={0};
        CHECK(qa_command_tokenize("say \"hello world\" next",dialect,false,&arena,
            qa_arena_alloc_callback,&token_storage,&error));
        CHECK(qa_command_tokenize("say \"hello world\" next",dialect,false,&frame,tokens_frame_allocate,lease,&error));
        CHECK(qa_command_tokenize_span("say \"hello world\" next;ignored",strlen("say \"hello world\" next"),
            dialect,false,&span,tokens_frame_allocate,lease,&error));
        CHECK(arena.count==3 && frame.count==3 && frame.borrowed);
        CHECK(span.count==arena.count && span.borrowed);
        for (size_t i=0;i<arena.count;++i) CHECK(!strcmp(arena.values[i],frame.values[i]));
        for (size_t i=0;i<arena.count;++i) CHECK(!strcmp(arena.values[i],span.values[i]));
        CHECK(!strcmp(frame.values[1],"hello world"));
        CHECK(!strcmp(frame.args_text,dialect==QA_RULESET_Q3?"hello world next":"\"hello world\" next"));
        CHECK(!strcmp(frame.args_text,arena.args_text));
        CHECK(!strcmp(span.args_text,arena.args_text));
        qa_command_tokens_free(&arena); qa_command_tokens_free(&frame); qa_command_tokens_free(&span);
        qa_arena_destroy(&token_storage);
    }
    const struct {
        const char *text;
        size_t count;
        const char *tokens[4];
    } q3_cases[] = {
        {"alpha/* block */\"two words\" tail // ignored", 3, {"alpha", "two words", "tail"}},
        {"\"\" a\"b\"c", 4, {"", "a", "b", "c"}},
        {"del\177 next\200\"quoted\377\"", 3, {"del\177", "next", "quoted\377"}},
        {"\"unterminated", 1, {"unterminated"}}
    };
    qa_q3_tokens wire;
    for (size_t i = 0; i < sizeof(q3_cases) / sizeof(q3_cases[0]); ++i) {
        qa_arena token_storage = {0};
        qa_command_tokens parsed = {0};
        qa_arena_init(&token_storage, 0);
        CHECK(qa_q3_tokenize(q3_cases[i].text, &wire, &error));
        CHECK(qa_command_tokenize(q3_cases[i].text, QA_RULESET_Q3, false, &parsed,
            qa_arena_alloc_callback, &token_storage, &error));
        CHECK(wire.count == q3_cases[i].count && parsed.count == q3_cases[i].count);
        CHECK(!wire.truncated);
        for (size_t token = 0; token < wire.count; ++token) {
            CHECK(!strcmp(qa_q3_token(&wire, token), q3_cases[i].tokens[token]));
            CHECK(!strcmp(parsed.values[token], q3_cases[i].tokens[token]));
        }
        qa_command_tokens_free(&parsed);
        qa_arena_destroy(&token_storage);
    }
    char many[2051];
    for (size_t i = 0; i < 1025; ++i) { many[i * 2] = 'x'; many[i * 2 + 1] = ' '; }
    many[2050] = 0;
    CHECK(qa_q3_tokenize(many, &wire, &error) && wire.count == 1024 && wire.truncated);
    many[2048] = 0;
    CHECK(qa_q3_tokenize(many, &wire, &error) && wire.count == 1024 && !wire.truncated);
    char oversized[sizeof(wire.text) + 1];
    memset(oversized, 'x', sizeof(oversized) - 1); oversized[sizeof(oversized) - 1] = 0;
    CHECK(!qa_q3_tokenize(oversized, &wire, &error));
    CHECK(wire.count == 1 && wire.offsets[0] == 0 && !wire.truncated);
    CHECK(!memcmp(wire.text, oversized, sizeof(wire.text) - 1));
    second[0]='X'; qa_unified_frame_pool_destroy(&pool);
    CHECK(owned.count==2 && pooled.count==2);
    CHECK(!strcmp(owned.values[1],"quoted; text") && !strcmp(pooled.values[1],owned.values[1]));
    CHECK(!strcmp(pooled.args_text,"quoted; text"));
    char *held=pooled.values[1];
    qa_command_tokens_free(&pooled); CHECK(!strcmp(held,"quoted; text"));
    qa_unified_frame_lease_release(lease); qa_command_tokens_free(&owned);
    qa_arena_destroy(&literal_storage);
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
static void test_lightstyle_snapshot(void)
{
    qa_error error={0}; uint16_t style=700;
    qaw_legacy face={.styles=&style,.style_count=1};
    qaw_surface surface={.legacy=&face}; qawl_world retained={0};
    qa_scene_world world={.bsp={.family=QA_BSP_Q1},.legacy_data=&retained,
        .surfaces=&surface,.surface_count=1};
    CHECK(qawl_light_styles_prepare(&world,&error));
    float values[702]; for(size_t i=0;i<702;++i) values[i]=(float)i;
    qa_scene_world_input input={.q1_styles=values,.style_count=702};
    CHECK(qawl_light_styles(&world,&input,&error));
    float *storage=retained.q1_styles;
    CHECK(retained.style_count==701 && storage[700]==700);
    values[701]=INFINITY; values[700]=42;
    CHECK(!qawl_light_styles(&world,&input,&error));
    CHECK(retained.style_count==701 && storage[700]==700);
    input.style_count=1;
    CHECK(qawl_light_styles(&world,&input,&error) && retained.style_count==1);
    input.style_count=701;
    CHECK(qawl_light_styles(&world,&input,&error));
    CHECK(retained.q1_styles==storage && storage[700]==42);
    input.q1_styles=NULL;
    CHECK(qawl_light_styles(&world,&input,&error) && retained.style_count==0);
    free(retained.q1_styles);

    qa_bsp_lightgrid_sample samples[2]={{.style=201},{.style=UINT8_MAX}};
    retained=(qawl_world){0}; world=(qa_scene_world){.bsp={.family=QA_BSP_Q2},
        .legacy_data=&retained,.lightgrid={.samples=samples,.sample_count=2}};
    CHECK(qawl_light_styles_prepare(&world,&error));
    qa_vec3 colors[203]; for(size_t i=0;i<203;++i) colors[i]=qa_v3(1,2,3);
    input=(qa_scene_world_input){.q2_styles=colors,.style_count=203};
    CHECK(qawl_light_styles(&world,&input,&error));
    CHECK(retained.style_count==202 && qa_vec_equal(retained.q2_styles[201],qa_v3(1,2,3)));
    colors[202].z=NAN;
    CHECK(!qawl_light_styles(&world,&input,&error) && retained.style_count==202);
    free(retained.q2_styles);
}

static void test_shared_trajectory(void)
{
    qa_error error={0}; qa_vec3 position={0},velocity={0};
    qa_trajectory t={.type=QA_TRAJECTORY_LINEAR,.delta={1,0,0}};
    CHECK(qa_trajectory_position(&t,999,800,&position,&error));
    CHECK(position.x==0.999f);
    t=(qa_trajectory){.type=QA_TRAJECTORY_GRAVITY,.base={0,0,10},.delta={0,0,5}};
    CHECK(qa_trajectory_position(&t,1000,800,&position,&error));
    CHECK(qa_trajectory_velocity(&t,1000,800,&velocity,&error));
    CHECK(position.z==-385 && velocity.z==-795);
    t=(qa_trajectory){.type=QA_TRAJECTORY_LINEAR_STOP,.duration_ms=1000,.delta={2,0,0}};
    CHECK(qa_trajectory_position(&t,-1,800,&position,&error) && position.x==0);
    CHECK(qa_trajectory_position(&t,1001,800,&position,&error) && position.x==2);
    CHECK(qa_trajectory_velocity(&t,1000,800,&velocity,&error) && velocity.x==2);
    CHECK(qa_trajectory_velocity(&t,1001,800,&velocity,&error) && velocity.x==0);
    t.time_ms=INT32_MAX-10; t.duration_ms=20;
    CHECK(qa_trajectory_velocity(&t,INT32_MIN+9,800,&velocity,&error) && velocity.x==2);
    CHECK(qa_trajectory_velocity(&t,INT32_MIN+10,800,&velocity,&error) && velocity.x==0);
    t=(qa_trajectory){.type=QA_TRAJECTORY_SINE,.duration_ms=2000,.delta={6,0,0}};
    CHECK(qa_trajectory_velocity(&t,0,800,&velocity,&error) && velocity.x==3);
    t.duration_ms=0;
    CHECK(qa_trajectory_position(&t,0,800,&position,&error) && isnan(position.x));
    t.type=(qa_trajectory_type)99; position=qa_v3(1,2,3);
    CHECK(!qa_trajectory_position(&t,0,800,&position,&error));
    CHECK(error.code==QA_ERROR_FORMAT && qa_vec_equal(position,qa_v3(1,2,3)));
}

static void test_bounded_handshake(void)
{
    qa_error error={0};uint8_t storage[512];
    for (qa_unified_handshake_kind kind=QA_UNIFIED_HELLO;kind<=QA_UNIFIED_CONNECT;++kind) {
        qa_unified_handshake value={.kind=kind},decoded={0};
        memset(value.nonce.bytes,0x31,sizeof value.nonce.bytes);
        memset(value.token.bytes,0x72,sizeof value.token.bytes);
        qa_bytes wire={0};
        CHECK(qa_unified_handshake_write(&value,storage,sizeof storage,&wire,&error));
        CHECK(wire.data==storage && wire.size<=sizeof storage);
        qa_unified_document *document=NULL;qa_buffer owned={0};
        CHECK(qa_unified_document_create_handshake(&value,&document,&error));
        CHECK(qa_unified_document_encode(document,&owned,&error));
        CHECK(wire.size==owned.size && !memcmp(wire.data,owned.data,wire.size));
        CHECK(qa_unified_handshake_read(wire,&decoded,&error));
        CHECK(decoded.kind==kind && !memcmp(decoded.nonce.bytes,value.nonce.bytes,sizeof value.nonce.bytes));
        qa_unified_token token=kind==QA_UNIFIED_HELLO?(qa_unified_token){0}:value.token;
        CHECK(!memcmp(decoded.token.bytes,token.bytes,sizeof token.bytes));
        qa_unified_handshake retained=decoded;
        storage[wire.size]=0;
        CHECK(!qa_unified_handshake_read((qa_bytes){storage,wire.size+1},&decoded,&error));
        CHECK(!memcmp(&decoded,&retained,sizeof decoded));
        qa_bytes previous=wire;
        CHECK(!qa_unified_handshake_write(&value,storage,4,&wire,&error));
        CHECK(wire.data==previous.data && wire.size==previous.size);
        qa_buffer_free(&owned);qa_unified_document_destroy(document);
    }
    qa_unified_handshake untouched={.kind=QA_UNIFIED_CONNECT,.nonce={{1}}},decoded=untouched;
    const uint8_t unknown[]={'Q','U','H','S',1,3};
    CHECK(!qa_unified_handshake_read((qa_bytes){unknown,sizeof unknown},&decoded,&error));
    CHECK(!memcmp(&decoded,&untouched,sizeof decoded));
    CHECK(!qa_unified_handshake_read((qa_bytes){storage,sizeof storage+1},&decoded,&error));
    CHECK(!memcmp(&decoded,&untouched,sizeof decoded));
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

static void test_metadata_names(void)
{
    qa_error error={0};qa_strings *source_names=NULL,*receiver_names=NULL;
    qa_string_id provider,content,instance,image,padding;
    CHECK(qa_strings_create(&source_names,&error));
    CHECK(qa_strings_create(&receiver_names,&error));
    CHECK(qa_strings_intern_cstr(source_names,"source:one",&provider,&error));
    CHECK(qa_strings_intern_cstr(source_names,"q1-classic-id1",&content,&error));
    CHECK(qa_strings_intern_cstr(source_names,"client:one",&instance,&error));
    CHECK(qa_strings_intern_cstr(source_names,"pics/weapon.pcx",&image,&error));
    CHECK(qa_strings_intern_cstr(receiver_names,"receiver-only",&padding,&error));
    qa_unified_frame_metadata *value=calloc(1,sizeof(*value));CHECK(value);
    *value=(qa_unified_frame_metadata){.strings=source_names,.epoch=1,.configuration_revision=1,
        .roster_revision=1,.replace_configurations=true,.replace_styles=true,.replace_q3=true,.replace_q1=true,
        .configuration_count=1,.q3_configuration_count=1};
    value->configurations=calloc(1,sizeof(*value->configurations));CHECK(value->configurations);
    qa_unified_configuration_state *configuration=value->configurations;
    qa_unified_provider_state selected={.provider=provider,.content=content};
    *configuration=(qa_unified_configuration_state){.actor={.registry=77,.slot=3,.generation=1},
        .movement=selected,.character=selected,.appearance=selected,.inventory=selected,.hud=selected,.weapon_count=1};
    configuration->weapons=calloc(1,sizeof(*configuration->weapons));CHECK(configuration->weapons);
    *configuration->weapons=selected;
    configuration->q2_hud=calloc(1,sizeof(*configuration->q2_hud));CHECK(configuration->q2_hud);
    configuration->q2_hud->protocol=qa_q2_default_protocol(false);
    configuration->q2_hud->data_provider=provider;
    configuration->q2_hud->configstring_count=1;
    configuration->q2_hud->configstrings=calloc(1,sizeof(*configuration->q2_hud->configstrings));
    CHECK(configuration->q2_hud->configstrings);
    configuration->q2_hud->configstrings[0]=(qa_q2_config_name){.index=32,.value_id=image};
    value->q3_configurations=calloc(1,sizeof(*value->q3_configurations));CHECK(value->q3_configurations);
    *value->q3_configurations=(qa_unified_q3_configuration){.provider_name=provider,.instance=instance,.content=content};
    value->q3_configurations->game_state=calloc(1,sizeof(*value->q3_configurations->game_state));
    CHECK(value->q3_configurations->game_state);
    value->q3_configurations->game_state->string_bytes=1;
    qa_unified_document *source=NULL,*decoded=NULL,*update=NULL,*merged=NULL;
    CHECK(qa_unified_document_create_metadata(&value,NULL,&source,&error));
    qa_strings_destroy(source_names);source_names=NULL;
    qa_buffer wire={0};CHECK(qa_unified_document_encode(source,&wire,&error));
    qa_unified_frame_pool *pool=qa_unified_frame_pool_create(4*1024*1024,8,&error);CHECK(pool);
    qa_unified_frame_lease *lease=qa_unified_frame_lease_acquire(pool,&error);CHECK(lease);
    CHECK(qa_unified_document_decode(QA_UNIFIED_CONTROL_DOCUMENT,(qa_bytes){wire.data,wire.size},
        receiver_names,lease,NULL,&decoded,&error));
    qa_unified_frame_lease_release(lease);qa_buffer_free(&wire);
    CHECK(qa_unified_document_equal(source,decoded));
    const qa_unified_frame_metadata *read=qa_unified_document_metadata(decoded);
    CHECK(read->strings==receiver_names && read->configurations[0].movement.provider!=provider);
    CHECK(!strcmp(qa_strings_cstr(read->strings,read->configurations[0].movement.provider),"source:one"));
    CHECK(!strcmp(qa_strings_cstr(read->strings,read->configurations[0].inventory.content),"q1-classic-id1"));
    CHECK(!strcmp(qa_strings_cstr(read->strings,read->configurations[0].q2_hud->configstrings[0].value_id),"pics/weapon.pcx"));
    CHECK(!strcmp(qa_strings_cstr(read->strings,read->q3_configurations[0].instance),"client:one"));
    value=calloc(1,sizeof(*value));CHECK(value);
    *value=(qa_unified_frame_metadata){.strings=receiver_names,.epoch=1,.frame=1,
        .configuration_revision=1,.roster_revision=1,.style_revision=1,.replace_styles=true,.style_count=1};
    value->styles=calloc(1,sizeof(*value->styles));CHECK(value->styles);
    value->styles[0]=(qa_unified_style_pattern){.family=QA_GAME_Q1,.pattern=strdup("m")};
    CHECK(value->styles[0].pattern);
    CHECK(qa_unified_document_create_metadata(&value,NULL,&update,&error));
    CHECK(qa_unified_metadata_apply(decoded,update,&merged,&error));
    qa_strings_destroy(receiver_names);receiver_names=NULL;
    qa_unified_document_destroy(source);qa_unified_document_destroy(decoded);qa_unified_document_destroy(update);
    CHECK(qa_unified_document_encode(merged,&wire,&error));
    qa_strings *fresh=NULL;CHECK(qa_strings_create(&fresh,&error));
    CHECK(qa_unified_document_decode(QA_UNIFIED_CONTROL_DOCUMENT,(qa_bytes){wire.data,wire.size},
        fresh,NULL,NULL,&decoded,&error));
    qa_strings_destroy(fresh);
    CHECK(qa_unified_document_equal(merged,decoded));
    CHECK(!strcmp(qa_strings_cstr(qa_unified_document_metadata(decoded)->strings,
        qa_unified_document_metadata(decoded)->configurations[0].appearance.provider),"source:one"));
    qa_buffer_free(&wire);qa_unified_document_destroy(merged);qa_unified_document_destroy(decoded);
    qa_unified_frame_pool_destroy(&pool);
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

static void test_shared_text_interpolation(void)
{
    qa_error error={0}; qa_arena scratch={0}; char out[128];
    qa_arena_init(&scratch,4096);
    const char *introduced[]={"{","1}"};
    CHECK(qa_q1_classic_text("{0}{1}",introduced,2,&scratch,out,sizeof(out),&error));
    CHECK(!strcmp(out,"1}"));
    CHECK(qa_q1_classic_text("{8}x",NULL,0,&scratch,out,sizeof(out),&error));
    CHECK(!strcmp(out,"x"));
    const char *finale[]={"$qc_finale_end"};
    CHECK(qa_q1_classic_text("{0}",finale,1,&scratch,out,8,&error));
    CHECK(!memcmp(out,qa_q1_finale_text(false,finale[0]),7) && !out[7]);
    const char *utf8[]={"\xe2\x82\xac"};
    for(size_t n=1;n<=4;++n) {
        CHECK(qa_q1_classic_text("{0}x",utf8,1,&scratch,out,n,&error));
        CHECK(strlen(out)==(n==4?3:0));
    }
    const char text[]="FORMAT = \"{0}x{1}\"\n";
    qa_bytes layer={(const uint8_t *)text,sizeof(text)-1};
    qa_localization_options options={.profile=QA_LOCALIZATION_Q2_RERELEASE};
    qa_localization *catalog=NULL;
    CHECK(qa_localization_create(&layer,1,&options,&catalog,&error));
    const char *values[]={"\xe2\x82\xac","z"};
    CHECK(qa_localize(catalog,"$FORMAT",values,2,false,false,out,3)==2);
    CHECK(!strcmp(out,"xz"));
    CHECK(qa_localize(catalog,"$FORMAT",values,2,false,true,out,3)==2);
    CHECK((unsigned char)out[0]==0xe2 && (unsigned char)out[1]==0x82 && !out[2]);
    qa_buffer complete={0};
    CHECK(qa_localize_presentation_arena(catalog,"$FORMAT",values,2,false,&scratch,&complete,&error));
    CHECK(complete.size==5 && !strcmp((const char *)complete.data,"\xe2\x82\xacxz"));
    char long_argument[4097]; memset(long_argument,'a',sizeof(long_argument)-1);
    long_argument[sizeof(long_argument)-1]=0;
    const char *long_values[]={long_argument,"$MISSING"};
    CHECK(qa_localize_presentation_arena(catalog,"$FORMAT",long_values,2,false,&scratch,&complete,&error));
    CHECK(complete.size==4105 && !memcmp(complete.data,long_argument,4096));
    CHECK(!strcmp((const char *)complete.data+4096,"x$MISSING"));
    char long_format[2051]; memset(long_format,'b',2047); memcpy(long_format+2047,"{0}",4);
    CHECK(qa_localize_presentation_arena(catalog,long_format,values,2,true,&scratch,&complete,&error));
    CHECK(complete.size==2050 && !strcmp((const char *)complete.data+2047,values[0]));
    qa_builtin_message_arg retained_args[]={{.kind=QA_BUILTIN_MESSAGE_TEXT,
        .value.literal=long_argument}};
    qa_unified_presentation_payload message={.kind=QA_UNIFIED_PRESENTATION_BUILTIN,
        .value.builtin={.kind=QA_BUILTIN_MESSAGE,.family=QA_GAME_Q2,
            .message_format=long_format,.arguments=retained_args,.argument_count=1,.flags=16}};
    qa_unified_presentation_payload decoded={0}; qa_buffer wire={0};
    CHECK(qa_unified_record_delta_encode(&qa_unified_presentation_payload_layout,
        &message,NULL,65536,&wire,NULL,&error));
    CHECK(qa_unified_record_delta_decode(&qa_unified_presentation_payload_layout,
        (qa_bytes){wire.data,wire.size},NULL,&decoded,NULL,NULL,NULL,NULL,NULL,&error));
    CHECK(decoded.value.builtin.flags==16 && decoded.value.builtin.argument_count==1);
    CHECK(!strcmp(decoded.value.builtin.message_format,long_format));
    CHECK(decoded.value.builtin.arguments[0].kind==QA_BUILTIN_MESSAGE_TEXT);
    long_argument[0]='z'; long_format[0]='c';
    CHECK(decoded.value.builtin.message_format[0]=='b');
    CHECK(decoded.value.builtin.arguments[0].value.literal[0]=='a');
    qa_unified_record_dispose(&qa_unified_presentation_payload_layout,&decoded);
    qa_buffer_free(&wire);
    qa_localization_release(catalog); qa_arena_destroy(&scratch);
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

typedef struct movement_body_test {
    qa_bounds watched;
    qa_vec3 first_probe;
    size_t probes, horizontal_traces;
    bool blocked, start_solid;
} movement_body_test;

static bool movement_body_trace(void *context, const qa_trace_query *query,
    qa_trace_result *out, qa_error *error)
{
    (void)error;
    movement_body_test *test = context;
    *out = (qa_trace_result){.family = QA_GAME_Q2, .fraction = 1, .end = query->end};
    if (qa_bounds_equal(query->shape.bounds, test->watched)) {
        if (qa_vec_equal(query->start, query->end)) {
            if (!test->probes) test->first_probe = query->start;
            ++test->probes;
        } else if (query->start.x != query->end.x || query->start.y != query->end.y) {
            ++test->horizontal_traces;
        }
        out->start_solid = test->start_solid || test->blocked;
        if (test->blocked) {
            out->all_solid = true;
            out->fraction = 0;
            out->end = query->start;
        }
    }
    return true;
}

static bool movement_body_contents(void *context, const qa_point_query *query,
    qa_point_contents *out, qa_error *error)
{
    (void)context; (void)query; (void)error;
    *out = (qa_point_contents){.family = QA_GAME_Q2};
    return true;
}

static void test_q2_requested_body(void)
{
    const qa_bounds requested = { {-7, -9, -20}, {8, 10, 12} };
    const qa_bounds larger = { {-32, -33, -40}, {34, 35, 64} };
    const qa_vec3 origin = {40, 60, 80};
    for (qa_ruleset_id kind = QA_RULESET_Q2_CLASSIC; kind <= QA_RULESET_Q2_RERELEASE; ++kind) {
        qa_error error = {0};
        qa_movement_input input = qa_movement_input_default(kind, (qa_actor_id){0});
        CHECK(qa_movement_set_origin(&input.state, origin, &error));
        if (kind == QA_RULESET_Q2_CLASSIC) input.state.data.q2.gravity = 0;
        else input.state.data.q2r.gravity = 0;
        input.command.milliseconds = 50;
        input.command.forward_move = 200;
        input.environment.has_body_bounds = true;
        input.environment.body_bounds = requested;
        qa_bounds reference = input.shape.bounds;
        movement_body_test test = {.watched = requested};
        qa_movement_services services = {.context = &test,
            .trace = movement_body_trace, .point_contents = movement_body_contents};
        qa_movement_result result = {0};
        CHECK(qa_movement_move(&input, &services, &result, &error));
        CHECK(qa_bounds_equal(result.bounds, requested));
        CHECK(test.horizontal_traces && qa_movement_origin(&result.state).x > origin.x);
        CHECK(result.view_height == 22 && qa_bounds_equal(input.shape.bounds, reference));

        /* Removing a request restores the character posture, rather than
         * treating the previously accepted physical hull as its reference. */
        input.state = result.state;
        input.current_bounds = result.bounds;
        input.has_current_bounds = true;
        input.environment.has_body_bounds = false;
        test = (movement_body_test){.watched = reference};
        CHECK(qa_movement_move(&input, &services, &result, &error));
        CHECK(qa_bounds_equal(result.bounds, reference) && result.view_height == 22);

        for (unsigned ducked = 0; ducked < 2; ++ducked) {
            input.state = qa_movement_state_default(kind, origin);
            input.current_bounds = requested;
            input.environment.has_body_bounds = true;
            input.environment.body_bounds = larger;
            input.command.forward_move = 0;
            input.command.up_move = ducked ? 0 : -1;
            input.command.buttons = ducked ? 0 : 16;
            float prior_height = ducked ? -2 : 19;
            if (kind == QA_RULESET_Q2_CLASSIC) {
                input.state.data.q2.gravity = 0;
                input.state.data.q2.flags = ducked ? 1 : 4;
            } else {
                input.state.data.q2r.gravity = 0;
                input.state.data.q2r.flags = ducked;
                input.state.data.q2r.view_height = prior_height;
            }
            test = (movement_body_test){.watched = larger, .blocked = true};
            CHECK(qa_movement_move(&input, &services, &result, &error));
            CHECK(qa_bounds_equal(result.bounds, requested));
            uint32_t flags = kind == QA_RULESET_Q2_CLASSIC
                ? result.state.data.q2.flags : result.state.data.q2r.flags;
            CHECK((flags & 1u) == ducked);
            float expected_height = kind == QA_RULESET_Q2_CLASSIC
                ? (ducked ? -2 : 22) : prior_height;
            CHECK(result.view_height == expected_height);
            CHECK(test.probes && qa_vec_equal(test.first_probe, origin));
        }

        /* Original Q2 clearance rejects allsolid, not startsolid alone. */
        input.state = qa_movement_state_default(kind, origin);
        input.command.up_move = 0;
        input.command.buttons = 0;
        test = (movement_body_test){.watched = larger, .start_solid = true};
        CHECK(qa_movement_move(&input, &services, &result, &error));
        CHECK(qa_bounds_equal(result.bounds, larger) && result.view_height == 22);

        input.environment.fixed_pose = true;
        input.environment.pose = (qa_movement_posture){requested, 13};
        CHECK(qa_movement_move(&input, &services, &result, &error));
        CHECK(qa_bounds_equal(result.bounds, requested) && result.view_height == 13);
        qa_movement_result_free(&result);
    }
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
    char sequence[3];
    qa_command_context delivered;
} log_delivery_test;
static bool log_test_output(void *context,const char *text,qa_error *error)
{
    log_delivery_test *test=context;(void)error;
    CHECK(text && *text && test->captured<sizeof test->sequence);
    test->sequence[test->captured++]=*text;return true;
}
static void log_test_normal(void *context,const qa_command_context *command,const char *text)
{
    log_delivery_test *test=context;
    CHECK(text && *text);test->delivered=*command;++test->normal;
}
static bool log_test_produce(void *context,const qa_command_invocation *command,qa_error *error)
{
    log_delivery_test *test=context;char text[]="2";
    qa_console_emit_to(command->console,&command->context,"1",log_test_normal,test);
    bool okay=qa_application_log_emit(&test->app,&command->context,text,QA_APPLICATION_LOG_CONSOLE,error);
    qa_console_emit(command->console,&command->context,"3");
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
    CHECK(test.captured==3 && test.normal==0 && !memcmp(test.sequence,"123",3));
    qa_application_event_view view;
    CHECK(qa_application_event_read(&test.app,&(qa_application_event_cursor){.id=first},&view) &&
        view.kind==QA_APPLICATION_EVENT_LOG);
    const qa_application_log_event *log=view.value.log;
    CHECK(log->context.dialect==context.dialect && log->context.owner==1 && log->text[0]=='1');
    CHECK(qa_application_event_read(&test.app,&(qa_application_event_cursor){.id=first+1},&view) &&
        view.value.log->text[0]=='2');
    qa_application_log_deliver(test.app.console,log,false,NULL,NULL);
    CHECK(test.captured==3 && test.normal==0);
    uint64_t next=qa_application_events_next(&test.app);
    CHECK(qa_application_log_emit(&test.app,&context,"value",QA_APPLICATION_LOG_CONSOLE,&error));
    CHECK(qa_application_event_read(&test.app,&(qa_application_event_cursor){.id=next},&view));
    qa_application_log_deliver(test.app.console,view.value.log,false,NULL,NULL);
    CHECK(test.normal==1 && test.delivered.dialect==context.dialect);
    qa_console_destroy(test.app.console);
    application_event_stream_dispose(&test.app);free(test.app.unified_persistent);
}
static void test_q1_muzzle_projection(void)
{
    qa_application app={0};qa_error error={0};
    CHECK(qa_session_create(&(qa_session_options){.actor_capacity=2,.component_capacity=1},&app.session,&error));
    CHECK(application_event_stream_create(&app,16,&error));
    uint64_t registry=qa_actors_identity(qa_session_actors(app.session));
    qa_actor_id current={.registry=registry,.slot=0,.generation=2};
    qa_actor_id previous=current;previous.generation=1;
    qa_actor_id aborted=current;aborted.generation=3;
    application_event_write write;
    CHECK(application_event_stream_begin(&app,QA_APPLICATION_EVENT_BUILTIN,&write,&error));
    write.envelope->raw.builtin.event=(qa_builtin_event){.family=QA_GAME_Q1,.kind=QA_BUILTIN_MUZZLE,.actor=current};
    CHECK(application_event_stream_commit(&app,&write,&error));
    CHECK(application_events_local_q1_muzzle(&app,current));
    uint64_t next=qa_application_events_next(&app);
    CHECK(application_event_stream_begin(&app,QA_APPLICATION_EVENT_BUILTIN,&write,&error));
    write.envelope->raw.builtin.event=(qa_builtin_event){.family=QA_GAME_Q1,.kind=QA_BUILTIN_MUZZLE,.actor=aborted};
    application_event_stream_abort(&app,&write,&error);
    CHECK(qa_application_events_next(&app)==next && !application_events_local_q1_muzzle(&app,aborted));
    CHECK(application_event_stream_begin(&app,QA_APPLICATION_EVENT_BUILTIN,&write,&error));
    write.envelope->raw.builtin.event=(qa_builtin_event){.family=QA_GAME_Q1,.kind=QA_BUILTIN_MUZZLE,.actor=previous};
    CHECK(application_event_stream_commit(&app,&write,&error));
    CHECK(application_events_local_q1_muzzle(&app,current) && application_events_local_q1_muzzle(&app,previous));
    app.event_local_cursor=next;
    CHECK(!application_events_local_q1_muzzle(&app,current) && application_events_local_q1_muzzle(&app,previous));
    CHECK(application_event_stream_begin(&app,QA_APPLICATION_EVENT_BUILTIN,&write,&error));
    write.envelope->raw.builtin.event=(qa_builtin_event){.family=QA_GAME_Q1,.kind=QA_BUILTIN_MUZZLE,.actor=current};
    CHECK(application_event_stream_commit(&app,&write,&error));
    CHECK(application_events_local_q1_muzzle(&app,current) && application_events_local_q1_muzzle(&app,previous));
    app.event_local_cursor=app.event_source_cursor=qa_application_events_next(&app);
    application_events_retire(&app);
    CHECK(!application_events_local_q1_muzzle(&app,current) && !application_events_local_q1_muzzle(&app,previous));
    CHECK(application_event_stream_begin(&app,QA_APPLICATION_EVENT_BUILTIN,&write,&error));
    write.envelope->raw.builtin.event=(qa_builtin_event){.family=QA_GAME_Q1,.kind=QA_BUILTIN_MUZZLE,.actor=previous};
    CHECK(application_event_stream_commit(&app,&write,&error));
    CHECK(!application_events_local_q1_muzzle(&app,current) && application_events_local_q1_muzzle(&app,previous));
    qa_actor_id foreign=current;foreign.registry+=100;
    CHECK(!application_events_local_q1_muzzle(&app,foreign));
    application_event_stream_dispose(&app);free(app.unified_persistent);
    CHECK(qa_session_destroy(app.session,&error));
}
static void test_connection_request_cursor(void)
{
    qa_application app={0}; qa_error error={0};
    CHECK(application_event_stream_create(&app,16,&error));
    qa_application_event_subscription reader={0};
    qa_application_events_subscribe(&app,&reader);
    char server[]="127.0.0.1:26000";
    qa_application_connection_request_event request={.recipient=&reader,.server=server};
    CHECK(qa_application_connection_request_emit(&app,&request,&error));
    server[0]='9'; request.disconnect=true; request.server="";
    CHECK(qa_application_connection_request_emit(&app,&request,&error));
    request=(qa_application_connection_request_event){.recipient=&app,.server="localhost"};
    CHECK(qa_application_connection_request_emit(&app,&request,&error));
    qa_application_event_range range={.next=reader.next,.end=qa_application_events_next(&app)};
    qa_application_event_view output; uint64_t mask=UINT64_C(1)<<QA_APPLICATION_EVENT_CONNECTION_REQUEST;
    CHECK(qa_application_event_raw_next(&app,&range,mask,&output));
    const qa_application_connection_request_event *first=output.value.connection_request;
    CHECK(first->recipient==&reader && !first->disconnect && first->server[0]=='1');
    qa_event_lease *lease=qa_application_event_retain(&app,range.id); CHECK(lease);
    CHECK(qa_application_event_raw_next(&app,&range,mask,&output));
    CHECK(output.value.connection_request->disconnect && !*output.value.connection_request->server);
    CHECK(qa_application_event_raw_next(&app,&range,mask,&output));
    CHECK(output.value.connection_request->recipient==&app);
    CHECK(!qa_application_event_raw_next(&app,&range,mask,&output));
    app.event_local_cursor=app.event_source_cursor=range.end;
    qa_application_events_consumed(&reader,range.end);
    qa_application_events_unsubscribe(&reader);
    CHECK(!strcmp(first->server,"127.0.0.1:26000"));
    qa_event_lease_release(lease);
    application_event_stream_dispose(&app);free(app.unified_persistent);
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
    qa_application_event_range range={.next=1,.end=2};
    qa_application_event_view raw;
    CHECK(qa_application_event_raw_next(&app,&range,UINT64_C(1)<<QA_APPLICATION_EVENT_Q3_EFFECT,&raw));
    CHECK(range.id==1 && raw.value.q3_effect->recipient==&first);
    CHECK(!qa_application_event_raw_next(&app,&range,UINT64_MAX,&raw));
    range.end=3;
    CHECK(qa_application_event_raw_next(&app,&range,UINT64_MAX,&raw));
    CHECK(range.id==2 && raw.value.q3_effect->recipient==&second);
    range=(qa_application_event_range){.next=1,.end=3};
    CHECK(!qa_application_event_raw_next(&app,&range,UINT64_C(1)<<QA_APPLICATION_EVENT_LOG,&raw));
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
    range=(qa_application_event_range){.next=1,.end=3};
    CHECK(qa_application_event_raw_next(&app,&range,UINT64_MAX,&raw) && range.id==2);
    cursor=(qa_application_event_cursor){.id=2};
    CHECK(qa_application_event_read(&app,&cursor,&view) && view.value.q3_effect->recipient==&second &&
        view.value.q3_effect->entity.event==21);
    qa_application_events_unsubscribe(&second);
    CHECK(qa_application_events_first(&app)==3 && !second.application);
    application_event_stream_dispose(&app);
    CHECK(!first.application);
    free(app.unified_persistent);
}

static double cinematic_test_clock(void *context) { return *(const double *)context; }
static void cinematic_test_complete(void *context,qa_cinematic_target target,qa_cinematic_end reason)
{
    (void)target; (void)reason; ++*(unsigned *)context;
}
/* INFO, one solid codebook frame, one skip frame, and EOF lookahead. */
static const uint8_t cinematic_test_roq[]={
    0x84,0x10,0xff,0xff,0xff,0xff,30,0,
    0x01,0x10,8,0,0,0,0,0, 8,0,8,0,8,0,4,0,
    0x02,0x10,10,0,0,0,1,1, 100,100,100,100,128,128,0,0,0,0,
    0x11,0x10,3,0,0,0,0,0, 0,0x80,0,
    0x11,0x10,2,0,0,0,0,0, 0,0,
    0x12,0x10,0,0,0,0,0,0};
static void test_roq_prefix_checkpoint(void)
{
    qa_error error={0}; qa_media_input *input=NULL;
    CHECK(qa_media_input_memory((qa_bytes){cinematic_test_roq,sizeof(cinematic_test_roq)},NULL,NULL,&input,&error));
    qa_cinematic_source source={.name="prefix.roq",.format=QA_CINEMATIC_ROQ,.data.roq=input};
    for (unsigned shared=0;shared<2;++shared) {
        qa_roq_scratch *scratch=NULL;
        if (shared) CHECK(qa_roq_scratch_create(&scratch,&error));
        double now=shared?500:0,origin=now;
        qa_cinematic_options options={.clock={&now,cinematic_test_clock},.roq_scratch=scratch,
            .target={.kind=QA_CINEMATIC_SEAT},.loop=true,.silent=true};
        qa_cinematic *movie=NULL,*restored=NULL; qa_cinematic_checkpoint saved,copy;
        qa_media_tick tick;
        CHECK(qa_cinematic_create(&source,&options,&movie,&error));
        CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error));
        CHECK(saved.progress_kind==QA_CINEMATIC_PROGRESS_ROQ && saved.progress.roq.decoded==-1 &&
            !saved.progress.roq.picture.present && saved.progress.roq.cursor.position==16);
        double later=10000;
        qa_cinematic_options resumed=options; resumed.clock.context=&later;
        CHECK(qa_cinematic_restore(&source,&resumed,&saved,&restored,&error));
        CHECK(qa_cinematic_checkpoint_read(restored,&copy,&error));
        CHECK(copy.progress.roq.decoded==-1 && copy.progress.roq.clock_ms==saved.progress.roq.clock_ms &&
            copy.progress.roq.cursor.position==16 && !qa_cinematic_frame(restored));
        qa_cinematic_destroy(restored); restored=NULL;
        CHECK(qa_cinematic_tick(movie,&tick,&error) && !tick.frame);
        CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error));
        CHECK(saved.progress.roq.decoded==0 && saved.progress.roq.cursor.position==32);
        now=origin+34; /* The clock advances without another decode. */
        CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error));
        CHECK(qa_cinematic_restore(&source,&resumed,&saved,&restored,&error));
        CHECK(!qa_cinematic_frame(restored));
        CHECK(qa_cinematic_checkpoint_read(restored,&copy,&error));
        CHECK(copy.progress.roq.decoded==0 && copy.progress.roq.clock_ms==origin+34);
        CHECK(qa_cinematic_tick(restored,&tick,&error) && tick.frame && tick.frame->index==0);
        CHECK(qa_cinematic_tick(movie,&tick,&error) && tick.frame && tick.frame->index==0);
        CHECK(tick.frame->rgba.size==256 &&
            qa_load_u32le(tick.frame->rgba.data)==qa_roq_yuv_rgba(100,128,128));
        CHECK(!memcmp(tick.frame->rgba.data,qa_cinematic_frame(restored)->rgba.data,256));
        qa_cinematic_destroy(restored); restored=NULL;
        CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error));
        qa_cinematic_checkpoint legacy=saved; legacy.progress_kind=QA_CINEMATIC_PROGRESS_TIME;
        legacy.audio.captured=false;
        CHECK(qa_cinematic_restore(&source,&resumed,&legacy,&restored,&error));
        CHECK(qa_cinematic_frame(restored) && qa_cinematic_frame(restored)->index==0);
        qa_cinematic_destroy(restored); restored=NULL;
        CHECK(qa_cinematic_pause(movie,true,&error)); now+=80;
        CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error));
        CHECK(saved.status==QA_MEDIA_PAUSED && saved.progress.roq.clock_ms==origin+34);
        CHECK(qa_cinematic_restore(&source,&resumed,&saved,&restored,&error));
        later+=80;
        CHECK(qa_cinematic_checkpoint_read(restored,&copy,&error));
        CHECK(copy.progress.roq.clock_ms==saved.progress.roq.clock_ms && copy.status==QA_MEDIA_PAUSED);
        qa_cinematic_destroy(restored); restored=NULL;
        CHECK(qa_cinematic_pause(movie,false,&error));
        now=origin+80+67;
        CHECK(qa_cinematic_tick(movie,&tick,&error) && tick.frame && tick.frame->index==1);
        now=origin+80+100;
        CHECK(qa_cinematic_tick(movie,&tick,&error) && tick.looped && tick.frame);
        CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error));
        CHECK(saved.loop==1 && saved.progress.roq.decoded==-1 && saved.progress.roq.picture.present &&
            saved.progress.roq.picture.loop==0 && saved.progress.roq.picture.index==1);
        CHECK(qa_cinematic_restore(&source,&resumed,&saved,&restored,&error));
        const qa_media_frame *frame=qa_cinematic_frame(restored);
        CHECK(frame && frame->index==1 && frame->loop==0 &&
            frame->presentation_ms==saved.progress.roq.picture.presentation_ms &&
            !memcmp(frame->rgba.data,tick.frame->rgba.data,frame->rgba.size));
        CHECK(qa_cinematic_tick(restored,&tick,&error) && !tick.looped && tick.frame && tick.frame->index==1);
        CHECK(qa_cinematic_checkpoint_read(restored,&copy,&error) && copy.loop==1 && copy.progress.roq.decoded==0);
        qa_cinematic_destroy(restored); restored=NULL;
        if (shared) {
            CHECK(qa_cinematic_roq_restart(movie,&error));
            CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error));
            CHECK(saved.loop==0 && saved.progress.roq.decoded==-1 && saved.progress.roq.pending_loop &&
                saved.progress.roq.picture.present);
            CHECK(qa_cinematic_restore(&source,&resumed,&saved,&restored,&error));
            CHECK(qa_cinematic_frame(restored) && qa_cinematic_frame(restored)->index==1);
            CHECK(qa_cinematic_tick(restored,&tick,&error) && tick.looped);
            CHECK(qa_cinematic_checkpoint_read(restored,&copy,&error) && copy.progress.roq.decoded==-1 &&
                !copy.progress.roq.pending_loop);
            qa_cinematic_destroy(restored);
        }
        qa_cinematic_destroy(movie); qa_roq_scratch_release(scratch);
    }
    double now=0; unsigned completions=0;
    qa_cinematic_options options={.clock={&now,cinematic_test_clock},
        .target={.kind=QA_CINEMATIC_SEAT},.silent=true,.context=&completions,.complete=cinematic_test_complete};
    qa_cinematic *movie=NULL,*restored=NULL; qa_cinematic_checkpoint saved;
    qa_media_tick tick;
    CHECK(qa_cinematic_create(&source,&options,&movie,&error));
    CHECK(qa_cinematic_tick(movie,&tick,&error)); now=100;
    CHECK(qa_cinematic_tick(movie,&tick,&error) && tick.status==QA_MEDIA_ENDED && completions==1);
    CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error));
    now=20000;
    CHECK(qa_cinematic_restore(&source,&options,&saved,&restored,&error));
    CHECK(qa_cinematic_status(restored)==QA_MEDIA_ENDED && completions==1);
    CHECK(qa_cinematic_tick(restored,&tick,&error) && tick.status==QA_MEDIA_ENDED && completions==1);
    qa_cinematic_destroy(restored); qa_cinematic_destroy(movie);
    CHECK(completions==1);
    CHECK(qa_cinematic_create(&source,&options,&movie,&error));
    CHECK(qa_cinematic_tick(movie,&tick,&error));
    CHECK(qa_cinematic_end_playback(movie,QA_CINEMATIC_STOPPED,&error) && completions==2);
    CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error));
    CHECK(qa_cinematic_restore(&source,&options,&saved,&restored,&error));
    CHECK(qa_cinematic_status(restored)==QA_MEDIA_STOPPED && completions==2);
    qa_cinematic_destroy(restored); qa_cinematic_destroy(movie);
    CHECK(completions==2);
    qa_media_input_release(input);
}

static bool cinematic_bank_test_limits(void *context,bool *limited,qa_error *error)
{ (void)context; (void)error; *limited=false; return true; }
static bool cinematic_bank_test_upload(void *context,const qa_scene_image *registered,
    const qa_scene_image *version,bool redefine,bool dirty,qa_error *error)
{
    (void)context; (void)registered; (void)version; (void)redefine; (void)dirty; (void)error;
    return true;
}
static uint64_t cinematic_bank_test_bus(void *context) { return *(const uint64_t *)context; }
static bool cinematic_bank_test_video(void *context,int32_t *value,qa_error *error)
{ (void)context; (void)error; *value=1; return true; }
static bool cinematic_bank_test_current(void *context,const qa_q3_cinematic_source_options *options)
{ return options && options->context==context; }
static void test_roq_handle_checkpoints(void)
{
    qa_error error={0};
    char directory[]="/tmp/qa-core-roq-XXXXXX",folder[128];
    CHECK(mkdtemp(directory));
    CHECK(snprintf(folder,sizeof(folder),"%s/video",directory)>0 && mkdir(folder,0700)==0);
    qa_resource_pool *pool=qa_resource_pool_create(&error); CHECK(pool);
    qa_vfs *files=qa_vfs_create(pool,&error); CHECK(files);
    qa_mount_id mount;
    CHECK(qa_vfs_mount_directory(files,directory,QA_ARCHIVE_EXACT,true,&mount,&error));
    const char *paths[]={"video/first.roq","video/second.roq"};
    uint8_t second[sizeof(cinematic_test_roq)];
    memcpy(second,cinematic_test_roq,sizeof(second));
    memset(second+32,180,4); /* The second movie's four codebook luma values. */
    CHECK(qa_vfs_write(files,mount,paths[0],(qa_bytes){cinematic_test_roq,sizeof(cinematic_test_roq)},&error));
    CHECK(qa_vfs_write(files,mount,paths[1],(qa_bytes){second,sizeof(second)},&error));
    qa_scene_resources *images=qa_scene_resources_create(files,&error); CHECK(images);
    qa_q3_image_upload_options upload={.color={.device={.color_bits=24},.gamma=1,.intensity=1}};
    CHECK(qa_scene_resources_source_q3_initialize(images,&upload,&error));
    qa_media_library *media=qa_media_library_create(images,&error); CHECK(media);
    qa_q3_cinematic_handles_options bank_options={.images=images,
        .ui_limits=cinematic_bank_test_limits,.upload=cinematic_bank_test_upload};
    qa_q3_cinematic_handles *banks[2]={0};
    qa_q3_cinematic_source *sources[2][2]={{0}};
    int32_t handles[2]={-1,-1};
    const char *instances[]={"first-movie","second-movie"};
    uint64_t buses[]={101,102};
    double now[]={0,10000};
    qa_q3_cinematic_restore_slot rows[2]={0};
    uint8_t pixels[2][8*8*4];
    for (unsigned pass=0;pass<2;++pass) {
        CHECK(qa_q3_cinematic_handles_create(&bank_options,banks+pass,&error));
        if (pass) CHECK(qa_q3_cinematic_handles_restore_reserve(banks[pass],rows,2,&error));
        for (unsigned i=0;i<2;++i) {
            qa_q3_cinematic_source_options options={.files=files,.media=media,
                .clock={now+pass,cinematic_test_clock},.context=buses+i,.audio_bus=cinematic_bank_test_bus,
                .in_game_video=cinematic_bank_test_video,.current=cinematic_bank_test_current};
            CHECK(qa_q3_cinematic_source_create(banks[pass],&options,&sources[pass][i],&error));
            CHECK(qa_q3_cinematic_source_restore_bind(sources[pass][i],instances[i],"core-roq",0,0,UINT32_MAX,&error));
            if (!pass) CHECK(qa_q3_cinematic_play(sources[pass][i],paths[i],(qa_scene_rect_f){0,0,8,8},
                2u|8u,NULL,NULL,handles+i,&error) && handles[i]>=0);
        }
        if (!pass) {
            const double times[]={0,34,67};
            for (unsigned step=0;step<3;++step) {
                now[pass]=times[step];
                for (unsigned i=0;i<2;++i) {
                    int32_t status;
                    CHECK(qa_q3_cinematic_run(sources[pass][i],handles[i],&status,&error) && status==1);
                    qa_q3_cinematic_slot slot;
                    CHECK(qa_q3_cinematic_handles_at(banks[pass],(uint32_t)handles[i],&slot) && slot.playback);
                    const qa_media_frame *picture=qa_cinematic_frame(slot.playback);
                    CHECK(step?picture && picture->index==step-1:!picture);
                }
            }
        } else {
            bool complete=false;
            CHECK(qa_q3_cinematic_handles_restore_finish(banks[pass],&complete,&error) && complete);
        }
        qa_roq_scratch *first_scratch=qa_q3_cinematic_source_decoder_scratch(sources[pass][0],handles[0]);
        qa_roq_scratch *second_scratch=qa_q3_cinematic_source_decoder_scratch(sources[pass][1],handles[1]);
        CHECK(first_scratch && second_scratch && first_scratch!=second_scratch);
        for (unsigned i=0;i<2;++i) {
            qa_q3_cinematic_slot slot;
            CHECK(qa_q3_cinematic_handles_at(banks[pass],(uint32_t)handles[i],&slot) && slot.playback);
            const qa_media_frame *picture=qa_cinematic_frame(slot.playback);
            CHECK(picture && picture->index==1 && picture->loop==0 && picture->rgba.size==sizeof(pixels[i]));
            CHECK(qa_load_u32le(picture->rgba.data)==qa_roq_yuv_rgba(i?180:100,128,128));
            if (pass) CHECK(!memcmp(pixels[i],picture->rgba.data,sizeof(pixels[i])));
            else {
                memcpy(pixels[i],picture->rgba.data,sizeof(pixels[i]));
                rows[i]=(qa_q3_cinematic_restore_slot){.handle=handles[i],.instance=slot.instance,.content=slot.content,
                    .role=slot.role,.seat=slot.seat,.source_group=slot.source_group,.path=slot.path,.rect=slot.rect,
                    .flags=slot.flags,.has_playback=true,.occupied=slot.occupied,.selected=slot.selected,
                    .decoder=slot.decoder,.status=slot.status,.play_on_walls=slot.play_on_walls};
                CHECK(qa_cinematic_checkpoint_read((qa_cinematic *)slot.playback,&rows[i].playback,&error));
            }
        }
    }
    now[0]=100; now[1]+=33;
    for (unsigned i=0;i<2;++i) {
        qa_cinematic_checkpoint progress[2];
        for (unsigned pass=0;pass<2;++pass) {
            int32_t status;
            CHECK(qa_q3_cinematic_run(sources[pass][i],handles[i],&status,&error) && status==5);
            qa_q3_cinematic_slot slot;
            CHECK(qa_q3_cinematic_handles_at(banks[pass],(uint32_t)handles[i],&slot) && slot.playback);
            CHECK(qa_cinematic_checkpoint_read((qa_cinematic *)slot.playback,progress+pass,&error));
            const qa_media_frame *picture=qa_cinematic_frame(slot.playback);
            CHECK(picture && picture->index==1 && picture->loop==0 &&
                !memcmp(pixels[i],picture->rgba.data,sizeof(pixels[i])));
        }
        CHECK(progress[0].loop==1 && progress[1].loop==progress[0].loop &&
            progress[1].progress.roq.decoded==progress[0].progress.roq.decoded &&
            progress[1].progress.roq.cursor.position==progress[0].progress.roq.cursor.position);
    }
    qa_q3_cinematic_slot survivor;
    CHECK(qa_q3_cinematic_handles_at(banks[0],(uint32_t)handles[1],&survivor) && survivor.playback);
    qa_cinematic_checkpoint before,after;
    CHECK(qa_cinematic_checkpoint_read((qa_cinematic *)survivor.playback,&before,&error));
    CHECK(qa_q3_cinematic_source_destroy(&sources[0][0],&error));
    CHECK(qa_cinematic_checkpoint_read((qa_cinematic *)survivor.playback,&after,&error));
    CHECK(after.loop==before.loop && after.progress.roq.decoded==before.progress.roq.decoded &&
        after.progress.roq.cursor.position==before.progress.roq.cursor.position &&
        !memcmp(pixels[1],qa_cinematic_frame(survivor.playback)->rgba.data,sizeof(pixels[1])));
    for (unsigned pass=0;pass<2;++pass) {
        for (unsigned i=0;i<2;++i) CHECK(qa_q3_cinematic_source_destroy(&sources[pass][i],&error));
        CHECK(qa_q3_cinematic_handles_destroy(banks+pass,&error));
    }
    qa_media_library_destroy(media); qa_scene_resources_destroy(images);
    for (unsigned i=0;i<2;++i) CHECK(qa_vfs_remove(files,mount,paths[i],&error));
    qa_vfs_destroy(files); qa_resource_pool_destroy(pool);
    CHECK(rmdir(folder)==0 && rmdir(directory)==0);
}

static void test_raw_audio_custody(void)
{
    qa_error error={0};
    qa_audio_raw_stream *stream=NULL,*copy=NULL,*restored=NULL;
    int16_t first[2050],second[2050];
    float discarded[4080]={0},a[4120]={0},b[4120]={0};
    for(size_t i=0;i<2050;++i) { first[i]=(int16_t)i; second[i]=(int16_t)(-1-(int)i); }
    CHECK(qa_audio_raw_create(22050,&stream,&error));
    CHECK(qa_audio_raw_queue(stream,first,2050,1,22050,0,false,&error));
    CHECK(qa_audio_raw_mix(stream,discarded,2040,1)==2040);
    CHECK(qa_audio_raw_queue(stream,second,2050,1,22050,2050,false,&error));
    CHECK(qa_audio_raw_clone_rate(stream,22050,&copy,&error));
    qa_buffer saved={0};
    CHECK(qa_audio_raw_checkpoint(stream,&saved,&error));
    CHECK(qa_audio_raw_restore((qa_bytes){saved.data,saved.size},22050,&restored,&error));
    qa_buffer_free(&saved);
    qa_audio_raw_destroy(stream);
    CHECK(qa_audio_raw_mix(copy,a,2060,1)==2060);
    CHECK(qa_audio_raw_mix(restored,b,2060,1)==2060);
    CHECK(!memcmp(a,b,sizeof(a)));
    for(size_t i=0;i<2060;++i) {
        float expected=i<10?first[2040+i]:second[i-10];
        CHECK(a[2*i]==expected && a[2*i+1]==expected);
    }
    qa_audio_raw_destroy(copy); qa_audio_raw_destroy(restored);
}

static void test_raw_audio_scalar_cursor(void)
{
    qa_error error={0}; qa_audio_raw_stream *source=NULL,*restored=NULL;
    int16_t samples[40]; float discarded[10]={0},a[32]={0},b[32]={0};
    for (size_t i=0;i<40;++i) samples[i]=(int16_t)(100+i*7);
    CHECK(qa_audio_raw_create(7,&source,&error));
    CHECK(qa_audio_raw_queue(source,samples,40,1,5,0,true,&error));
    CHECK(qa_audio_raw_mix(source,discarded,5,1)==5);
    CHECK(qa_audio_raw_set_rate(source,3,&error));
    CHECK(qa_audio_raw_mix(source,discarded,1,1)==1);
    qa_audio_raw_cursor cursor;
    qa_audio_raw_pause(source,true);
    CHECK(qa_audio_raw_cursor_read(source,&cursor));
    CHECK(cursor.position==4 && qa_audio_raw_position(source)==5 && cursor.fraction>0 && cursor.remainder==2);
    CHECK(qa_audio_raw_create(3,&restored,&error));
    CHECK(qa_audio_raw_queue(restored,samples+5,35,1,5,5,true,&error));
    CHECK(qa_audio_raw_cursor_restore(restored,&cursor,&error));
    CHECK(qa_audio_raw_mix(source,a,8,1)==0 && qa_audio_raw_mix(restored,b,8,1)==0);
    qa_audio_raw_pause(source,false); qa_audio_raw_pause(restored,false);
    CHECK(qa_audio_raw_cursor_read(source,&cursor));
    CHECK(qa_audio_raw_mix(source,a,8,1)==8 && qa_audio_raw_mix(restored,b,8,1)==8);
    CHECK(!memcmp(a,b,16*sizeof(*a)));
    qa_audio_raw_destroy(restored); restored=NULL;
    CHECK(qa_audio_raw_queue(source,samples,40,1,5,0,true,&error));
    CHECK(qa_audio_raw_cursor_restore(source,&cursor,&error));
    CHECK(qa_audio_raw_set_rate(source,11,&error));
    CHECK(qa_audio_raw_create(11,&restored,&error));
    CHECK(qa_audio_raw_queue(restored,samples+5,35,1,5,5,true,&error));
    CHECK(qa_audio_raw_cursor_restore(restored,&cursor,&error));
    memset(a,0,sizeof(a)); memset(b,0,sizeof(b));
    CHECK(qa_audio_raw_mix(source,a,16,1)==16 && qa_audio_raw_mix(restored,b,16,1)==16);
    CHECK(!memcmp(a,b,sizeof(a)));
    qa_audio_raw_destroy(source); qa_audio_raw_destroy(restored); source=restored=NULL;
    CHECK(qa_audio_raw_create(2,&source,&error));
    CHECK(qa_audio_raw_queue(source,samples,1,1,5,0,true,&error));
    CHECK(qa_audio_raw_mix(source,discarded,1,1)==1);
    CHECK(qa_audio_raw_cursor_read(source,&cursor) && cursor.position>cursor.end);
    CHECK(qa_audio_raw_create(2,&restored,&error));
    CHECK(qa_audio_raw_queue(restored,samples,1,1,5,0,true,&error));
    CHECK(qa_audio_raw_cursor_restore(restored,&cursor,&error));
    CHECK(qa_audio_raw_mix(restored,discarded,1,1)==0);
    CHECK(qa_audio_raw_queue(source,samples+1,10,1,5,1,false,&error));
    CHECK(qa_audio_raw_queue(restored,samples+1,10,1,5,1,false,&error));
    memset(a,0,sizeof(a)); memset(b,0,sizeof(b));
    CHECK(qa_audio_raw_mix(source,a,3,1)==3 && qa_audio_raw_mix(restored,b,3,1)==3);
    CHECK(!memcmp(a,b,6*sizeof(*a)));
    qa_audio_raw_destroy(source); qa_audio_raw_destroy(restored); source=restored=NULL;
    CHECK(qa_audio_raw_create(7,&source,&error) && qa_audio_raw_cursor_read(source,&cursor));
    CHECK(qa_audio_raw_create(11,&restored,&error) && qa_audio_raw_cursor_restore(restored,&cursor,&error));
    CHECK(qa_audio_raw_position(restored)==0 && qa_audio_raw_queued(restored)==0);
    qa_audio_raw_destroy(source); qa_audio_raw_destroy(restored);
}

static void test_roq_audio_cursor(void)
{
    uint8_t bytes[196]={0};
    qa_store_u16le(bytes,0x1084); qa_store_u16le(bytes+6,30);
    qa_store_u16le(bytes+8,0x1001); qa_store_u32le(bytes+10,8);
    qa_store_u16le(bytes+16,8); qa_store_u16le(bytes+18,8);
    qa_store_u16le(bytes+24,0x1021); qa_store_u32le(bytes+26,64); memset(bytes+32,1,64);
    qa_store_u16le(bytes+96,0x1011); qa_store_u32le(bytes+98,2);
    qa_store_u16le(bytes+106,0x1021); qa_store_u32le(bytes+108,64); memset(bytes+114,1,64);
    qa_store_u16le(bytes+178,0x1011); qa_store_u32le(bytes+180,2);
    qa_store_u16le(bytes+188,0x1012);
    qa_error error={0}; qa_media_input *input=NULL;
    CHECK(qa_media_input_memory((qa_bytes){bytes,sizeof(bytes)},NULL,NULL,&input,&error));
    qa_cinematic_source source={.name="audio-prefix.roq",.format=QA_CINEMATIC_ROQ,.data.roq=input};
    for (unsigned looping=0;looping<2;++looping) {
        qa_audio_engine *engine=NULL,*copy_engine=NULL;
        qa_audio_engine_options engine_options={.sample_rate=48000,.output_channels=2,
            .mix_frames=32,.initial_voices=8};
        CHECK(qa_audio_engine_create(&engine_options,&engine,&error));
        CHECK(qa_audio_engine_create(&engine_options,&copy_engine,&error));
        double now=0,later=10000;
        qa_cinematic_options options={.clock={&now,cinematic_test_clock},
            .target={.kind=QA_CINEMATIC_SEAT},.loop=looping!=0,.hold=looping==0,
            .audio=engine,.audio_bus=23,.gain=1,.audio_audience={.kind=QA_CINEMATIC_AUDIO_WORLD}};
        qa_cinematic *movie=NULL,*restored=NULL; qa_cinematic_checkpoint saved,copy;
        qa_media_tick tick; int16_t discarded[14],a[32],b[32];
        qa_cinematic_options resumed=options; resumed.clock.context=&later; resumed.audio=copy_engine;
        CHECK(qa_cinematic_create(&source,&options,&movie,&error));
        CHECK(qa_cinematic_tick(movie,&tick,&error));
        CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error) && saved.audio.captured && !saved.audio.present);
        CHECK(qa_cinematic_restore(&source,&resumed,&saved,&restored,&error));
        CHECK(!qa_audio_engine_bus_stream(copy_engine,23));
        qa_cinematic_destroy(restored); restored=NULL;
        now=34;
        CHECK(qa_cinematic_tick(movie,&tick,&error));
        CHECK(qa_audio_engine_mix(engine,discarded,7,&error));
        now=100; CHECK(qa_cinematic_tick(movie,&tick,&error));
        CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error));
        CHECK(saved.audio.captured && saved.audio.present && saved.audio.loop==0 &&
            saved.audio.cursor.position==3 && saved.audio.cursor.end==64 && saved.audio.cursor.remainder!=0 &&
            !saved.audio.cursor.paused);
        CHECK(looping?(saved.loop==1 && saved.progress.roq.source_sample==0):
            (saved.status==QA_MEDIA_HELD && saved.paused));
        qa_cinematic_checkpoint legacy=saved; legacy.audio.captured=false;
        CHECK(qa_cinematic_restore(&source,&resumed,&legacy,&restored,&error));
        qa_cinematic_destroy(restored); restored=NULL;
        CHECK(qa_cinematic_restore(&source,&resumed,&saved,&restored,&error));
        CHECK(qa_cinematic_checkpoint_read(restored,&copy,&error));
        CHECK(copy.audio.present && copy.audio.loop==saved.audio.loop &&
            copy.audio.cursor.position==saved.audio.cursor.position &&
            copy.audio.cursor.remainder==saved.audio.cursor.remainder && !copy.audio.cursor.paused);
        CHECK(qa_audio_engine_mix(engine,a,16,&error) && qa_audio_engine_mix(copy_engine,b,16,&error));
        CHECK(!memcmp(a,b,sizeof(a)) && a[0]!=0);
        qa_cinematic_destroy(restored); qa_cinematic_destroy(movie);
        qa_audio_engine_destroy(copy_engine); qa_audio_engine_destroy(engine);
    }
    qa_media_input_release(input);
}

static void test_cin_prefix_checkpoint(void)
{
    uint8_t bytes[65556+782+14+782+4]={0};
    qa_store_u32le(bytes,1); qa_store_u32le(bytes+4,1);
    qa_store_u32le(bytes+8,14); qa_store_u32le(bytes+12,1); qa_store_u32le(bytes+16,1);
    for (unsigned i=0;i<256;++i) { bytes[20+i*256]=1; bytes[21+i*256]=1; }
    uint8_t *next=bytes+65556;
    for (unsigned i=0;i<3;++i) {
        bool palette=i!=1;
        qa_store_u32le(next,palette?1:0); next+=4;
        if (palette) { next[0]=i?30:10; next[4]=i?40:20; next+=768; }
        qa_store_u32le(next,5); next+=4;
        qa_store_u32le(next,1); next+=4;
        *next++=i==1?1:0;
        *next++=(uint8_t)(132+i);
    }
    qa_store_u32le(next,2); next+=4; CHECK((size_t)(next-bytes)==sizeof(bytes));
    qa_error error={0}; qa_media_input *input=NULL; qa_cin_asset *asset=NULL;
    CHECK(qa_media_input_memory((qa_bytes){bytes,sizeof(bytes)},NULL,NULL,&input,&error));
    CHECK(qa_cin_asset_load(input,&asset,&error));
    qa_cinematic_source source={.name="prefix.cin",.format=QA_CINEMATIC_CIN,.data.cin=asset};
    for (unsigned ending=0;ending<3;++ending) {
        qa_audio_engine *engine=NULL,*copy_engine=NULL;
        qa_audio_engine_options engine_options={.sample_rate=48000,.output_channels=2,
            .mix_frames=32,.initial_voices=8};
        CHECK(qa_audio_engine_create(&engine_options,&engine,&error));
        CHECK(qa_audio_engine_create(&engine_options,&copy_engine,&error));
        double now=0,later=10000; unsigned completions=0,restored_completions=0;
        qa_cinematic_options options={.clock={&now,cinematic_test_clock},
            .target={.kind=QA_CINEMATIC_SEAT},.hold=ending==1,.loop=ending==2,
            .audio=engine,.audio_bus=29,.gain=1,.audio_audience={.kind=QA_CINEMATIC_AUDIO_WORLD},
            .context=&completions,.complete=cinematic_test_complete};
        qa_cinematic_options resumed=options;
        resumed.clock.context=&later; resumed.audio=copy_engine; resumed.context=&restored_completions;
        qa_cinematic *movie=NULL,*restored=NULL; qa_cinematic_checkpoint saved,copy;
        qa_media_tick tick; int16_t discarded[34],a[32],b[32];
        CHECK(qa_cinematic_create(&source,&options,&movie,&error));
        CHECK(qa_cinematic_frame(movie) && qa_cinematic_frame(movie)->index==0 &&
            qa_cinematic_frame(movie)->rgba.data[0]==10);
        CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error) && saved.progress_kind==QA_CINEMATIC_PROGRESS_CIN &&
            saved.progress.cin.cursor.next_index==1 && !saved.progress.cin.cursor.ended);
        qa_cinematic_checkpoint legacy=saved; legacy.progress_kind=QA_CINEMATIC_PROGRESS_TIME;
        legacy.audio.captured=false;
        CHECK(qa_cinematic_restore(&source,&resumed,&legacy,&restored,&error));
        CHECK(qa_cinematic_frame(restored) && qa_cinematic_frame(restored)->index==0);
        qa_cinematic_destroy(restored); restored=NULL; restored_completions=0;
        now=500;
        CHECK(qa_audio_engine_mix(engine,discarded,17,&error));
        CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error));
        CHECK(saved.progress.cin.clock_ms==500 && saved.progress.cin.cursor.next_index==1 &&
            saved.audio.present && saved.audio.cursor.end==1 && saved.audio.cursor.remainder!=0);
        CHECK(qa_cinematic_restore(&source,&resumed,&saved,&restored,&error));
        CHECK(qa_cinematic_frame(restored) && qa_cinematic_frame(restored)->index==0 &&
            qa_cinematic_frame(restored)->rgba.data[0]==10);
        CHECK(qa_audio_engine_mix(engine,a,16,&error) && qa_audio_engine_mix(copy_engine,b,16,&error));
        CHECK(!memcmp(a,b,sizeof(a)) && a[0]==1024);
        qa_cinematic_destroy(restored); restored=NULL; restored_completions=0;
        CHECK(qa_cinematic_tick(movie,&tick,&error) && tick.changed && !tick.frame);
        CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error) && saved.progress.cin.cursor.next_index==2);
        CHECK(qa_cinematic_pause(movie,true,&error)); now=600;
        CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error) && saved.progress.cin.clock_ms==500);
        CHECK(qa_cinematic_restore(&source,&resumed,&saved,&restored,&error));
        CHECK(qa_cinematic_status(restored)==QA_MEDIA_PAUSED && !qa_cinematic_frame(restored));
        CHECK(qa_cinematic_checkpoint_read(restored,&copy,&error) && copy.progress.cin.cursor.next_index==2 &&
            copy.progress.cin.cursor.position==saved.progress.cin.cursor.position && copy.progress.cin.clock_ms==500);
        qa_cinematic_destroy(restored); restored=NULL; restored_completions=0;
        CHECK(qa_cinematic_pause(movie,false,&error)); now=744;
        CHECK(qa_cinematic_tick(movie,&tick,&error) && tick.frame && tick.frame->index==1 &&
            tick.frame->rgba.data[1]==20);
        CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error) && saved.progress.cin.cursor.next_index==3);
        CHECK(qa_cinematic_restore(&source,&resumed,&saved,&restored,&error));
        CHECK(qa_cinematic_frame(restored) && qa_cinematic_frame(restored)->index==1 &&
            qa_cinematic_frame(restored)->rgba.data[1]==20 &&
            qa_cinematic_frame(restored)->presentation_ms==tick.frame->presentation_ms);
        qa_cinematic_destroy(restored); restored=NULL; restored_completions=0;
        now=815; CHECK(qa_cinematic_tick(movie,&tick,&error));
        CHECK(qa_cinematic_checkpoint_read(movie,&saved,&error));
        if (ending==0) CHECK(saved.status==QA_MEDIA_ENDED && saved.progress.cin.cursor.ended && !tick.frame && completions==1);
        else if (ending==1) CHECK(saved.status==QA_MEDIA_HELD && saved.progress.cin.cursor.ended &&
            tick.frame && tick.frame->index==2 && tick.frame->rgba.data[0]==30 && !saved.audio.cursor.paused);
        else CHECK(saved.loop==1 && !saved.progress.cin.cursor.ended && saved.progress.cin.cursor.next_index==1 &&
            tick.frame && tick.frame->index==0 && saved.audio.loop==1);
        CHECK(qa_cinematic_restore(&source,&resumed,&saved,&restored,&error));
        CHECK(qa_cinematic_status(restored)==saved.status && restored_completions==0);
        const qa_media_frame *frame=qa_cinematic_frame(restored);
        if (tick.frame) CHECK(frame && frame->index==tick.frame->index && frame->loop==tick.frame->loop &&
            !memcmp(frame->rgba.data,tick.frame->rgba.data,frame->rgba.size));
        else CHECK(!frame);
        qa_cinematic_destroy(restored); qa_cinematic_destroy(movie);
        CHECK(completions==1 && restored_completions==(ending==0?0u:1u));
        qa_audio_engine_destroy(copy_engine); qa_audio_engine_destroy(engine);
    }
    qa_cin_asset_release(asset); qa_media_input_release(input);
}

static bool audio_position_not_actor(void *opaque, const qa_audio_position *position)
{
    return position->actor != *(const uint64_t *)opaque;
}
typedef struct audio_actor_roots {
    uint64_t sound, origin;
    unsigned found;
} audio_actor_roots;
static void audio_actor_root(void *opaque, uint64_t actor)
{
    audio_actor_roots *roots = opaque;
    if (actor == roots->sound) roots->found |= 1;
    if (actor == roots->origin) roots->found |= 2;
}

static void test_shared_audio_preparation(void)
{
    qa_error error = {0}; char directory[] = "/tmp/qa-audio-assets-XXXXXX";
    qa_audio_mixer *positions = NULL;
    qa_audio_mixer_options position_options = {.sample_rate = 44100, .output_channels = 2,
        .workspace_capacity = 8};
    uint64_t actor = UINT64_C(0x100000001), other = UINT64_C(0x200000001);
    uint64_t owner = UINT64_C(0x100000005), other_owner = UINT64_C(0x200000005);
    CHECK(qa_audio_mixer_create(&position_options, &positions, &error));
    CHECK(qa_audio_mixer_position(positions, actor, qa_v3(1, 0, 0), &error));
    CHECK(qa_audio_mixer_position(positions, other, qa_v3(2, 0, 0), &error));
    CHECK(qa_audio_mixer_position_owner(positions, actor, owner, qa_v3(3, 0, 0), &error));
    CHECK(qa_audio_mixer_position_owner(positions, actor, other_owner, qa_v3(4, 0, 0), &error));
    CHECK(qa_audio_positions_read(positions->positions, &positions->position_projection, actor, owner, true).x == 3);
    CHECK(qa_audio_positions_read(positions->positions, &positions->position_projection, actor, other_owner, true).x == 4);
    CHECK(qa_audio_positions_read(positions->positions, &positions->position_projection, other, owner, true).x == 2);
    CHECK(qa_audio_mixer_position(positions, actor, qa_v3(5, 0, 0), &error));
    CHECK(qa_audio_positions_read(positions->positions, &positions->position_projection, actor, owner, true).x == 5);
    CHECK(qa_audio_mixer_position_owner(positions, actor, QA_AUDIO_NO_OWNER, qa_v3(6, 0, 0), &error));
    CHECK(qa_audio_positions_read(positions->positions, &positions->position_projection, actor, owner, true).x == 5);
    CHECK(qa_audio_positions_read(positions->positions, &positions->position_projection, actor, owner, false).x == 6);
    qa_audio_mixer_stop_actor(positions, actor, owner);
    CHECK(qa_audio_positions_read(positions->positions, &positions->position_projection, actor, owner, true).x == 5);
    qa_audio_mixer_stop_all(positions);
    CHECK(qa_audio_positions_read(positions->positions, &positions->position_projection, actor, owner, true).x == 0);
    qa_audio_mixer_destroy(positions);
    qa_audio_positions shared = {0};
    qa_audio_mixer *seats[3] = {0};
    CHECK(qa_audio_positions_prepare(&shared, NULL, 8, false, &error));
    CHECK(qa_audio_mixer_create_positions(&position_options, &shared, &seats[0], &error));
    CHECK(qa_audio_mixer_create_positions(&position_options, &shared, &seats[1], &error));
    CHECK(qa_audio_positions_set(&shared, NULL, actor, QA_AUDIO_NO_OWNER, qa_v3(1, 0, 0), true, &error));
    CHECK(qa_audio_positions_read(&shared, &seats[0]->position_projection, actor, owner, true).x == 1);
    CHECK(qa_audio_mixer_position_owner(seats[0], actor, owner, qa_v3(2, 0, 0), &error));
    CHECK(qa_audio_positions_read(&shared, &seats[0]->position_projection, actor, owner, true).x == 2);
    CHECK(qa_audio_positions_read(&shared, &seats[1]->position_projection, actor, owner, true).x == 1);
    CHECK(qa_audio_mixer_position(seats[1], actor, qa_v3(3, 0, 0), &error));
    CHECK(qa_audio_positions_read(&shared, &seats[0]->position_projection, actor, owner, true).x == 2);
    CHECK(qa_audio_positions_read(&shared, &seats[1]->position_projection, actor, owner, true).x == 3);
    CHECK(qa_audio_positions_set(&shared, NULL, actor, QA_AUDIO_NO_OWNER, qa_v3(4, 0, 0), true, &error));
    CHECK(qa_audio_positions_read(&shared, &seats[0]->position_projection, actor, owner, true).x == 4);
    qa_audio_mixer_stop_all(seats[0]);
    CHECK(qa_audio_positions_read(&shared, &seats[0]->position_projection, actor, owner, true).x == 0);
    CHECK(qa_audio_positions_read(&shared, &seats[1]->position_projection, actor, owner, true).x == 4);
    CHECK(qa_audio_mixer_create_positions(&position_options, &shared, &seats[2], &error));
    CHECK(qa_audio_positions_read(&shared, &seats[2]->position_projection, actor, owner, true).x == 4);
    CHECK(qa_audio_positions_set(&shared, NULL, actor, QA_AUDIO_NO_OWNER, qa_v3(5, 0, 0), true, &error));
    CHECK(qa_audio_positions_read(&shared, &seats[0]->position_projection, actor, owner, true).x == 5);
    CHECK(qa_audio_positions_set(&shared, NULL, other, QA_AUDIO_NO_OWNER, qa_v3(9, 0, 0), true, &error));
    uint64_t revision = shared.revision;
    qa_audio_positions_retain(&shared, audio_position_not_actor, &actor);
    CHECK(shared.count == 1 && shared.revision == revision);
    for (size_t seat = 0; seat < 3; ++seat) {
        CHECK(qa_audio_positions_read(&shared, &seats[seat]->position_projection, actor, owner, true).x == 0);
        CHECK(qa_audio_positions_read(&shared, &seats[seat]->position_projection, other, owner, true).x == 9);
    }
    for (size_t seat = 0; seat < 3; ++seat) qa_audio_mixer_destroy(seats[seat]);
    qa_audio_positions_destroy(&shared);
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
        uint64_t resource_id = qa_resource_id(qa_audio_asset_resource(asset));
        CHECK(qa_audio_bank_get(bank, resource_id, families[i]) == asset);
        CHECK(!qa_audio_bank_get(bank, resource_id, (qa_game_family)-1));
        CHECK(!qa_audio_bank_get(bank, resource_id ^ UINT64_C(0x100000000), families[i]));
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
        qa_audio_play sound = {.sample = qa_audio_asset_sample(asset), .asset = asset,
            .family = families[i], .actor = QA_AUDIO_NO_ACTOR, .owner = QA_AUDIO_NO_OWNER,
            .audience = QA_AUDIO_WORLD, .origin_kind = QA_AUDIO_LOCAL, .volume = 1};
        const size_t initial_voices[] = {0, 2, 8};
        for (size_t initial = 0; initial < sizeof(initial_voices) / sizeof(*initial_voices); ++initial) {
            qa_audio_mixer *mixer = NULL;
            audio_notification_counts counts = {0};
            qa_audio_mixer_options options = {.sample_rate = 44100, .output_channels = 2,
                .initial_voices = initial_voices[initial], .prepared_capacity = 1, .workspace_capacity = 8,
                .observer = count_audio_notification, .observer_user = &counts};
            CHECK(qa_audio_mixer_create(&options, &mixer, &error));
            for (size_t turn = 0; turn < 16; ++turn) {
                bool accepted = false;
                CHECK(qa_audio_mixer_play(mixer, &sound, 0, &accepted, &error) && accepted);
                CHECK(qa_audio_mixer_play(mixer, &sound, 100, &accepted, &error) && accepted);
                int16_t output[32]; CHECK(qa_audio_mixer_mix(mixer, output, 16, &error));
                qa_audio_mixer_stop_all(mixer);
            }
            CHECK(counts.starts && counts.starts == counts.stops);
            qa_audio_mixer_destroy(mixer);
        }
        qa_audio_engine *engine=NULL;
        audio_notification_counts counts={0};
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
        qa_audio_event delayed = event;
        delayed.sound.actor = actor;
        delayed.sound.origin_kind = QA_AUDIO_ACTOR;
        delayed.sound.origin_actor = other;
        delayed.sound.delay_seconds = 1;
        CHECK(qa_audio_engine_apply(engine, &delayed, &error));
        audio_actor_roots roots = {.sound = actor, .origin = other};
        qa_audio_engine_actors_visit(engine, audio_actor_root, &roots);
        CHECK(roots.found == 3);
        qa_audio_engine_stop_all(engine);
        roots.found = 0;
        qa_audio_engine_actors_visit(engine, audio_actor_root, &roots);
        CHECK(roots.found == 0);
        qa_audio_engine_destroy(engine);CHECK(counts.starts==counts.stops);
        const int16_t leased_pcm[] = {1200, -1200, 600, -600};
        qa_audio_sample *leased_sample = NULL;
        CHECK(qa_audio_sample_copy(leased_pcm, 4, 1, 44100,
            QA_AUDIO_NO_LOOP, &leased_sample, &error));
        uint64_t sample_event = qa_application_events_next(&app);
        CHECK(qa_application_audio_emit(&app, &(qa_audio_event){.kind = QA_AUDIO_EVENT_PLAY,
            .sound = {.sample = leased_sample}}, &error));
        qa_event_lease *sample_lease = qa_application_event_retain(&app, sample_event);
        CHECK(sample_lease != NULL);
        qa_audio_sample_release(leased_sample);
        application_event_stream_dispose(&app);free(app.unified_persistent);
        const application_event_envelope *sample_record = qa_event_lease_record(sample_lease);
        CHECK(sample_record->kind == QA_APPLICATION_EVENT_AUDIO);
        CHECK(sample_record->raw.audio.sound.sample->frame_count == 4);
        CHECK(!memcmp(sample_record->raw.audio.sound.sample->samples, leased_pcm, sizeof(leased_pcm)));
        qa_event_lease_release(sample_lease);
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
    qa_audio_bank_begin(bank);
    qa_audio_asset *kept = NULL;
    CHECK(qa_audio_bank_register(bank, "optional.wav", QA_GAME_Q2, &kept, &error) && kept);
    uint64_t kept_id = qa_resource_id(qa_audio_asset_resource(kept));
    qa_audio_bank_end(bank);
    CHECK(qa_audio_bank_get(bank, kept_id, QA_GAME_Q2) == kept);
    CHECK(!qa_audio_bank_get(bank, kept_id, QA_GAME_Q1));
    qa_audio_bank_begin(bank); qa_audio_bank_end(bank);
    CHECK(!qa_audio_bank_get(bank, kept_id, QA_GAME_Q2));
    CHECK(qa_audio_asset_sample(kept)->frame_count != 0);
    qa_audio_asset_release(kept);
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

static int32_t font_export_image(void *context,const qa_scene_image *image)
{ (void)context; (void)image; return 37; }
static void font_export_warning(void *context,const char *message)
{ (void)message; ++*(unsigned *)context; }
static void test_q3_font_export(const char *executable)
{
    qa_error error={0};
    const char *slash=strrchr(executable,'/');
    char directory[4096];
    int length=slash?snprintf(directory,sizeof(directory),"%.*s/engine-data/fonts",
        (int)(slash-executable),executable):snprintf(directory,sizeof(directory),"engine-data/fonts");
    CHECK(length>0 && (size_t)length<sizeof(directory));
    qa_resource_pool *pool=qa_resource_pool_create(&error); CHECK(pool);
    qa_vfs *files=qa_vfs_create(pool,&error); CHECK(files);
    qa_mount_id mount;
    CHECK(qa_vfs_mount_directory(files,directory,QA_ARCHIVE_EXACT,false,&mount,&error));
    qa_scene_resources *images=qa_scene_resources_create(files,&error); CHECK(images);
    qa_q3_image_upload_options upload={.color={.device={.color_bits=24},.gamma=1,.intensity=1}};
    CHECK(qa_scene_resources_source_q3_initialize(images,&upload,&error));
    qa_font_library *fonts=qa_font_library_create(files,images,&error); CHECK(fonts);
    qa_font_q3_options options={.point_size=16,.truetype_path="DejaVuSans.ttf",.generate_if_missing=true};
    const qa_font *font=NULL,*cached=NULL;
    CHECK(qa_font_q3_register(fonts,&options,&font,&error));
    qa_fs_root *root=NULL; CHECK(qa_fs_root_temporary_create(&root,&error));
    unsigned warnings=0;
    qa_font_q3_save_options save={.root=root,.context=&warnings,.warning=font_export_warning};
    uint8_t record[QA_Q3_FONT_RECORD_BYTES];
    CHECK(qa_font_q3_export(font,font_export_image,NULL,&save,record,&error));
    qa_fs_entry_kind kind;
    CHECK(qa_fs_root_status(root,"fonts/fontImage_16.dat",&kind,NULL,&error) && kind==QA_FS_MISSING);
    CHECK(qa_font_q3_register(fonts,&options,&cached,&error) && cached==font);
    save.enabled=true;
    CHECK(qa_font_q3_export(cached,font_export_image,NULL,&save,record,&error) && !warnings);
    qa_fs_file *saved=NULL; qa_fs_identity identity; qa_buffer bytes={0};
    CHECK(qa_fs_root_file_open(root,"fonts/fontImage_16.dat",&saved,&identity,&error));
    CHECK(qa_fs_file_read_snapshot(saved,&identity,&bytes,&error)); qa_fs_file_close(saved);
    CHECK(bytes.size==sizeof(record) && !memcmp(bytes.data,record,sizeof(record)));
    qa_q3_font_record decoded;
    CHECK(qa_q3_font_record_decode((qa_bytes){bytes.data,bytes.size},&decoded,&error));
    CHECK(decoded.glyphs['A'].handle==37 && decoded.glyphs['A'].shader_name[0]);
    qa_buffer_free(&bytes);
    CHECK(qa_fs_root_file_open(root,decoded.glyphs['A'].shader_name,&saved,&identity,&error));
    CHECK(qa_fs_file_read_snapshot(saved,&identity,&bytes,&error)); qa_fs_file_close(saved);
    qa_image atlas={0};
    CHECK(qa_image_decode_tga((qa_bytes){bytes.data,bytes.size},QA_IMAGE_FORMAT,&atlas,&error));
    qa_font_glyph glyph; CHECK(qa_font_find_glyph(font,'A',&glyph));
    CHECK(atlas.width==glyph.image->levels[0].width && atlas.height==glyph.image->levels[0].height &&
        atlas.rgba.size==glyph.image->levels[0].bytes &&
        !memcmp(atlas.rgba.data,glyph.image->levels[0].pixels,atlas.rgba.size));
    qa_image_free(&atlas); qa_buffer_free(&bytes);
    save.root=NULL;
    CHECK(qa_font_q3_export(font,font_export_image,NULL,&save,record,&error) && warnings==1);
    qa_font_q3_options unavailable={.point_size=17,.truetype_path="missing-font.ttf",
        .generate_if_missing=true,.context=&warnings,.warning=font_export_warning};
    const qa_font *missing=font;
    CHECK(qa_font_q3_register(fonts,&unavailable,&missing,&error) && !missing && warnings==2);
    unavailable.truetype_path="LICENSE-DejaVu.txt";
    missing=font;
    CHECK(qa_font_q3_register(fonts,&unavailable,&missing,&error) && !missing && warnings==3);
    CHECK(qa_font_find_glyph(font,'A',&glyph) && glyph.visible);
    qa_font_library_destroy(fonts); qa_scene_resources_destroy(images);
    qa_vfs_destroy(files); qa_resource_pool_destroy(pool);
    CHECK(qa_fs_root_temporary_dispose(&root,&error));
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
    const char *names[] = {"disabled_outer", "disabled_inner", "disabled_sibling",
        "outer", "inner", "sibling", "last_request"};
    qa_string_id labels[7];
    CHECK(qa_profiler_prepare(profiler, names, 7, 2, labels, &error));
    CHECK(qa_profiler_push(profiler, labels[0], &error));
    CHECK(qa_profiler_push(profiler, labels[1], &error));
    CHECK(qa_profiler_enable(profiler, true, &error));
    CHECK(!qa_profiler_enabled(profiler) && qa_profiler_idle(profiler));
    CHECK(qa_profiler_pop(profiler, &error));
    CHECK(!qa_profiler_enabled(profiler));
    CHECK(qa_profiler_push(profiler, labels[2], &error));
    CHECK(qa_profiler_pop(profiler, &error));
    CHECK(!qa_profiler_enabled(profiler) && clock.calls == 1);
    CHECK(qa_profiler_pop(profiler, &error));
    CHECK(qa_profiler_enabled(profiler) && qa_profiler_idle(profiler));

    clock.now = 1;
    CHECK(qa_profiler_push(profiler, labels[3], &error));
    clock.now = 2;
    CHECK(qa_profiler_push(profiler, labels[4], &error));
    CHECK(qa_profiler_enable(profiler, false, &error));
    CHECK(qa_profiler_enabled(profiler) && !qa_profiler_idle(profiler));
    clock.now = 5;
    CHECK(qa_profiler_pop(profiler, &error));
    CHECK(qa_profiler_enabled(profiler));
    clock.now = 6;
    CHECK(qa_profiler_push(profiler, labels[5], &error));
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
    CHECK(qa_profiler_push(profiler, labels[6], &error));
    CHECK(qa_profiler_enable(profiler, false, &error));
    CHECK(qa_profiler_enable(profiler, true, &error));
    CHECK(qa_profiler_pop(profiler, &error));
    CHECK(qa_profiler_enabled(profiler));
    CHECK(qa_profiler_reset(profiler, &error));
    CHECK(qa_profiler_push(profiler, labels[3], &error));
    clock.now += 2;
    CHECK(qa_profiler_pop(profiler, &error));
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
    CHECK(qa_cvars_revision(engine)!=0 && qa_cvars_revision(client)!=0);
    qa_cvars_edit *edit=NULL;
    CHECK(qa_cvars_edit_prepare(engine,&edit,&error));
    CHECK(qa_cvars_revision(engine)==0 && qa_cvars_revision(client)==0);
    qa_cvar_archive_entry entry={.name="fov",.value="110"};
    qa_cvar_archive archive={.entries=&entry,.count=1};
    CHECK(qa_cvar_archive_apply(client,&archive,&error));
    CHECK(qa_cvars_find(engine,"cg_fov")->number==110 && qa_cvars_find(client,"fov")->number==110);
    qa_cvars_edit_abort(edit); edit=NULL;
    CHECK(qa_cvars_revision(engine)!=0 && qa_cvars_revision(client)!=0);
    CHECK(qa_cvars_find(engine,"fov")->number==original && qa_cvars_find(client,"cg_fov")->number==original);
    CHECK(qa_cvars_edit_prepare(engine,&edit,&error));
    entry.value="120";
    CHECK(qa_cvar_archive_apply(client,&archive,&error) && qa_cvars_edit_ready(edit,&error));
    qa_cvars_edit_publish(edit);
    CHECK(qa_cvars_edit_finish(engine,&error));
    CHECK(qa_cvars_revision(engine)!=0 && qa_cvars_revision(client)!=0);
    CHECK(qa_cvars_find(engine,"fov")->number==120 && qa_cvars_find(client,"cg_fov")->number==120);
    qa_cvars_destroy(client); qa_cvars_destroy(engine);
}

static void test_cvar_info_storage(void)
{
    uint8_t color=7;
    CHECK(qa_text_q3_color_escape('^','a',&color) && color==1);
    CHECK(qa_text_q3_color_escape('^','z',&color) && color==2);
    CHECK(qa_text_q3_color_escape('^','9',&color) && color==1);
    CHECK(qa_text_q3_color_escape('^',0xe9,&color) && color==1);
    CHECK(!qa_text_q3_color_escape('^','^',&color) && color==1);
    CHECK(!qa_text_q3_color_escape('^',0,&color) && color==1);
    CHECK(!qa_text_q3_color_escape('x','1',&color) && color==1);
    qa_error color_error={0};
    qa_console_buffer *color_buffer=qa_console_buffer_create(QA_RULESET_Q3,32,128,&color_error);
    CHECK(color_buffer);
    static const uint8_t colored[]="^aA^zB^9C^";
    CHECK(qa_console_buffer_print(color_buffer,(qa_bytes){colored,sizeof colored-1},0,&color_error));
    qa_console_row color_row;
    CHECK(qa_console_buffer_row(color_buffer,qa_console_buffer_count(color_buffer)-1,&color_row));
    CHECK(color_row.count==4 && color_row.cells[0].scalar=='A' && color_row.cells[0].color==1);
    CHECK(color_row.cells[1].scalar=='B' && color_row.cells[1].color==2);
    CHECK(color_row.cells[2].scalar=='C' && color_row.cells[2].color==1);
    CHECK(color_row.cells[3].scalar=='^' && color_row.cells[3].color==1);
    qa_console_buffer_clear(color_buffer);
    qa_console_buffer_dialect(color_buffer,QA_RULESET_Q2_RERELEASE);
    static const uint8_t q2_colored[]="^aA^9B";
    CHECK(qa_console_buffer_print(color_buffer,(qa_bytes){q2_colored,sizeof q2_colored-1},0,&color_error));
    CHECK(qa_console_buffer_row(color_buffer,qa_console_buffer_count(color_buffer)-1,&color_row));
    CHECK(color_row.count==4 && color_row.cells[0].scalar=='^' && color_row.cells[3].scalar=='B' && color_row.cells[3].color==1);
    qa_console_buffer_destroy(color_buffer);
    const uint8_t upper[]={'A',0,0xc1},lower[]={'a',0,0xc1},other[]={'a',0,0xe1};
    CHECK(qa_text_bytes_equal((qa_bytes){upper,sizeof upper},(qa_bytes){lower,sizeof lower},true));
    CHECK(!qa_text_bytes_equal((qa_bytes){upper,sizeof upper},(qa_bytes){lower,sizeof lower},false));
    CHECK(!qa_text_bytes_equal((qa_bytes){upper,sizeof upper},(qa_bytes){other,sizeof other},true));
    CHECK(!qa_text_bytes_compare((qa_bytes){upper,sizeof upper},(qa_bytes){lower,sizeof lower},true));
    CHECK(qa_text_bytes_compare((qa_bytes){upper,sizeof upper},(qa_bytes){lower,sizeof lower},false)<0);
    CHECK(qa_text_bytes_compare((qa_bytes){lower,sizeof lower},(qa_bytes){other,sizeof other},true)<0);
    CHECK(qa_text_bytes_compare((qa_bytes){lower,2},(qa_bytes){lower,sizeof lower},true)<0);
    CHECK(!qa_text_bytes_compare((qa_bytes){0},(qa_bytes){0},true));
    CHECK(qa_text_bytes_prefix((qa_bytes){upper,sizeof upper},(qa_bytes){lower,2},true));
    CHECK(!qa_text_bytes_prefix((qa_bytes){upper,sizeof upper},(qa_bytes){lower,2},false));
    CHECK(!qa_text_bytes_prefix((qa_bytes){upper,2},(qa_bytes){lower,sizeof lower},true));
    CHECK(qa_text_bytes_prefix((qa_bytes){upper,sizeof upper},(qa_bytes){0},true));
    CHECK(!qa_text_ascii_compare("Alpha","ALPHA"));
    CHECK(qa_text_ascii_compare("name","name_extra")<0);
    CHECK(qa_text_ascii_equal((const char *)upper,(const char *)other));
    CHECK(!qa_text_ascii_equal("command","command_extra"));
    const char *text="\\name\\first\\Name\\folded\\name\\last\\empty\\\\orphan";
    qa_bytes key={(const uint8_t *)"name",4},info_span;
    CHECK(qa_info_find(text,key,0,&info_span) && info_span.size==5 && !memcmp(info_span.data,"first",5));
    CHECK(qa_info_find(text,key,QA_INFO_LAST,&info_span) && info_span.size==4 && !memcmp(info_span.data,"last",4));
    CHECK(qa_info_find("Name\\folded",key,QA_INFO_CASE_FOLD,&info_span) && info_span.size==6 && !memcmp(info_span.data,"folded",6));
    CHECK(qa_info_find(text,(qa_bytes){(const uint8_t *)"empty",5},0,&info_span) && info_span.size==0);
    CHECK(!qa_info_find(text,(qa_bytes){(const uint8_t *)"orphan",6},0,&info_span) && info_span.size==0);
    qa_error info_error={0};char too_small[4];
    char raw_info[32] = ""; size_t raw_length = 0;
    CHECK(qa_info_append(raw_info, sizeof(raw_info), &raw_length,
        (qa_bytes){(const uint8_t *)"name", 4}, (qa_bytes){(const uint8_t *)"a\\b", 3}, 0, &info_error));
    CHECK(qa_info_append(raw_info, sizeof(raw_info), &raw_length,
        (qa_bytes){(const uint8_t *)"empty", 5}, (qa_bytes){0}, 0, &info_error));
    CHECK(!strcmp(raw_info, "\\name\\a\\b\\empty\\") && raw_length == 16);
    CHECK(!qa_info_append(raw_info, raw_length + 1, &raw_length,
        (qa_bytes){(const uint8_t *)"x", 1}, (qa_bytes){(const uint8_t *)"y", 1}, 0, &info_error));
    CHECK(!strcmp(raw_info, "\\name\\a\\b\\empty\\") && raw_length == 16);
    CHECK(!qa_q3_info_value(text,"NAME",too_small,sizeof(too_small),&info_error));
    char info[1024]="\\k\\first\\k\\second"; const char *warning;
    CHECK(qa_q3_info_set(info,sizeof(info),"k","third",QA_Q3_INFO_BROWSER,&warning,&info_error));
    CHECK(!warning && !strcmp(info,"\\k\\third\\k\\second"));
    CHECK(qa_q3_info_set(info,sizeof(info),"k","",QA_Q3_INFO_BROWSER,&warning,&info_error));
    CHECK(!warning && !strcmp(info,"\\k\\second"));
    CHECK(qa_q3_info_set(info,sizeof(info),"bad;key","value",QA_Q3_INFO_BROWSER,&warning,&info_error));
    CHECK(warning && !strcmp(info,"\\k\\second"));
    char oversized[1024];memset(oversized,'x',sizeof(oversized)-1);oversized[sizeof(oversized)-1]=0;
    strcpy(info,"\\k\\old\\keep\\1");
    CHECK(qa_q3_info_set(info,sizeof(info),"k",oversized,QA_Q3_INFO_BROWSER,&warning,&info_error));
    CHECK(warning && !strcmp(info,"\\keep\\1"));
    strcpy(info,"\\k\\old\\");
    CHECK(!qa_q3_info_set(info,sizeof(info),"k",oversized,QA_Q3_INFO_STRICT,NULL,&info_error));
    CHECK(!strcmp(info,"\\k\\old\\"));
    CHECK(!qa_q3_info_set(info,sizeof(info),"k",oversized,QA_Q3_INFO_BROWSER,&warning,&info_error));
    CHECK(!warning && !strcmp(info,"\\"));
    char big_info[8192]="\\keep\\1";
    CHECK(qa_q3_info_set(big_info,sizeof(big_info),"k","last",QA_Q3_INFO_STRICT,NULL,&info_error));
    CHECK(!strcmp(big_info,"\\keep\\1\\k\\last"));
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

static void test_common_compression(void)
{
    qa_error error={0};
    const uint8_t input[]="a/*discard\nthis*/b \t\r\n c //discard\n\"// quoted\t/* text */\" \201  ";
    const uint8_t expected[]="ab\nc\n\"// quoted\t/* text */\" \201";
    uint8_t output[sizeof(input)],in_place[sizeof(input)];size_t written=SIZE_MAX;
    CHECK(qa_common_compress((qa_bytes){input,sizeof(input)-1},QA_COMMON_TERMINATED,
        (qa_buffer){output,sizeof(output)},&written,&error));
    CHECK(written==sizeof(expected)-1 && !memcmp(output,expected,sizeof(expected)));
    memcpy(in_place,input,sizeof(input));
    CHECK(qa_common_compress((qa_bytes){in_place,sizeof(in_place)-1},QA_COMMON_TERMINATED,
        (qa_buffer){in_place,sizeof(in_place)},&written,&error));
    CHECK(written==sizeof(expected)-1 && !memcmp(in_place,expected,sizeof(expected)));
    const uint8_t terminated[]={'x',0,'/','*'};
    CHECK(qa_common_compress((qa_bytes){terminated,sizeof(terminated)},QA_COMMON_UNINITIALIZED,
        (qa_buffer){output,2},&written,&error) && written==1 && output[0]=='x' && !output[1]);
    written=SIZE_MAX;
    CHECK(!qa_common_compress((qa_bytes){(const uint8_t *)"x",1},QA_COMMON_UNINITIALIZED,
        (qa_buffer){output,sizeof(output)},&written,&error) && written==SIZE_MAX);
    CHECK(!qa_common_compress((qa_bytes){input,sizeof(input)-1},QA_COMMON_TERMINATED,
        (qa_buffer){output,1},&written,&error) && written==SIZE_MAX);
}

static void test_medium_coverage(void)
{
    qa_collision_medium_interval parts[4]={{.5f,1,false},{0,.5f,true}},solid[4];
    qa_trace_result result={.fraction=1};
    qa_collision_medium_coverage(parts,2,solid,&result);
    CHECK(!result.in_open && result.in_water);
    parts[0]=(qa_collision_medium_interval){0,1,false};
    parts[1]=(qa_collision_medium_interval){0,.5f,true};
    parts[2]=(qa_collision_medium_interval){.5f,1,true};
    qa_collision_medium_coverage(parts,3,solid,&result);
    CHECK(!result.in_open && !result.in_water);
    result.fraction=0;
    parts[0]=(qa_collision_medium_interval){0,0,false};
    qa_collision_medium_coverage(parts,1,solid,&result);
    CHECK(!result.in_open && result.in_water);
    parts[1]=(qa_collision_medium_interval){0,0,true};
    qa_collision_medium_coverage(parts,2,solid,&result);
    CHECK(!result.in_open && !result.in_water);
    qa_collision_medium_coverage(NULL,0,NULL,&result);
    CHECK(result.in_open && !result.in_water);
}

static void test_qw_shared_output(void)
{
    qa_error error={0};qa_net_protocol_id protocol={.kind=QA_NET_QW28};
    qa_qw_decoder *decoder=qa_qw_decoder_create(protocol,&error);
    qa_qw_source_history *history=qa_qw_source_history_create(&error);
    CHECK(decoder && history);
    qa_qw_source_entity baseline={.number=1,.model=1};
    CHECK(qa_qw_source_history_baselines(history,&baseline,1,&error));
    qa_q1_entity original;qa_q1_entity_init(&original);original.number=1;original.model=1;
    CHECK(qa_qw_decoder_set_baseline(decoder,&original,&error));
    qa_qw_source_frame frame={.sequence=1,.count=1};frame.entities[0]=baseline;frame.entities[0].solid=true;
    uint8_t bytes[128];qa_net_writer writer;qa_net_reader reader;qa_qw_service received;
    qa_net_writer_init(&writer,bytes,sizeof(bytes),&error);
    CHECK(qa_qw_source_write_entities(&writer,history,&frame,NULL));
    qa_net_reader_init(&reader,(qa_bytes){bytes,qa_net_writer_size(&writer)},&error);
    CHECK(qa_qw_service_read(&reader,decoder,1,&received) && qa_net_reader_finish(&reader));
    CHECK(received.kind==QA_QW_PACKET_ENTITIES && received.data.packet.frame->count==1);
    CHECK(received.data.packet.frame->entities[0].qw_flags&QA_QW_ENTITY_SOLID);
    CHECK(qa_qw_source_history_store(history,&frame,&error));
    frame.sequence=2;frame.entities[0].solid=false;
    qa_net_writer_init(&writer,bytes,sizeof(bytes),&error);
    CHECK(qa_qw_source_write_entities(&writer,history,&frame,qa_qw_source_history_frame(history,1)));
    const uint8_t clear[]={48,1,1,0,0,0};
    CHECK(qa_net_writer_size(&writer)==sizeof(clear) && !memcmp(bytes,clear,sizeof(clear)));
    qa_qw_decoder_delta_request(decoder,2,true,1);
    qa_net_reader_init(&reader,(qa_bytes){bytes,qa_net_writer_size(&writer)},&error);
    CHECK(qa_qw_service_read(&reader,decoder,2,&received) && qa_net_reader_finish(&reader));
    CHECK(!(received.data.packet.frame->entities[0].qw_flags&QA_QW_ENTITY_SOLID));
    CHECK(qa_qw_source_history_store(history,&frame,&error));
    frame.sequence=3;frame.entities[0].model=257;
    qa_net_writer_init(&writer,bytes,sizeof(bytes),&error);
    CHECK(qa_qw_source_write_entities(&writer,history,&frame,qa_qw_source_history_frame(history,2)));
    const uint8_t changed[]={48,2,1,128,4,1,0,0};
    CHECK(qa_net_writer_size(&writer)==sizeof(changed) && !memcmp(bytes,changed,sizeof(changed)));
    CHECK(qa_qw_source_history_store(history,&frame,&error));
    frame.sequence=4;frame.entities[0].solid=true;
    qa_net_writer_init(&writer,bytes,sizeof(bytes),&error);
    CHECK(qa_qw_source_write_entities(&writer,history,&frame,qa_qw_source_history_frame(history,3)));
    const uint8_t set[]={48,3,1,128,64,0,0};
    CHECK(qa_net_writer_size(&writer)==sizeof(set) && !memcmp(bytes,set,sizeof(set)));
    qa_qw_source_history_destroy(history);qa_qw_decoder_destroy(decoder);
    protocol.kind=QA_NET_QW29;decoder=qa_qw_decoder_create(protocol,&error);CHECK(decoder);
    qa_qw_frame extended={.sequence=1,.count=1};qa_q1_entity_init(extended.entities);
    extended.entities[0].number=513;extended.entities[0].model=300;extended.entities[0].frame=301;
    extended.entities[0].alpha=128;extended.entities[0].scale=24;
    qa_qw_service packet={.kind=QA_QW_PACKET_ENTITIES,.data.packet.frame=&extended};
    qa_net_writer_init(&writer,bytes,sizeof(bytes),&error);
    CHECK(qa_qw_service_write(&writer,protocol,&packet,decoder));
    qa_net_reader_init(&reader,(qa_bytes){bytes,qa_net_writer_size(&writer)},&error);
    CHECK(qa_qw_service_read(&reader,decoder,1,&received) && qa_net_reader_finish(&reader));
    const qa_q1_entity *row=received.data.packet.frame->entities;
    CHECK(received.data.packet.frame->count==1 && row->number==513 && row->model==300 && row->frame==301);
    CHECK(row->alpha==128 && row->scale==24);
    qa_qw_decoder_destroy(decoder);
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

static void test_dedicated_line_storage(void)
{
    qa_error error={0};
    qa_dedicated_console *first=qa_dedicated_console_create(&error),
        *restored=qa_dedicated_console_create(&error);
    CHECK(first && restored);
    const uint8_t text[]={'a','\r','\n',0xc3,0xa9,0xff};
    CHECK(qa_dedicated_console_feed(first,(qa_bytes){text,sizeof text},true,&error));
    qa_bytes line,again;bool present;
    CHECK(qa_dedicated_console_line_peek(first,&line,&present,&error) && present);
    CHECK(line.size==3 && !memcmp(line.data,"a\n",3));
    qa_dedicated_console_line_commit(first);
    qa_buffer saved={0},copy={0};
    CHECK(qa_dedicated_console_checkpoint(first,&saved,&error));
    CHECK(qa_dedicated_console_restore(restored,(qa_bytes){saved.data,saved.size},&error));
    CHECK(qa_dedicated_console_checkpoint(restored,&copy,&error));
    CHECK(saved.size==copy.size && !memcmp(saved.data,copy.data,saved.size));
    const uint8_t expected[]={0xc3,0xa9,0xef,0xbf,0xbd,'\n',0};
    CHECK(qa_dedicated_console_line_peek(restored,&line,&present,&error) && present);
    CHECK(line.size==sizeof expected && !memcmp(line.data,expected,sizeof expected));
    CHECK(qa_dedicated_console_line_peek(restored,&again,&present,&error) && present);
    CHECK(again.data==line.data && again.size==line.size);
    qa_dedicated_console_line_commit(restored);
    CHECK(qa_dedicated_console_eof(restored));
    qa_buffer_free(&saved);qa_buffer_free(&copy);
    qa_dedicated_console_destroy(first);qa_dedicated_console_destroy(restored);

    first=qa_dedicated_console_create(&error);CHECK(first);
    uint8_t *invalid=malloc(QA_PLATFORM_EVENT_BYTE_CAPACITY);CHECK(invalid);
    memset(invalid,0xff,QA_PLATFORM_EVENT_BYTE_CAPACITY);
    CHECK(qa_dedicated_console_feed(first,(qa_bytes){invalid,QA_PLATFORM_EVENT_BYTE_CAPACITY},true,&error));
    CHECK(qa_dedicated_console_line_peek(first,&line,&present,&error) && present);
    CHECK(line.size==3u*QA_PLATFORM_EVENT_BYTE_CAPACITY+2u);
    for(size_t i=0;i<QA_PLATFORM_EVENT_BYTE_CAPACITY;++i)
        CHECK(!memcmp(line.data+i*3,expected+2,3));
    CHECK(line.data[line.size-2]=='\n' && line.data[line.size-1]==0);
    qa_dedicated_console_line_commit(first);CHECK(qa_dedicated_console_eof(first));
    free(invalid);qa_dedicated_console_destroy(first);
}

typedef struct client_input_receipt {
    int32_t codes[16],mouse_x,mouse_y,handling;
    bool down[16];
    size_t keys,mice,handlings;
} client_input_receipt;
static bool client_input_key(void *context,int32_t key,bool down,qa_error *error)
{
    client_input_receipt *receipt=context;(void)error;
    CHECK(receipt->keys<sizeof(receipt->codes)/sizeof(*receipt->codes));
    receipt->codes[receipt->keys]=key;receipt->down[receipt->keys++]=down;return true;
}
static bool client_input_mouse(void *context,int32_t x,int32_t y,qa_error *error)
{
    client_input_receipt *receipt=context;(void)error;
    receipt->mouse_x=x;receipt->mouse_y=y;++receipt->mice;return true;
}
static bool client_input_handling(void *context,int32_t type,qa_error *error)
{
    client_input_receipt *receipt=context;(void)error;
    receipt->handling=type;++receipt->handlings;return true;
}
static void test_client_input_sink(void)
{
    qa_error error={0};
    qa_cvars *cvars=qa_cvars_create(&(qa_cvar_options){.dialect=QA_RULESET_Q3,.side=QA_CVAR_SIDE_CLIENT},&error);
    CHECK(cvars);
    qa_command_context context={.dialect=QA_RULESET_Q3,.origin=QA_COMMAND_SEAT,.direct=true,
        .cvar_view=qa_cvars_view_identity(cvars)};
    qa_console *console=qa_console_create(&(qa_console_options){.context=context,.cvars=cvars,.disable_builtins=true},&error);
    CHECK(console);
    qa_input_seat *seat=qa_input_seat_create(&(qa_input_seat_options){.context=context,.console=console,
        .cvars=cvars,.gamepad=qa_gamepad_defaults()},&error);
    CHECK(seat);
    bool held[256]={0},handled=false;
    client_input_receipt receipt={.handling=-1};
    qa_input_client_sink sink={.seat=seat,.owner=37,.catcher=QA_INPUT_CATCH_GAME,.held_keys=&held,
        .context=&receipt,.key=client_input_key,.mouse=client_input_mouse,.handling=client_input_handling};
    CHECK(qa_input_q3_catcher_from_source(8)==QA_INPUT_CATCH_GAME);
    CHECK(qa_input_q3_catcher_to_source(QA_INPUT_CATCH_GAME)==8);
    CHECK(qa_input_q3_catcher_from_source(4)==QA_INPUT_CATCH_CHAT);
    CHECK(qa_input_q3_catcher_to_source(QA_INPUT_CATCH_CHAT)==4);
    CHECK(qa_input_q3_catcher_to_source(qa_input_q3_catcher_from_source(15))==15);
    CHECK(qa_input_seat_set_catcher(seat,37,QA_INPUT_CATCH_GAME,&error));
    qa_input_event event={.kind=QA_INPUT_EVENT_KEY,.input={.kind=QA_PHYSICAL_KEY,.code='a'},.down=true};
    CHECK(qa_input_client_event(&sink,&event,&handled,&error)&&handled&&held['a']);
    qa_input_seat_retire_catcher(seat,37);
    CHECK(qa_input_seat_set_focus(seat,QA_INPUT_CONSOLE,1,&error));
    event.down=false;
    CHECK(qa_input_client_event(&sink,&event,&handled,&error)&&handled&&!held['a']);
    CHECK(receipt.keys==2&&receipt.codes[0]=='a'&&receipt.down[0]&&!receipt.down[1]);
    event.down=true;
    CHECK(qa_input_client_event(&sink,&event,&handled,&error)&&!handled&&receipt.keys==2);
    CHECK(qa_input_seat_set_focus(seat,QA_INPUT_GAME,2,&error));
    CHECK(qa_input_seat_set_catcher(seat,37,QA_INPUT_CATCH_GAME,&error));
    event=(qa_input_event){.kind=QA_INPUT_EVENT_TEXT,.text="\xc3\xa9"};
    CHECK(qa_input_client_event(&sink,&event,&handled,&error)&&handled);
    CHECK(receipt.codes[2]==(0xe9|QA_KEY_CHAR_FLAG)&&receipt.down[2]);
    event=(qa_input_event){.kind=QA_INPUT_EVENT_WHEEL,.delta={0,2.5f}};
    CHECK(qa_input_client_event(&sink,&event,&handled,&error)&&handled&&receipt.keys==9);
    for(size_t i=3;i<9;++i)CHECK(receipt.codes[i]==QA_KEY_WHEEL_UP&&receipt.down[i]==(i%2==1));
    event=(qa_input_event){.kind=QA_INPUT_EVENT_MOUSE,.delta={1.9f,-2.9f}};
    CHECK(qa_input_client_event(&sink,&event,&handled,&error)&&handled);
    CHECK(receipt.mice==1&&receipt.mouse_x==1&&receipt.mouse_y==-2);
    event=(qa_input_event){.kind=QA_INPUT_EVENT_KEY,.input={.kind=QA_PHYSICAL_KEY,.code=QA_KEY_ESCAPE},.down=true};
    CHECK(qa_input_client_event(&sink,&event,&handled,&error)&&handled);
    CHECK(receipt.handlings==1&&receipt.handling==0&&receipt.keys==9);
    CHECK(!(qa_input_seat_catcher(seat,37)&QA_INPUT_CATCH_GAME));
    sink.catcher=QA_INPUT_CATCH_UI;sink.handling=NULL;
    CHECK(qa_input_seat_set_catcher(seat,37,QA_INPUT_CATCH_UI,&error));
    event.input.code=QA_KEY_LEFT;
    CHECK(qa_input_client_event(&sink,&event,&handled,&error)&&handled);
    event.repeat=true;
    CHECK(qa_input_client_event(&sink,&event,&handled,&error)&&handled);
    CHECK(receipt.keys==11&&receipt.codes[9]==QA_KEY_LEFT&&receipt.codes[10]==QA_KEY_LEFT);
    CHECK(qa_input_seat_set_focus(seat,QA_INPUT_CHAT,3,&error));
    event.down=false;
    CHECK(qa_input_client_event(&sink,&event,&handled,&error)&&handled&&!held[QA_KEY_LEFT]);
    qa_input_seat_destroy(seat);qa_console_destroy(console);qa_cvars_destroy(cvars);
}

static void test_shared_input_menu_defaults(void)
{
    qa_controller_selection a={.kind=QA_CONTROLLER_GUID,
        .guid="AABBCCDDEEFF00112233445566778899",.ordinal=4};
    qa_controller_selection b={.kind=QA_CONTROLLER_GUID,
        .guid="aabbccddeeff00112233445566778899",.ordinal=4};
    qa_controller_info device={.guid="aabbccddeeff00112233445566778899",
        .ordinal=4,.serial="Unit-1"};
    CHECK(qa_controller_selection_equal(&a,&b));
    CHECK(qa_controller_selection_matches_device(&a,&device));
    device.ordinal=5;
    CHECK(!qa_controller_selection_matches_device(&a,&device));
    b.ordinal=5;
    CHECK(!qa_controller_selection_equal(&a,&b));
    a.kind=b.kind=QA_CONTROLLER_SERIAL;
    a.serial=b.serial="Unit-1";
    CHECK(qa_controller_selection_equal(&a,&b));
    CHECK(qa_controller_selection_matches_device(&a,&device));
    b.serial="unit-1";
    CHECK(!qa_controller_selection_equal(&a,&b));
    device.serial=NULL;
    CHECK(!qa_controller_selection_matches_device(&a,&device));
    a.kind=b.kind=QA_CONTROLLER_AUTO;
    CHECK(qa_controller_selection_equal(&a,&b));
    CHECK(!qa_controller_selection_matches_device(&a,&device));
    b.kind=QA_CONTROLLER_NONE;
    CHECK(!qa_controller_selection_equal(&a,&b));
    a.kind=QA_CONTROLLER_NONE;
    CHECK(qa_controller_selection_equal(&a,&b));
    CHECK(!qa_controller_selection_matches_device(&a,&device));
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
        char held_command[768];
        memcpy(held_command,"+showscores ",12);
        memset(held_command+12,'a',sizeof held_command-13);
        held_command[sizeof held_command-1]=0;
        for (unsigned replacement=0;replacement<2;++replacement) {
            qa_physical_input physical={.kind=QA_PHYSICAL_KEY,.code=QA_KEY_TAB};
            CHECK(qa_input_seat_bind(seat,&(qa_input_binding){.input=physical,
                .kind=QA_BIND_COMMAND,.command=held_command},&error));
            qa_input_event key={.kind=QA_INPUT_EVENT_KEY,.time_ms=time+=10,
                .down=true,.input=physical};
            size_t drained=0;
            CHECK(qa_input_seat_event(seat,&key,NULL,&error));
            CHECK(qa_console_drain(console,4096,&drained,&error));
            CHECK(qa_input_seat_action_active(seat,QA_INPUT_SCORES));
            qa_input_binding next={.input=physical,.kind=QA_BIND_COMMAND,.command="+attack"};
            if (replacement==0) {
                qa_input_seat *candidate=NULL;
                CHECK(qa_input_seat_configuration_clone(seat,&candidate,&error));
                CHECK(qa_input_seat_bind(candidate,&next,&error));
                CHECK(qa_input_seat_configuration_ready(seat,candidate,&error));
                qa_input_seat_configuration_publish(seat,candidate);
                qa_input_seat_destroy(candidate);
            } else CHECK(qa_input_seat_replace_bindings(seat,&next,1,&error));
            key.down=false;key.time_ms=time+=10;
            CHECK(qa_input_seat_event(seat,&key,NULL,&error));
            CHECK(qa_console_drain(console,4096,&drained,&error));
            CHECK(!qa_input_seat_action_active(seat,QA_INPUT_SCORES));
            CHECK(!qa_input_seat_action_active(seat,QA_INPUT_ATTACK));
            key.down=true;key.time_ms=time+=10;
            CHECK(qa_input_seat_event(seat,&key,NULL,&error));
            CHECK(qa_console_drain(console,4096,&drained,&error));
            CHECK(qa_input_seat_action_active(seat,QA_INPUT_ATTACK));
            key.down=false;key.time_ms=time+=10;
            CHECK(qa_input_seat_event(seat,&key,NULL,&error));
            CHECK(qa_console_drain(console,4096,&drained,&error));
            CHECK(!qa_input_seat_action_active(seat,QA_INPUT_ATTACK));
        }
        CHECK(qa_input_seat_prepare_sources(seat, 8, &error));
        for (unsigned i=0;i<8;++i) {
            qa_physical_input physical={.kind=QA_PHYSICAL_KEY,.code=(uint16_t)('a'+i)};
            CHECK(qa_input_seat_bind(seat,&(qa_input_binding){.input=physical,
                .kind=QA_BIND_ACTION,.action=QA_INPUT_ATTACK},&error));
            qa_input_event key={.kind=QA_INPUT_EVENT_KEY,.time_ms=time+=10,
                .down=true,.input=physical};
            CHECK(qa_input_seat_event(seat,&key,NULL,&error));
            key.repeat=true;
            CHECK(qa_input_seat_event(seat,&key,NULL,&error));
        }
        for (unsigned i=0;i<8;++i) {
            qa_input_event key={.kind=QA_INPUT_EVENT_KEY,.time_ms=time+=10,
                .input={.kind=QA_PHYSICAL_KEY,.code=(uint16_t)('a'+i)}};
            CHECK(qa_input_seat_event(seat,&key,NULL,&error));
            CHECK(qa_input_seat_action_active(seat,QA_INPUT_ATTACK)==(i!=7));
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
    bool registered=false,consumed=false;
    CHECK(qa_net_dosbox_dispatch(network,&event,&registered,&consumed,&error) && registered && consumed);
    CHECK(qa_net_dosbox_bind(network,4000,4,&a,&error));
    CHECK(qa_net_dosbox_bind(network,4001,4,&b,&error));
    for(uint32_t i=0;i<256;++i)
        CHECK(qa_net_transport_send(a,qa_net_transport_address(b),(qa_bytes){(const uint8_t *)&i,4},&error)==QA_NET_SEND_ACCEPTED);
    uint32_t extra=256;
    CHECK(!qa_net_transport_send_ready(a,qa_net_transport_address(b)));
    CHECK(qa_net_transport_send(a,qa_net_transport_address(b),(qa_bytes){(const uint8_t *)&extra,4},&error)==QA_NET_SEND_FULL);
    qa_net_address broadcast=*qa_net_transport_address(b);memset(broadcast.host.ipx.node,255,6);
    CHECK(qa_net_transport_send(a,&broadcast,(qa_bytes){(const uint8_t *)&extra,4},&error)==QA_NET_SEND_FULL && sends==1);
    packet.to=*qa_net_transport_address(b);packet.from=*qa_net_transport_address(a);
    packet.payload=(qa_bytes){(const uint8_t *)&extra,4};
    qa_net_writer_init(&writer,bytes,sizeof(bytes),&error);CHECK(qa_net_ipx_encode(&packet,&writer));
    event.packet.payload=(qa_bytes){bytes,qa_net_writer_size(&writer)};event.packet.received_ns=1234;
    CHECK(!qa_net_dosbox_dispatch_ready(network,&event));
    CHECK(qa_net_dosbox_dispatch(network,&event,&registered,&consumed,&error) && !consumed);
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
    CHECK(qa_net_dosbox_dispatch_ready(network,&event));
    CHECK(qa_net_dosbox_dispatch(network,&event,&registered,&consumed,&error) && consumed);
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
static void test_kex_userinfo_projection(void)
{
    qa_error error = {0};
    char out[16385];
    const char *input = "\\name\\base\\name_1\\early\\rate\\25000\\name\\later\\name_1\\last\\name_0\\other";
    CHECK(qa_q2_kex_seat_userinfo(input, 1, out, sizeof(out), &error));
    CHECK(!strcmp(out, "\\name\\last\\rate\\25000"));
    CHECK(qa_q2_kex_seat_userinfo(input, 0, out, sizeof(out), &error));
    CHECK(!strcmp(out, "\\name\\other\\rate\\25000"));
    CHECK(qa_q2_kex_seat_userinfo(input, 7, out, sizeof(out), &error));
    CHECK(!strcmp(out, "\\name\\later\\rate\\25000"));
    CHECK(qa_q2_kex_client_userinfo("\\a\\first\\a\\second\\a_1\\seat", out, sizeof(out), &error));
    CHECK(!strcmp(out, "\\a\\first\\a\\second\\a_1\\seat\\a_0\\first\\a_0\\second"));
    CHECK(qa_q2_kex_client_userinfo("\\a\\base\\a_0\\chosen", out, sizeof(out), &error));
    CHECK(!strcmp(out, "\\a\\base\\a_0\\chosen"));
    CHECK(qa_q2_kex_seat_userinfo("\\a_18446744073709551616\\ordinary\\a_1\\selected", 1, out, sizeof(out), &error));
    CHECK(!strcmp(out, "\\a_18446744073709551616\\ordinary\\a\\selected"));
    CHECK(qa_q2_kex_seat_userinfo("\\", 0, out, sizeof(out), &error) && !out[0]);
    CHECK(qa_q2_kex_seat_userinfo("\\empty\\\\tail\\v\\", 0, out, sizeof(out), &error));
    CHECK(!strcmp(out, "\\empty\\\\tail\\v"));
    CHECK(!qa_q2_kex_seat_userinfo("name", 0, out, sizeof(out), &error));
    CHECK(!qa_q2_kex_seat_userinfo("\\\\bad", 0, out, sizeof(out), &error));
    CHECK(!qa_q2_kex_seat_userinfo("\\a\\b\\\\tail\\v", 0, out, sizeof(out), &error));
    CHECK(qa_q2_kex_seat_userinfo("\\a\\b", 0, out, 5, &error) && !strcmp(out, "\\a\\b"));
    CHECK(!qa_q2_kex_seat_userinfo("\\a\\b", 0, out, 4, &error));
    char many[257 * 4 + 1];
    for (size_t i = 0; i < 257; ++i) memcpy(many + i * 4, "\\k\\v", 4);
    many[256 * 4] = 0;
    CHECK(qa_q2_kex_seat_userinfo(many, 0, out, sizeof(out), &error) && !strcmp(out, "\\k\\v"));
    memcpy(many + 256 * 4, "\\k\\v", 5);
    CHECK(!qa_q2_kex_seat_userinfo(many, 0, out, sizeof(out), &error));
    char widths[1024 + 8193 + 3];
    widths[0] = '\\'; memset(widths + 1, 'k', 1023); widths[1024] = '\\';
    memset(widths + 1025, 'v', 8192); widths[9217] = 0;
    CHECK(qa_q2_kex_seat_userinfo(widths, 0, out, sizeof(out), &error));
    CHECK(strlen(out) == 9217 && !memcmp(out, widths, 9218));
    widths[9217] = 'v'; widths[9218] = 0;
    CHECK(!qa_q2_kex_seat_userinfo(widths, 0, out, sizeof(out), &error));
    widths[1024] = 'k'; widths[1025] = '\\'; widths[1026] = 0;
    CHECK(!qa_q2_kex_seat_userinfo(widths, 0, out, sizeof(out), &error));
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

static void test_provider_identity_views(void)
{
    qa_application app={0}; qa_error error={0};
    application_provider previous={.owner=17},replacement={.owner=17},other={.owner=18};
    CHECK(qa_id_index_reserve(&app.provider_index,3,&error));
    qa_id_index_insert(&app.provider_index,17,0,(size_t)(uintptr_t)&previous);
    qa_id_index_insert(&app.provider_index,18,0,(size_t)(uintptr_t)&other);
    qa_id_index_insert(&app.provider_index,17,0,(size_t)(uintptr_t)&replacement);
    application_provider *published[]={&previous,&other},*candidate[]={&replacement},
        *ambiguous[]={&previous,&replacement};
    CHECK(application_provider_roster_index(&app,published,2,&error));
    CHECK(application_provider_roster_index(&app,candidate,1,&error));
    CHECK(application_provider_roster_index(&app,ambiguous,2,&error));
    app.providers=published; app.provider_count=2;
    app.routing_providers=candidate; app.routing_provider_count=1;
    CHECK(application_provider_lookup(&app,17,APPLICATION_PROVIDER_LIVE)==&replacement);
    CHECK(application_provider_lookup(&app,17,APPLICATION_PROVIDER_PUBLISHED)==&previous);
    CHECK(application_provider_lookup_unique(&app,17,APPLICATION_PROVIDER_ROUTING)==&replacement);
    app.routing_providers=ambiguous; app.routing_provider_count=2;
    qa_id_index_cursor cursor=application_provider_lookup_begin(&app,17,APPLICATION_PROVIDER_ROUTING);
    CHECK(application_provider_lookup_next(&app,&cursor)==&previous);
    CHECK(application_provider_lookup_next(&app,&cursor)==&replacement);
    CHECK(!application_provider_lookup_next(&app,&cursor));
    CHECK(!application_provider_lookup_unique(&app,17,APPLICATION_PROVIDER_ROUTING));
    application_provider_roster_unindex(&app,ambiguous);
    CHECK(!application_provider_lookup(&app,17,APPLICATION_PROVIDER_ROUTING));
    CHECK(application_provider_lookup(&app,17,APPLICATION_PROVIDER_PUBLISHED)==&previous);
    application_provider_unindex(&app,&replacement);
    app.routing_providers=candidate; app.routing_provider_count=1;
    CHECK(!application_provider_lookup(&app,17,APPLICATION_PROVIDER_ROUTING));
    CHECK(application_provider_lookup(&app,17,APPLICATION_PROVIDER_LIVE)==&previous);
    application_provider_roster_unindex(&app,published);
    CHECK(!application_provider_lookup(&app,17,APPLICATION_PROVIDER_PUBLISHED));
    CHECK(application_provider_lookup(&app,17,APPLICATION_PROVIDER_LIVE)==&previous);
    CHECK(application_provider_lookup(&app,18,APPLICATION_PROVIDER_LIVE)==&other);
    qa_id_index_destroy(&app.provider_index);
}

static bool index_value_at_least(void *opaque, const qa_id_index_entry *entry)
{
    return entry->value >= *(const size_t *)opaque;
}

static void test_actor_index_lifetimes(void)
{
    qa_error error={0}; qa_actor_registry *registry=NULL; qa_actor_index index={0};
    qa_id_index candidates = {0}; size_t retained;
    size_t capacity = qa_id_index_capacity_for(129);
    CHECK(capacity >= 129 && !(capacity & (capacity - 1)));
    CHECK(!qa_id_index_capacity_for(SIZE_MAX));
    uint64_t identity = UINT64_C(0x100000007), other_identity = UINT64_C(0x200000007);
    CHECK(qa_id_index_reserve(&candidates, 4, &error));
    qa_id_index_insert(&candidates, 1, identity, 10);
    qa_id_index_insert(&candidates, 1, identity, 20);
    qa_id_index_insert(&candidates, 1, identity, 30);
    qa_id_index_insert(&candidates, 1, other_identity, 40);
    CHECK(qa_id_index_remove_value(&candidates, 1, identity, 20));
    qa_id_index_cursor cursor = qa_id_index_begin(&candidates, 1, identity);
    CHECK(qa_id_index_next(&candidates, &cursor, &retained) && retained == 30);
    CHECK(qa_id_index_next(&candidates, &cursor, &retained) && retained == 10);
    CHECK(!qa_id_index_next(&candidates, &cursor, &retained));
    CHECK(!qa_id_index_remove_value(&candidates, 1, identity, 99));
    CHECK(qa_id_index_remove(&candidates, 1, identity));
    cursor = qa_id_index_begin(&candidates, 1, other_identity);
    CHECK(qa_id_index_next(&candidates, &cursor, &retained) && retained == 40);
    qa_id_index_insert(&candidates, 1, identity, 10);
    qa_id_index_insert(&candidates, 1, identity, 20);
    qa_id_index_insert(&candidates, 1, identity, 30);
    qa_id_index_entry *entries = candidates.entries;
    size_t *buckets = candidates.buckets;
    size_t minimum = 20;
    CHECK(qa_id_index_retain(&candidates, index_value_at_least, &minimum) == 1);
    CHECK(candidates.entries == entries && candidates.buckets == buckets);
    cursor = qa_id_index_begin(&candidates, 1, identity);
    CHECK(qa_id_index_next(&candidates, &cursor, &retained) && retained == 30);
    CHECK(qa_id_index_next(&candidates, &cursor, &retained) && retained == 20);
    CHECK(!qa_id_index_next(&candidates, &cursor, &retained));
    cursor = qa_id_index_begin(&candidates, 1, other_identity);
    CHECK(qa_id_index_next(&candidates, &cursor, &retained) && retained == 40);
    CHECK(qa_id_index_retain(&candidates, index_value_at_least, &minimum) == 0);
    minimum = 50;
    CHECK(qa_id_index_retain(&candidates, index_value_at_least, &minimum) == 3);
    cursor = qa_id_index_begin(&candidates, 1, identity);
    CHECK(!qa_id_index_next(&candidates, &cursor, &retained));
    qa_id_index_destroy(&candidates);
    uint8_t bytes[16384]; qa_arena storage;
    CHECK(qa_arena_init_buffer(&storage,bytes,sizeof(bytes),&error));
    CHECK(qa_actors_create(2,NULL,NULL,&registry,&error));
    CHECK(qa_actor_index_create(&index,registry,&storage,&error));
    CHECK(qa_id_index_reserve(&index.retained,2,&error));
    qa_actor_id first,second,reused;
    CHECK(qa_actors_allocate(registry,1,1,&first,&error));
    CHECK(qa_actors_allocate(registry,1,1,&second,&error));
    qa_actor_index_put(&index,first,10); qa_actor_index_put(&index,second,20);
    CHECK(qa_actor_index_remember(&index,first,10,&error));
    CHECK(qa_actor_index_remember(&index,second,20,&error));
    CHECK(qa_actors_release(registry,first,&error));
    CHECK(qa_actors_allocate(registry,1,1,&reused,&error));
    CHECK(reused.slot==first.slot && reused.generation!=first.generation);
    qa_actor_index_put(&index,reused,30);
    CHECK(qa_actor_index_remember(&index,reused,30,&error));
    size_t value; qa_actor_id actor;
    CHECK(!qa_actor_index_find(&index,first,&value));
    CHECK(qa_actor_index_find(&index,reused,&value) && value==30);
    CHECK(qa_actor_index_find_retained(&index,first,&value) && value==10);
    qa_actor_index_forget_retained(&index,first);
    CHECK(!qa_actor_index_find_retained(&index,first,&value));
    CHECK(qa_actor_index_find_retained(&index,second,&value) && value==20);
    CHECK(qa_actor_index_retained_at(&index,0,&actor,&value) && qa_actor_id_equal(actor,second));
    CHECK(qa_id_index_reserve(&index.retained,64,&error));
    CHECK(qa_actor_index_find_retained(&index,reused,&value) && value==30);
    qa_actor_index_forget_retained(&index,second);
    CHECK(qa_actor_index_retained_count(&index)==1);
    CHECK(qa_actor_index_find_retained(&index,reused,&value) && value==30);
    qa_actor_index_destroy(&index);
    CHECK(qa_actors_destroy(registry,&error)); qa_arena_destroy(&storage);
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
typedef struct q2_record_delivery_test {
    qa_network_runtime *runtime;
    q2_session *session;
    qa_net_client_id id;
    const qa_q2_server_record *loan;
    qa_q2_server_event_kind kinds[8];
    size_t count, preparations, frames, before, after, tail, drops;
    bool commands, cancel;
} q2_record_delivery_test;

static bool q2_record_admit(void *context, const qa_net_connect *request, qa_error *error)
{ (void)context; (void)request; (void)error; return true; }
static bool q2_record_current(void *context, qa_net_client_id id, qa_error *error)
{ (void)error; return qa_net_client_id_equal(((q2_record_delivery_test *)context)->id, id); }
static bool q2_record_serverdata(void *context, qa_net_client_id id, uint64_t generation,
    const qa_q2_serverdata *data, qa_q2_preparation *result, qa_error *error)
{
    (void)generation;
    CHECK(data->servercount == 7);
    *result = QA_Q2_PREPARATION_READY;
    return q2_record_current(context, id, error);
}
static bool q2_record_prepare(void *context, qa_net_client_id id, uint64_t generation,
    const qa_q2_game_state *game, qa_q2_preparation *result, qa_error *error)
{
    (void)generation;
    q2_record_delivery_test *state = context;
    CHECK(game->data.servercount == 7);
    ++state->preparations;
    *result = state->cancel ? QA_Q2_PREPARATION_CANCELED :
        state->preparations == 1 ? QA_Q2_PREPARATION_WAITING : QA_Q2_PREPARATION_READY;
    return q2_record_current(context, id, error);
}
static bool q2_record_frame(void *context, qa_net_client_id id, const qa_q2_wire_frame *frame,
    const qa_q2_server_record *records, size_t count, uint64_t time, qa_error *error)
{
    (void)records; (void)count;
    q2_record_delivery_test *state = context;
    CHECK(frame->valid && time == 123);
    CHECK(frame->server_frame == (int32_t)++state->frames);
    return q2_record_current(context, id, error);
}
static bool q2_record_command(void *context, qa_net_client_id id, uint8_t seat,
    const char *text, qa_error *error)
{
    q2_record_delivery_test *state = context;
    CHECK(seat == 0);
    if (!strcmp(text, "before")) ++state->before;
    else if (!strcmp(text, "after")) ++state->after;
    else { CHECK(!strcmp(text, "tail")); ++state->tail; }
    return q2_record_current(context, id, error);
}
static bool q2_record_consume(void *context, qa_net_client_id id,
    const qa_q2_server_record *records, size_t count, qa_unified_frame_lease *lease, qa_error *error)
{
    (void)lease;
    q2_record_delivery_test *state = context;
    CHECK(count == 1 && q2_record_current(context, id, error));
    state->loan = records;
    bool waiting = false;
    bool ok = records->event.kind != QA_Q2_SVC_COMMAND || !state->commands ||
        qa_network_q2_client_stufftext(state->runtime, id, records->seat,
            records->event.data.print.text, &waiting, error);
    /* Even cancellation must leave cold decoded storage alive through this
     * callback; the enclosing continuation retires it afterward. */
    CHECK(state->session->state.client.batch.records != NULL);
    if (records->event.kind == QA_Q2_SVC_COMMAND) CHECK(records->event.data.print.text[0] != 0);
    if (ok && !waiting) {
        CHECK(state->count < sizeof(state->kinds) / sizeof(*state->kinds));
        state->kinds[state->count++] = records->event.kind;
    }
    state->loan = NULL;
    return ok;
}
static bool q2_record_cancel(void *context, qa_net_client_id id, qa_error *error)
{
    q2_record_delivery_test *state = context;
    if (state->loan) CHECK(state->loan->event.kind == QA_Q2_SVC_COMMAND);
    return q2_record_current(context, id, error);
}
static bool q2_record_drop(void *context, qa_net_client_id id, const char *reason, qa_error *error)
{
    (void)reason;
    q2_record_delivery_test *state = context;
    CHECK(state->session->state.client.batch.records != NULL);
    if (state->loan) CHECK(state->loan->event.data.print.text[0] != 0);
    ++state->drops;
    return q2_record_current(context, id, error);
}
static bool q2_record_download(void *context, qa_net_client_id id,
    const qa_q2_server_event *event, bool *complete, qa_error *error)
{ (void)event; *complete = false; return q2_record_current(context, id, error); }
static bool q2_record_ack(void *context, qa_net_client_id id, uint32_t sequence, uint64_t time, qa_error *error)
{ (void)sequence; (void)time; return q2_record_current(context, id, error); }
static bool q2_record_sent(void *context, qa_net_client_id id, qa_net_seat_id seat,
    uint32_t sequence, uint64_t number, const qa_q2_usercmd *command, uint64_t time, qa_error *error)
{ (void)seat; (void)sequence; (void)number; (void)command; (void)time; return q2_record_current(context, id, error); }
static bool q2_record_input(void *context, const qa_usercmd *command, qa_q2_usercmd *out, qa_error *error)
{ (void)context; (void)command; (void)error; *out = (qa_q2_usercmd){0}; return true; }
static void test_q2_record_delivery(void)
{
    for (size_t pass = 0; pass < 5; ++pass) {
        qa_error error = {0}; qa_net_loopback *hub; qa_net_transport *client, *server;
        CHECK(qa_net_loopback_create((qa_net_limits){65535, 8}, &hub, &error));
        CHECK(qa_net_loopback_bind(hub, "client", &client, &error));
        CHECK(qa_net_loopback_bind(hub, "server", &server, &error));
        q2_record_delivery_test state = {.commands = pass < 3, .cancel = pass == 2 || pass == 4};
        qa_network_options options = {.owner = 71, .clients = 1, .packets_per_pump = 8,
            .hooks = {.admit = q2_record_admit}};
        CHECK(qa_network_create(client, &options, &state.runtime, &error));
        qa_net_protocol_id protocol = {.kind = QA_NET_Q2_34};
        qa_net_seat_binding seat = {.seat = {.owner = 71, .index = 0}, .remote_index = 0};
        qa_net_connect request = {.attachment = QA_NET_LOCAL_SEAT, .endpoint = *qa_net_transport_address(server),
            .protocol = protocol, .seats = &seat, .seat_count = 1};
        qa_network_q2_client_policy policy = {.channel = {.protocol = protocol}, .pending_commands = 1,
            .messages = {.demo = true, .config_strings = 2048, .inventory_slots = 256, .history_capacity = 2}};
        qa_network_q2_client_hooks hooks = {.context = &state, .current = q2_record_current,
            .server_data = q2_record_serverdata, .prepare = q2_record_prepare, .frame = q2_record_frame,
            .records = q2_record_consume, .download = q2_record_download, .cancel_loading = q2_record_cancel,
            .acknowledged = q2_record_ack, .sent = q2_record_sent, .command = q2_record_input,
            .server_command = q2_record_command, .drop = q2_record_drop};
        if (state.commands) hooks.records_commands = true;
        CHECK(qa_network_attach_q2_client(state.runtime, &request, &policy, &hooks, 0, &state.id, &error));
        state.session = q2_get(state.runtime, state.id, false, &error); CHECK(state.session);
        uint8_t bytes[4096]; qa_net_writer writer; qa_q2_codec codec;
        CHECK(qa_q2_codec_init(&codec, protocol, &error));
        qa_net_writer_init(&writer, bytes, sizeof(bytes), &error);
        qa_q2_serverdata data = {.servercount = 7, .attractloop = true, .gamedir = "baseq2", .levelname = "unit"};
        CHECK(qa_q2_write_serverdata(&codec, &writer, &data));
        qa_q2_server_event event = {.kind = QA_Q2_SVC_PRINT, .data.print.text = "unit"};
        CHECK(qa_q2_server_event_write(&codec, &writer, &event));
        event = (qa_q2_server_event){.kind = QA_Q2_SVC_COMMAND, .data.print.text = "before;precache 7;after"};
        CHECK(qa_q2_server_event_write(&codec, &writer, &event));
        qa_q2_wire_frame frame = {.valid = true, .server_frame = 1, .delta_frame = -1, .player_count = 1};
        CHECK(qa_q2_frame_write(&codec, &writer, &frame, NULL, (qa_q2_entity_span){0}, 1));
        event = (qa_q2_server_event){.kind = QA_Q2_SVC_PRINT, .data.print.text = "unit"};
        CHECK(qa_q2_server_event_write(&codec, &writer, &event));
        frame.server_frame = 2;
        CHECK(qa_q2_frame_write(&codec, &writer, &frame, NULL, (qa_q2_entity_span){0}, 1));
        event = (qa_q2_server_event){.kind = QA_Q2_SVC_COMMAND, .data.print.text = "tail"};
        CHECK(qa_q2_server_event_write(&codec, &writer, &event));
        CHECK(qa_network_q2_client_demo_receive(state.runtime, state.id,
            (qa_bytes){bytes, qa_net_writer_size(&writer)}, 123, &error));
        if (pass == 1 || pass == 2 || pass == 4) {
            q2_records cold = {.received_ns = state.session->state.client.batch.received_ns};
            for (size_t i = 0; i < state.session->state.client.batch.count; ++i)
                CHECK(q2_record_retain(&cold, state.session->state.client.batch.records + i, &error));
            q2_records_free(&state.session->state.client.batch); state.session->state.client.batch = cold;
            CHECK(!cold.lease);
        }
        CHECK(qa_network_q2_client_continue(state.runtime, state.id, &error));
        CHECK(state.before == 1 && state.after == 0 && state.tail == 0 && state.frames == 0);
        if (state.cancel) {
            CHECK(state.drops == 1 && state.session->retiring);
        } else {
            q2_client *held = &state.session->state.client;
            CHECK(held->preparation_held && held->batch.cursor == 2 && held->command_offset == strlen("before;precache 7;"));
            CHECK(state.count == 2 && state.kinds[0] == QA_Q2_SVC_SERVERDATA && state.kinds[1] == QA_Q2_SVC_PRINT);
            CHECK(qa_network_q2_client_continue(state.runtime, state.id, &error));
            static const qa_q2_server_event_kind expected[] = {QA_Q2_SVC_SERVERDATA, QA_Q2_SVC_PRINT,
                QA_Q2_SVC_COMMAND, QA_Q2_SVC_FRAME, QA_Q2_SVC_PRINT, QA_Q2_SVC_FRAME, QA_Q2_SVC_COMMAND};
            CHECK(state.count == sizeof(expected) / sizeof(*expected) && !memcmp(state.kinds, expected, sizeof(expected)));
            CHECK(state.before == 1 && state.after == 1 && state.tail == 1 && state.frames == 2 && state.preparations == 2);
            CHECK(held->command_offset == 0 && !held->preparation_held);
            CHECK(qa_network_q2_client_continue(state.runtime, state.id, &error));
            CHECK(state.count == sizeof(expected) / sizeof(*expected) && state.before == 1 && state.after == 1 && state.tail == 1);
        }
        CHECK(!state.session->state.client.batch.records && !state.session->state.client.receive_held);
        qa_network_destroy(state.runtime); qa_net_transport_close(server); qa_net_loopback_close(hub);
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

static void test_native_string_boundaries(void)
{
    qa_error error = {0};
    qa_native_guest *guest = NULL;
    qa_native_guest_options options = {
        .image = {.format = QA_NATIVE_IMAGE_ELF64,
            .target = {.os = QA_NATIVE_OS_LINUX, .arch = QA_NATIVE_ARCH_X86_64,
                .abi = QA_NATIVE_ABI_SYSTEM_V_X64, .pointer_bytes = 8}},
        .allocation_base = UINT64_C(0x300000),
        .maximum_backing_bytes = 4 * QA_NATIVE_GUEST_PAGE,
        .backend = QA_NATIVE_GUEST_EMULATED};
    CHECK(qa_native_guest_create(&options, &guest, &error));
    uint64_t base = UINT64_C(0x100000), boundary = base + QA_NATIVE_GUEST_PAGE;
    qa_native_guest_mapping first, second;
    CHECK(qa_native_guest_map(guest, base, QA_NATIVE_GUEST_PAGE,
        QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, (qa_bytes){0}, &first, &error));
    CHECK(qa_native_guest_map(guest, boundary, QA_NATIVE_GUEST_PAGE,
        QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, (qa_bytes){0}, &second, &error));
    CHECK(qa_native_guest_write(guest, boundary - 2, (qa_bytes){(const uint8_t *)"abc", 4}, &error));
    qa_buffer text = {0}; bool terminated;
    CHECK(guest_string_read(guest, boundary - 2, 1, 8, NULL, &text, &terminated, &error));
    CHECK(terminated && text.size == 3 && !memcmp(text.data, "abc", 4));
    qa_buffer_free(&text);
    CHECK(guest_string_read(guest, boundary - 2, 1, 2, NULL, &text, &terminated, &error));
    CHECK(!terminated && text.size == 2 && !memcmp(text.data, "ab", 2));
    qa_buffer_free(&text);
    const uint8_t wide[] = {'A', 0, 'B', 0, 0, 0};
    CHECK(qa_native_guest_write(guest, boundary - 1, (qa_bytes){wide, sizeof(wide)}, &error));
    qa_arena storage; qa_arena_init(&storage, 0);
    CHECK(guest_string_read(guest, boundary - 1, 2, 4, &storage, &text, &terminated, &error));
    CHECK(terminated && text.size == 4 && !memcmp(text.data, wide, sizeof(wide)));
    qa_arena_destroy(&storage); text = (qa_buffer){0};
    const uint8_t last[] = {255, 0};
    CHECK(qa_native_guest_write(guest, boundary - 2, (qa_bytes){last, sizeof(last)}, &error));
    CHECK(qa_native_guest_protect(guest, second.id, 0, &error));
    CHECK(guest_string_read(guest, boundary - 2, 1, 8, NULL, &text, &terminated, &error));
    CHECK(terminated && text.size == 1 && text.data[0] == 255 && text.data[1] == 0);
    qa_buffer_free(&text);
    CHECK(qa_native_guest_write(guest, boundary - 1, (qa_bytes){(const uint8_t *)"x", 1}, &error));
    CHECK(!guest_string_read(guest, boundary - 1, 1, 8, NULL, &text, &terminated, &error));
    CHECK(error.code != QA_OK && !text.data && !text.size);
    error = (qa_error){0};
    CHECK(guest_string_read(guest, 0, 1, 0, NULL, &text, &terminated, &error));
    CHECK(!terminated && !text.data && !text.size);
    uint64_t file_base = base + 4 * QA_NATIVE_GUEST_PAGE;
    qa_native_guest_mapping file_mapping;
    CHECK(qa_native_guest_map_file(guest, file_base, 2 * QA_NATIVE_GUEST_PAGE,
        QA_NATIVE_GUEST_READ, (qa_bytes){(const uint8_t *)"s", 2}, 0, &file_mapping, &error));
    CHECK(guest_string_read(guest, file_base + QA_NATIVE_GUEST_PAGE - 1, 1, 8,
        NULL, &text, &terminated, &error));
    CHECK(terminated && !text.size && text.data[0] == 0);
    qa_buffer_free(&text);
    CHECK(!guest_string_read(guest, file_base + QA_NATIVE_GUEST_PAGE, 1, 1,
        NULL, &text, &terminated, &error));
    CHECK(error.code == QA_ERROR_FORMAT && !text.data && !text.size);
    error = (qa_error){0};
    CHECK(qa_native_guest_destroy(&guest, &error) && !guest);
}

static void test_native_resumable_budget(void)
{
    qa_error error = {0};
    qa_native_guest *guest = NULL;
    qa_native_guest_options options = {
        .image = {.format = QA_NATIVE_IMAGE_ELF64,
            .target = {.os = QA_NATIVE_OS_LINUX, .arch = QA_NATIVE_ARCH_X86_64,
                .abi = QA_NATIVE_ABI_SYSTEM_V_X64, .pointer_bytes = 8}},
        .allocation_base = UINT64_C(0x200000),
        .maximum_backing_bytes = 2 * QA_NATIVE_GUEST_PAGE,
        .backend = QA_NATIVE_GUEST_EMULATED};
    CHECK(qa_native_guest_create(&options, &guest, &error));
    const uint64_t entry = UINT64_C(0x100000), returned = entry + 6;
    const uint8_t code[] = {
        0xff, 0xc0, 0xff, 0xc9, 0x75, 0xfa, 0x90,
        [16] = 0x89, 0xf8,                   /* mov eax, edi */
        0xb9, 0x03, 0x00, 0x00, 0x00,       /* mov ecx, 3 */
        0xff, 0xc0, 0xff, 0xc9, 0x75, 0xfa, /* inc eax; dec ecx; jnz */
        0xc3};                              /* ret */
    qa_native_guest_mapping mapping;
    CHECK(qa_native_guest_map(guest, entry, QA_NATIVE_GUEST_PAGE,
        QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_EXECUTE,
        (qa_bytes){code, sizeof(code)}, &mapping, &error));
    qa_native_guest_cpu cpu;
    CHECK(qa_native_guest_cpu_read(guest, &cpu, &error));
    cpu.registers[QA_NATIVE_RAX] = 0;
    cpu.registers[QA_NATIVE_RCX] = 3;
    cpu.flags = 2;
    CHECK(qa_native_guest_cpu_write(guest, &cpu, &error));
    qa_native_guest_run_result result;
    CHECK(qa_native_guest_run(guest, entry, returned, 20, 2, &result, &error));
    CHECK(result.kind == QA_NATIVE_GUEST_BUDGET && result.counted &&
        result.instructions == 2 && result.remaining == 18);
    CHECK(qa_native_guest_idle(guest) && !qa_native_guest_terminal(guest));
    CHECK(qa_native_guest_cpu_read(guest, &cpu, &error));
    CHECK(cpu.instruction == entry + 4 && cpu.registers[QA_NATIVE_RAX] == 1 &&
        cpu.registers[QA_NATIVE_RCX] == 2);
    cpu.registers[QA_NATIVE_RBX] = 17;
    CHECK(qa_native_guest_cpu_write(guest, &cpu, &error));
    qa_native_guest_cpu paused = cpu;
    CHECK(qa_native_guest_run(guest, cpu.instruction, returned, 0, 2, &result, &error));
    CHECK(result.kind == QA_NATIVE_GUEST_BUDGET && result.counted &&
        result.instructions == 0 && result.remaining == 0);
    CHECK(qa_native_guest_cpu_read(guest, &cpu, &error));
    CHECK(cpu.instruction == paused.instruction && cpu.flags == paused.flags &&
        !memcmp(cpu.registers, paused.registers, sizeof(cpu.registers)));
    CHECK(qa_native_guest_run(guest, cpu.instruction, returned, 18, 3, &result, &error));
    CHECK(result.kind == QA_NATIVE_GUEST_BUDGET && result.counted &&
        result.instructions == 3 && result.remaining == 15);
    CHECK(qa_native_guest_cpu_read(guest, &cpu, &error));
    CHECK(cpu.instruction == entry + 4 && cpu.registers[QA_NATIVE_RAX] == 2 &&
        cpu.registers[QA_NATIVE_RCX] == 1 && cpu.registers[QA_NATIVE_RBX] == 17);
    CHECK(qa_native_guest_run(guest, cpu.instruction, returned, result.remaining, 4, &result, &error));
    CHECK(result.kind == QA_NATIVE_GUEST_RETURN && result.counted &&
        result.instructions == 4 && result.remaining == 11);
    CHECK(qa_native_guest_cpu_read(guest, &cpu, &error));
    CHECK(cpu.instruction == returned && cpu.registers[QA_NATIVE_RAX] == 3 &&
        cpu.registers[QA_NATIVE_RCX] == 0 && cpu.registers[QA_NATIVE_RBX] == 17);
    CHECK(qa_native_guest_run(guest, entry, returned, 1, 100, &result, &error));
    CHECK(result.kind == QA_NATIVE_GUEST_BUDGET && result.counted &&
        result.instructions == 1 && result.remaining == 0);
    CHECK(qa_native_guest_cpu_read(guest, &cpu, &error));
    CHECK(cpu.instruction == entry + 2 && cpu.registers[QA_NATIVE_RAX] == 4);
    CHECK(qa_native_guest_idle(guest) && !qa_native_guest_terminal(guest));

    const uint64_t stack = UINT64_C(0x300000), callable = entry + 16;
    CHECK(qa_native_guest_map(guest, stack, QA_NATIVE_GUEST_PAGE,
        QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE,
        (qa_bytes){0}, &mapping, &error));
    cpu.registers[QA_NATIVE_RSP] = stack + QA_NATIVE_GUEST_PAGE;
    CHECK(qa_native_guest_cpu_write(guest, &cpu, &error));
    const uint64_t caller_sp = cpu.registers[QA_NATIVE_RSP], caller_pc = cpu.instruction;
    qa_arena storage = {0};
    const qa_native_type parameter = {.kind = QA_NATIVE_I32, .count = 1};
    const qa_native_signature signature = {
        .abi = QA_NATIVE_ABI_SYSTEM_V_X64, .parameters = &parameter,
        .parameter_count = 1, .result = {.kind = QA_NATIVE_I32, .count = 1}};
    guest_abi_plan *plan = NULL;
    CHECK(guest_abi_plan_native(&signature, NULL, 0, &storage, &plan, &error));
    qa_native_value argument = {.type = QA_NATIVE_I32, .as.i32 = 7};
    qa_native_value value = {.type = QA_NATIVE_I32, .as.i32 = -123};
    guest_abi_call *call = NULL;
    CHECK(guest_abi_call_begin(plan, guest, callable, returned, &argument, 1,
        &value, 20, false, 0, &storage, &call, &error));
    CHECK(!qa_native_guest_idle(guest) && !qa_native_guest_terminal(guest));
    CHECK(!qa_native_guest_destroy(&guest, &error) && guest && call);
    error = (qa_error){0};
    argument.as.i32 = 900;
    bool call_returned = false;
    CHECK(guest_abi_call_step(call, 4, &call_returned, &error));
    CHECK(!call_returned && value.type == QA_NATIVE_I32 && value.as.i32 == -123);
    CHECK(qa_native_guest_cpu_read(guest, &cpu, &error));
    CHECK(cpu.instruction == callable + 11 && cpu.registers[QA_NATIVE_RAX] == 8 &&
        cpu.registers[QA_NATIVE_RCX] == 2);
    CHECK(guest_abi_call_step(call, 4, &call_returned, &error));
    CHECK(!call_returned && value.type == QA_NATIVE_I32 && value.as.i32 == -123);
    CHECK(qa_native_guest_cpu_read(guest, &cpu, &error));
    CHECK(cpu.instruction == callable + 7 && cpu.registers[QA_NATIVE_RAX] == 9 &&
        cpu.registers[QA_NATIVE_RCX] == 1);
    CHECK(guest_abi_call_step(call, 4, &call_returned, &error));
    CHECK(call_returned && value.type == QA_NATIVE_I32 && value.as.i32 == -123);
    CHECK(qa_native_guest_cpu_read(guest, &cpu, &error));
    CHECK(cpu.instruction == returned && cpu.registers[QA_NATIVE_RAX] == 10 &&
        cpu.registers[QA_NATIVE_RCX] == 0);
    CHECK(guest_abi_call_finish(call, &error));
    CHECK(value.type == QA_NATIVE_I32 && value.as.i32 == 10);
    CHECK(qa_native_guest_cpu_read(guest, &cpu, &error));
    CHECK(cpu.registers[QA_NATIVE_RSP] == caller_sp && cpu.instruction == caller_pc);
    guest_abi_call_close(&call);
    CHECK(!call && qa_native_guest_idle(guest));

    argument.as.i32 = 7;
    value.as.i32 = -123;
    CHECK(guest_abi_invoke(plan, guest, callable, returned, &argument, 1, &value, 20, &error));
    CHECK(value.type == QA_NATIVE_I32 && value.as.i32 == 10 && qa_native_guest_idle(guest));
    CHECK(qa_native_guest_cpu_read(guest, &cpu, &error));
    CHECK(cpu.registers[QA_NATIVE_RSP] == caller_sp && cpu.instruction == caller_pc);

    value.as.i32 = -123;
    CHECK(guest_abi_call_begin(plan, guest, callable, returned, &argument, 1,
        &value, 5, false, 0, &storage, &call, &error));
    CHECK(guest_abi_call_step(call, 3, &call_returned, &error));
    CHECK(!call_returned && value.type == QA_NATIVE_I32 && value.as.i32 == -123);
    CHECK(qa_native_guest_cpu_read(guest, &cpu, &error));
    CHECK(cpu.instruction == callable + 9 && cpu.registers[QA_NATIVE_RAX] == 8 &&
        cpu.registers[QA_NATIVE_RCX] == 3);
    CHECK(!guest_abi_call_step(call, 3, &call_returned, &error));
    CHECK(!call_returned && error.offset == callable + 7 &&
        value.type == QA_NATIVE_I32 && value.as.i32 == -123 && qa_native_guest_terminal(guest));
    guest_abi_call_close(&call);
    CHECK(!call);
    qa_arena_destroy(&storage);
    CHECK(qa_native_guest_destroy(&guest, &error) && !guest);
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

static unsigned event_ring_final_records;
static void event_ring_final_record(void *record)
{
    CHECK(record != NULL);
    ++event_ring_final_records;
}
typedef struct event_ring_visit_check {
    const void *expected[2];
    size_t count;
} event_ring_visit_check;
static void event_ring_visit_record(void *opaque, const void *record)
{
    event_ring_visit_check *check = opaque;
    CHECK(check->count < sizeof(check->expected) / sizeof(*check->expected));
    CHECK(record == check->expected[check->count++]);
}

static void test_event_ring_cold_growth(void)
{
    qa_event_lease_release(NULL);
    event_ring_final_records = 0;
    qa_error error = {0};
    qa_event_ring *ring = qa_event_ring_create(8192, 1024, 2, &error);
    CHECK(ring != NULL);
    qa_event_transaction transaction;
    const char *retained = NULL;
    for (uint64_t id = 1; id <= 4; ++id) {
        CHECK(qa_event_ring_begin(ring, &transaction));
        char *text = qa_event_ring_alloc(&transaction, 5, 1, &error);
        CHECK(text != NULL);
        memcpy(text, "held", 5);
        CHECK(qa_event_ring_commit(&transaction, text,
            id == 4 ? event_ring_final_record : NULL) == id);
        if (id < 4) qa_event_ring_retire(ring, id + 1);
        else retained = text;
    }
    qa_event_lease *held = qa_event_ring_retain(ring, 4);
    CHECK(held != NULL);
    CHECK(qa_event_ring_reserve(ring, 8192, 4, &error));
    CHECK(qa_event_ring_at(ring, 4) == retained);
    CHECK(qa_event_lease_record(held) == retained);
    CHECK(qa_event_ring_reserve(ring, 32768, 4, &error));
    CHECK(qa_event_ring_contiguous_bytes(ring) == 32768);
    qa_event_capacity before, after;
    qa_event_capacity_read(ring, &before);
    CHECK(qa_event_ring_begin(ring, &transaction));
    CHECK(!qa_event_ring_reserve(ring, 131072, 65536, &error));
    CHECK(qa_event_ring_alloc(&transaction, 40000, 1, &error) == NULL);
    CHECK(qa_event_ring_commit(&transaction, NULL, event_ring_final_record) == 0);
    CHECK(event_ring_final_records == 0);
    qa_event_capacity_read(ring, &after);
    CHECK(after.available_bytes == before.available_bytes);
    CHECK(after.available_records == before.available_records);
    CHECK(qa_event_ring_at(ring, 4) == retained && !strcmp(retained, "held"));
    CHECK(qa_event_ring_reserve(ring, 131072, 65536, &error));
    CHECK(qa_event_ring_begin(ring, &transaction));
    char *large = qa_event_ring_alloc(&transaction, 65537, 1, &error);
    CHECK(large != NULL);
    memset(large, 'x', 65536);
    large[65536] = 0;
    CHECK(qa_event_ring_commit(&transaction, large, NULL) == 5);
    qa_event_lease *large_lease = qa_event_ring_retain(ring, 5);
    CHECK(large_lease != NULL);
    CHECK(qa_event_ring_at(ring, 4) == retained);
    CHECK(qa_event_ring_at(ring, 5) == large);
    qa_event_ring_retire(ring, 6);
    event_ring_visit_check visited = {.expected = {retained, large}};
    qa_event_ring_visit_retained(ring, event_ring_visit_record, &visited);
    CHECK(visited.count == 2);
    qa_event_ring *retired_owner = ring;
    qa_event_ring_destroy(&ring);
    CHECK(event_ring_final_records == 0);
    CHECK(ring == NULL && !strcmp(retained, "held"));
    CHECK(qa_event_lease_record(large_lease) == large && strlen(large) == 65536);
    qa_event_lease_retain(held);
    qa_event_lease_release(held);
    CHECK(event_ring_final_records == 0);
    qa_event_lease_release(held);
    CHECK(event_ring_final_records == 1);
    visited = (event_ring_visit_check){.expected = {large}};
    qa_event_ring_visit_retained(retired_owner, event_ring_visit_record, &visited);
    CHECK(visited.count == 1);
    CHECK(large[0] == 'x' && large[65535] == 'x');
    qa_event_lease_release(large_lease);
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
    uint64_t id=qa_event_ring_commit(&transaction,text,NULL);
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

static void test_q3_player_entity_projection(void)
{
    qa_q3_player player = {.clientNum = 7, .eFlags = 8,
        .eventSequence = 4, .entityEventSequence = 0,
        .events = {14, 21}, .eventParms = {3, 9}, .weapon = 5,
        .groundEntityNum = 1022, .loopSound = 17, .generic1 = 2};
    player.stats[0] = 100;
    player.origin[0] = -1.75f;
    player.velocity[1] = 40;
    player.viewangles[2] = 12.75f;
    player.powerups[3] = 1;
    qa_q3_entity entity = {.modelindex = 19, .event = 80, .eventParm = 81};
    entity.pos.time = 123;
    entity.pos.duration = 456;
    qa_q3_player_entity_state(&player, &entity, true, false, 0);
    CHECK(entity.number == 7 && entity.clientNum == 7 && entity.eType == 1);
    CHECK(entity.pos.type == 1 && entity.pos.time == 123 && entity.pos.duration == 456);
    CHECK(entity.pos.base[0] == -1 && entity.pos.delta[1] == 40 && entity.apos.base[2] == 12);
    CHECK(entity.event == (14 | 512) && entity.eventParm == 3 && player.entityEventSequence == 3);
    CHECK(entity.modelindex == 19 && entity.weapon == 5 && entity.powerups == 8);
    player.externalEvent = 44;
    player.externalEventParm = 6;
    qa_q3_player_entity_state(&player, &entity, false, true, 1000);
    CHECK(entity.event == 44 && entity.eventParm == 6 && player.entityEventSequence == 3);
    CHECK(entity.pos.type == 3 && entity.pos.time == 1000 && entity.pos.duration == 50);
    CHECK(entity.pos.base[0] == -1.75f && entity.apos.base[2] == 12.75f);
    player.externalEvent = 0;
    qa_q3_player_entity_state(&player, &entity, false, false, 0);
    CHECK(entity.event == (21 | 768) && entity.eventParm == 9 && player.entityEventSequence == 4);
    qa_q3_player_entity_state(&player, &entity, false, false, 0);
    CHECK(entity.event == (21 | 768) && entity.eventParm == 9 && player.entityEventSequence == 4);
    player.stats[0] = -40;
    qa_q3_player_entity_state(&player, &entity, false, false, 0);
    CHECK(entity.eType == 10 && entity.eFlags == 9);
    player.stats[0] = 100;
    player.pmType = 2;
    qa_q3_player_entity_state(&player, &entity, false, false, 0);
    CHECK(entity.eType == 10 && entity.eFlags == 8);
}

static bool gtv_send_discard(void *context, qa_bytes bytes, qa_error *error)
{ (void)context; (void)bytes; (void)error; return true; }
static bool gtv_data_count(void *context, const qa_gtv_event *event, qa_error *error)
{
    (void)error;
    if (event->kind == QA_GTV_EVENT_DATA) {
        const uint8_t expected[] = {17, 31, 127};
        CHECK(event->payload.size == sizeof(expected));
        CHECK(!memcmp(event->payload.data, expected, sizeof(expected)));
        ++*(unsigned *)context;
    }
    return true;
}
static void test_gtv_output_reuse(void)
{
    qa_error error = {0}; qa_gtv_server_stream *server = NULL;
    CHECK(qa_gtv_server_stream_create(&server, &error));
    size_t capacity = qa_gtv_server_message_capacity(server);
    uint8_t *bytes = malloc(capacity); CHECK(bytes);
    qa_q2_mvd_framer *framer = NULL;
    CHECK(qa_q2_mvd_framer_create(false, QA_Q2_MVD_MESSAGE_BYTES, &framer, &error));
    size_t size = 0, consumed; bool present; qa_bytes message;
    CHECK(qa_gtv_server_message(server, QA_GTV_NO_ACCESS, (qa_bytes){0}, bytes, capacity, &size, &error));
    CHECK(qa_q2_mvd_framer_push(framer, (qa_bytes){bytes, size}, &consumed, &present, &message, &error));
    CHECK(present && consumed == size && message.size == 1 && message.data[0] == QA_GTV_NO_ACCESS);
    qa_q2_mvd_framer_reset(framer, false);
    for (unsigned pass = 0; pass < 2; ++pass) {
        CHECK(qa_gtv_server_stream_reset(server, &error));
        qa_gtv_client *client = NULL;
        CHECK(qa_gtv_client_create(&(qa_gtv_identity){"viewer", "", "test"}, QA_GTV_DEFLATE,
            gtv_send_discard, NULL, &client, &error));
        CHECK(qa_gtv_client_start(client, &error));
        unsigned delivered = 0; uint8_t magic[4]; qa_store_u32le(magic, QA_Q2_MVD_MAGIC);
        CHECK(qa_gtv_client_receive(client, (qa_bytes){magic, sizeof(magic)}, gtv_data_count, &delivered, &error));
        CHECK(!qa_gtv_server_hello(server, QA_GTV_DEFLATE, bytes, 6, &size, &error) && !size);
        error = (qa_error){0};
        CHECK(qa_gtv_server_hello(server, QA_GTV_DEFLATE, bytes, capacity, &size, &error));
        CHECK(qa_gtv_client_receive(client, (qa_bytes){bytes, size}, gtv_data_count, &delivered, &error));
        CHECK(qa_gtv_client_request_start(client, 0, &error));
        CHECK(qa_gtv_server_message(server, QA_GTV_START, (qa_bytes){0}, bytes, capacity, &size, &error));
        CHECK(qa_gtv_client_receive(client, (qa_bytes){bytes, size}, gtv_data_count, &delivered, &error));
        const uint8_t body[] = {17, 31, 127};
        for (unsigned packet = 0; packet < 2; ++packet) {
            CHECK(!qa_gtv_server_message(server, QA_GTV_DATA, (qa_bytes){body, sizeof(body)}, bytes, 0, &size, &error) && !size);
            error = (qa_error){0};
            CHECK(qa_gtv_server_message(server, QA_GTV_DATA, (qa_bytes){body, sizeof(body)}, bytes, capacity, &size, &error));
            for (size_t at = 0; at < size;) {
                size_t count = size - at < 3 ? size - at : 3;
                CHECK(qa_gtv_client_receive(client, (qa_bytes){bytes + at, count}, gtv_data_count, &delivered, &error));
                at += count;
            }
        }
        CHECK(delivered == 2); qa_gtv_client_destroy(client);
    }
    qa_q2_mvd_framer_destroy(framer); qa_gtv_server_stream_destroy(server); free(bytes);
}

int main(int argc, char **argv)
{
    if(!player_feedback_test()) return 1;
    int recovery_status;
    if (test_recovery_child(argc, argv, &recovery_status)) return recovery_status;
    test_errors_and_buffers();
    test_aas_geometry_compaction();
    test_prediction_continuations();
    test_field_completion();
    test_source_text();
    test_shared_random();
    test_q2_hud_configuration();
    test_typewriter_centers();
    test_saved_player_disguise();
    test_compiled_sound_names();
    test_unicast_claim_storage();
    test_shared_team_values();
    test_source_scalars();
    test_q3_player_entity_projection();
    test_module_observation_storage();
    test_console_restore_pages();
    test_console_failed_release();
    test_console_command_precedence();
    test_shared_video_mode_recovery();
    test_chat_initial_aliases();
    test_physical_prediction_command();
    test_shared_campaign_modes();
    test_debug_geometry_wire();
    test_debug_geometry_lifetime();
    test_shared_angle_basis();
    test_shared_bounds_and_transform();
    test_shared_vector_geometry();
    test_shared_angle_wrapping();
    test_shared_direction_angles();
    test_native_import_slot_identity();
    test_native_string_boundaries();
    test_native_resumable_budget();
    test_q2_weapon_names();
    test_q2_player_media();
    test_literal_tokens();
    test_render_resource_identity();
    test_platform_event_retirement();
    test_q2_command_angles();
    test_loopback_admission();
    test_dosbox_loopback();
    test_loopback_nq_signon();
    test_kex_send_admission();
    test_kex_userinfo_projection();
    test_kex_direct_delivery();
    test_nq_send_admission();
    test_q3_send_admission();
    test_arena_progress_bound_store();
    test_event_ring_cold_growth();
    test_q3_reliable_event_references();
    test_q3_received_commands();
    test_q3_command_cursors();
    test_shared_cvar_archive();
    test_cvar_info_storage();
    test_common_compression();
    test_medium_coverage();
    test_qw_shared_output();
    test_dedicated_line_storage();
    test_client_input_sink();
    test_shared_input_menu_defaults();
    test_profiler_mode_changes();
    test_source_nonmipped_transparency();
    test_cached_world_surface();
    test_retained_patch_levels();
    test_json_caller_storage();
    test_q2_donor_save_metadata();
    test_binary();
    test_spans();
    test_arena();
    test_operation_pool();
    test_reserved_scene_frame();
    test_bounded_handshake();
    test_shared_trajectory();
    test_lightstyle_snapshot();
    test_metadata_retention();
    test_metadata_names();
    test_files();
    test_shared_text_interpolation();
    test_localization_lookup_generation();
    test_ring_console_capture();
    test_q1_muzzle_projection();
    test_connection_request_cursor();
    test_shared_effect_cursor();
    test_roq_prefix_checkpoint();
    test_roq_handle_checkpoints();
    test_raw_audio_custody();
    test_raw_audio_scalar_cursor();
    test_roq_audio_cursor();
    test_cin_prefix_checkpoint();
    test_shared_audio_preparation();
    test_retained_movement_result();
    test_q2_requested_body();
    test_campaign_unit();
    test_recovery_checkpoints();
    test_q1_original_codec();
    test_q3_font_export(argv[0]);
    test_q1_effect_state();
    test_q1_lightstyle_event();
    test_ordered_lightstyles();
    test_q2_owned_frames();
    test_q2_retained_records();
    test_q2_record_delivery();
    test_gtv_output_reuse();
    test_actor_index_lifetimes();
    test_component_construction_actors();
    test_provider_identity_views();
    test_q1_gameplay();
    test_guest();
    test_recovery(argv[0]);
    puts("core tests passed");
    return EXIT_SUCCESS;
}

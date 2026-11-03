/*
 * mission-owner-draw.ts paint/value/width/visible, frame.ts weapon registration
 * and per-generation menu ownership. No TS test exercises these modules directly,
 * so ORACLE_CASES below is generated: each case ran under bun against the real TS
 * MissionOwnerDraw, ClientDrawTools, ClientDrawIcons, Draw2D and state classes, and
 * the expected text is the recorded draw-call log. Every leaf checks a sentinel ctx.
 * Reuses the composition fixture of test_q3_presentation_depth.c, so that test's
 * main is renamed rather than its 600 fixture lines duplicated.
 * GPL-2.0-or-later.
 */
#define main q3_presentation_depth_main
#include "test_q3_presentation_depth.c"
#undef main
#include <stdarg.h>
#include <sys/mman.h>
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/presentation/draw_tools.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

typedef struct oracle_case { const char *name; const char *const *ops, *const *expected; } oracle_case;
static const char *const ORACLE_BASE =
    "time=70000\n"
    "sorted.0=2\n"
    "sorted.1=5\n"
    "sorted.2=3\n"
    "num_sorted=3\n"
    "selected=1\n"
    "scores1=8\n"
    "scores2=3\n"
    "fraglimit=20\n"
    "capturelimit=5\n"
    "maxclients=8\n"
    "ps.team=1\n"
    "ps.score=17\n"
    "ps.rank=1\n"
    "ps.health=88\n"
    "ps.armor=44\n"
    "client.2.name=Grunt\n"
    "client.2.team=1\n"
    "client.2.health=100\n"
    "client.2.armor=50\n"
    "client.2.location=1\n"
    "client.2.team_task=2\n"
    "client.2.cur_weapon=2\n"
    "client.2.info_valid=1\n"
    "client.5.name=Ranger\n"
    "client.5.team=1\n"
    "client.5.health=30\n"
    "client.5.armor=10\n"
    "client.5.location=2\n"
    "client.5.team_task=3\n"
    "client.5.cur_weapon=5\n"
    "client.5.info_valid=1\n"
    "client.5.powerups=130\n"
    "client.3.name=Major\n"
    "client.3.team=2\n"
    "client.3.health=70\n"
    "client.3.armor=0\n"
    "client.3.location=9\n"
    "client.3.team_task=4\n"
    "client.3.cur_weapon=3\n"
    "client.3.info_valid=1\n"
    "cs.609=Red Base\n"
    "cs.610=Tower\n"
    "pred.weapon=2\n"
    "ent.weapon=2\n"
    "ps.ammo.2=7";
static const oracle_case ORACLE_CASES[] = {
  {"armor 3d",
    (const char *const[]){
    "paint 1",
    NULL},
    (const char *const[]){
    "> 1\n"
    "scene clear\n"
    "scene entity armor.model origin 90 0 -10 axis 0.427555114 0.903989315 0 -0.903989315 0.427555114 0 0 0 1\n"
    "scene render 100 200 64 32 70000 1",
    NULL}},
  {"armor flag2d",
    (const char *const[]){
    "paint 1 flags=268435456",
    NULL},
    (const char *const[]){
    "> 1\n"
    "pic g.armorIcon 100 217 64 32 0 0 1 1",
    NULL}},
  {"armor icon2d",
    (const char *const[]){
    "paint 51",
    NULL},
    (const char *const[]){
    "> 51\n"
    "pic g.armorIcon 100 217 64 32 0 0 1 1",
    NULL}},
  {"armor cvar 2d",
    (const char *const[]){
    "cvar.cg_draw3dIcons=0\n"
    "paint 1",
    NULL},
    (const char *const[]){
    "> 1\n"
    "pic g.armorIcon 100 217 64 32 0 0 1 1",
    NULL}},
  {"armor cvars off",
    (const char *const[]){
    "cvar.cg_draw3dIcons=0\n"
    "cvar.cg_drawIcons=0\n"
    "paint 1",
    NULL},
    (const char *const[]){
    "> 1",
    NULL}},
  {"armor icons off 3d",
    (const char *const[]){
    "cvar.cg_drawIcons=0\n"
    "paint 1",
    NULL},
    (const char *const[]){
    "> 1",
    NULL}},
  {"armor time spin",
    (const char *const[]){
    "time=1234\n"
    "paint 1",
    NULL},
    (const char *const[]){
    "> 1\n"
    "scene clear\n"
    "scene entity armor.model origin 90 0 -10 axis -0.799537241 -0.600616515 0 0.600616515 -0.799537241 0 0 0 1\n"
    "scene render 100 200 64 32 1234 1",
    NULL}},
  {"draw status off",
    (const char *const[]){
    "cvar.cg_drawStatus=0\n"
    "paint 1\n"
    "paint 4\n"
    "paint 39",
    NULL},
    (const char *const[]){
    "> 1\n"
    "> 4\n"
    "> 39",
    NULL}},
  {"armor value",
    (const char *const[]){
    "paint 2 style=3",
    NULL},
    (const char *const[]){
    "> 2\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 128.5 227 4.80000019 7.20000029 0.203125 0 0.20703125 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 127.5 226 4.80000019 7.20000029 0.203125 0 0.20703125 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 133.300003 227 4.80000019 7.20000029 0.203125 0 0.20703125 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 132.300003 226 4.80000019 7.20000029 0.203125 0 0.20703125 1\n"
    "color null",
    NULL}},
  {"armor value bg",
    (const char *const[]){
    "paint 2 bg=numbg",
    NULL},
    (const char *const[]){
    "> 2\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic numbg 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"armor value lazy",
    (const char *const[]){
    "paint 2 bglazy=lazybg",
    NULL},
    (const char *const[]){
    "> 2\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic lazybg 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"health",
    (const char *const[]){
    "paint 4 style=3",
    NULL},
    (const char *const[]){
    "> 4\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 129 227 4.80000019 7.20000029 0.21875 0 0.22265625 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 128 226 4.80000019 7.20000029 0.21875 0 0.22265625 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 133.199997 227 4.80000019 7.20000029 0.21875 0 0.22265625 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 132.199997 226 4.80000019 7.20000029 0.21875 0 0.22265625 1\n"
    "color null",
    NULL}},
  {"health bg",
    (const char *const[]){
    "paint 4 bg=numbg",
    NULL},
    (const char *const[]){
    "> 4\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic numbg 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"health lazy",
    (const char *const[]){
    "paint 4 bglazy=lazybg",
    NULL},
    (const char *const[]){
    "> 4\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic lazybg 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"score",
    (const char *const[]){
    "paint 20 style=3",
    NULL},
    (const char *const[]){
    "> 20\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 128.5 227 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 127.5 226 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 134.5 227 4.80000019 7.20000029 0.21484375 0 0.21875 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 133.5 226 4.80000019 7.20000029 0.21484375 0 0.21875 1\n"
    "color null",
    NULL}},
  {"score bg",
    (const char *const[]){
    "paint 20 bg=numbg",
    NULL},
    (const char *const[]){
    "> 20\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic numbg 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"score lazy",
    (const char *const[]){
    "paint 20 bglazy=lazybg",
    NULL},
    (const char *const[]){
    "> 20\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic lazybg 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"sel health",
    (const char *const[]){
    "paint 41 style=3",
    NULL},
    (const char *const[]){
    "> 41\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 128.5 227 4.80000019 7.20000029 0.19921875 0 0.203125 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 127.5 226 4.80000019 7.20000029 0.19921875 0 0.203125 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 132.699997 227 4.80000019 7.20000029 0.1875 0 0.19140625 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 131.699997 226 4.80000019 7.20000029 0.1875 0 0.19140625 1\n"
    "color null",
    NULL}},
  {"sel health bg",
    (const char *const[]){
    "paint 41 bg=numbg",
    NULL},
    (const char *const[]){
    "> 41\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic numbg 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"sel health lazy",
    (const char *const[]){
    "paint 41 bglazy=lazybg",
    NULL},
    (const char *const[]){
    "> 41\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic lazybg 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"sel armor",
    (const char *const[]){
    "paint 40 style=3",
    NULL},
    (const char *const[]){
    "> 40\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 127.5 227 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 126.5 226 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 133.5 227 4.80000019 7.20000029 0.1875 0 0.19140625 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 132.5 226 4.80000019 7.20000029 0.1875 0 0.19140625 1\n"
    "color null",
    NULL}},
  {"sel armor bg",
    (const char *const[]){
    "paint 40 bg=numbg",
    NULL},
    (const char *const[]){
    "> 40\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic numbg 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"sel armor lazy",
    (const char *const[]){
    "paint 40 bglazy=lazybg",
    NULL},
    (const char *const[]){
    "> 40\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic lazybg 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"sel armor zero",
    (const char *const[]){
    "selected=2\n"
    "paint 40",
    NULL},
    (const char *const[]){
    "> 40",
    NULL}},
  {"number fraction",
    (const char *const[]){
    "ps.health=-5\n"
    "paint 4 scale=0.5 x=33.5 w=40.25",
    NULL},
    (const char *const[]){
    "> 4\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.big 50.125 227 4 6 0.17578125 0 0.1796875 1\n"
    "pic font.big 53.125 227 4 6 0.20703125 0 0.2109375 1\n"
    "color null",
    NULL}},
  {"ammo 3d",
    (const char *const[]){
    "paint 5",
    NULL},
    (const char *const[]){
    "> 5\n"
    "scene clear\n"
    "scene entity ammo2.model origin 70 0 0 axis -0.266865194 0.963733852 0 -0.963733852 -0.266865194 0 0 0 1\n"
    "scene render 100 200 64 32 70000 1",
    NULL}},
  {"ammo 2d flag",
    (const char *const[]){
    "paint 5 flags=268435456",
    NULL},
    (const char *const[]){
    "> 5\n"
    "pic ammo2 100 200 64 32 0 0 1 1",
    NULL}},
  {"ammo icon2d",
    (const char *const[]){
    "paint 52",
    NULL},
    (const char *const[]){
    "> 52\n"
    "pic ammo2 100 200 64 32 0 0 1 1",
    NULL}},
  {"ammo null icon",
    (const char *const[]){
    "pred.weapon=3\n"
    "paint 52",
    NULL},
    (const char *const[]){
    "> 52",
    NULL}},
  {"ammo cvar 2d",
    (const char *const[]){
    "cvar.cg_draw3dIcons=0\n"
    "paint 5",
    NULL},
    (const char *const[]){
    "> 5\n"
    "pic ammo2 100 200 64 32 0 0 1 1",
    NULL}},
  {"ammo cvars off",
    (const char *const[]){
    "cvar.cg_draw3dIcons=0\n"
    "cvar.cg_drawIcons=0\n"
    "paint 5",
    NULL},
    (const char *const[]){
    "> 5",
    NULL}},
  {"ammo default model",
    (const char *const[]){
    "ent.weapon=4\n"
    "paint 5",
    NULL},
    (const char *const[]){
    "> 5",
    NULL}},
  {"ammo weapon0",
    (const char *const[]){
    "ent.weapon=0\n"
    "paint 5",
    NULL},
    (const char *const[]){
    "> 5",
    NULL}},
  {"ammo hud",
    (const char *const[]){
    "weapon_hud=1\n"
    "paint 5\n"
    "paint 52\n"
    "paint 6\n"
    "value 6",
    NULL},
    (const char *const[]){
    "> 5\n"
    "> 52\n"
    "> 6\n"
    "value 17",
    NULL}},
  {"ammo value",
    (const char *const[]){
    "paint 6\n"
    "paint 6 bg=numbg\n"
    "value 6",
    NULL},
    (const char *const[]){
    "> 6\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 130.5 226 4.80000019 7.20000029 0.21484375 0 0.21875 1\n"
    "color null\n"
    "> 6\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic numbg 100 200 64 32 0 0 1 1\n"
    "color null\n"
    "value 7",
    NULL}},
  {"ammo value w0",
    (const char *const[]){
    "ent.weapon=0\n"
    "paint 6\n"
    "value 6",
    NULL},
    (const char *const[]){
    "> 6\n"
    "value -1",
    NULL}},
  {"ammo bad pred",
    (const char *const[]){
    "pred.weapon=16\n"
    "paint 52",
    NULL},
    (const char *const[]){
    "THROW Invalid weapon media index 16",
    NULL}},
  {"sel head default",
    (const char *const[]){
    "paint 7",
    NULL},
    (const char *const[]){
    "> 7",
    NULL}},
  {"sel head 3d",
    (const char *const[]){
    "client.5.head=1\n"
    "paint 7",
    NULL},
    (const char *const[]){
    "> 7\n"
    "scene clear\n"
    "scene entity head5.model origin 41.7910423 0 0 axis -1 -8.74227766e-8 0 8.74227766e-8 -1 0 0 0 1\n"
    "scene render 100 200 64 32 70000 1",
    NULL}},
  {"sel head 3d deferred",
    (const char *const[]){
    "client.5.head=1\n"
    "client.5.deferred=1\n"
    "paint 7",
    NULL},
    (const char *const[]){
    "> 7\n"
    "scene clear\n"
    "scene entity head5.model origin 41.7910423 0 0 axis -1 -8.74227766e-8 0 8.74227766e-8 -1 0 0 0 1\n"
    "scene render 100 200 64 32 70000 1\n"
    "pic g.deferShader 100 200 64 32 0 0 1 1",
    NULL}},
  {"sel head icon",
    (const char *const[]){
    "cvar.cg_draw3dIcons=0\n"
    "client.5.model_icon=icons/ranger\n"
    "paint 7",
    NULL},
    (const char *const[]){
    "> 7\n"
    "pic icons/ranger 100 200 64 32 0 0 1 1",
    NULL}},
  {"voice head",
    (const char *const[]){
    "current_voice_client=3\n"
    "client.3.head=1\n"
    "paint 62",
    NULL},
    (const char *const[]){
    "> 62\n"
    "scene clear\n"
    "scene entity head3.model origin 41.7910423 0 0 axis -1 -8.74227766e-8 0 8.74227766e-8 -1 0 0 0 1\n"
    "scene render 100 200 64 32 70000 1",
    NULL}},
  {"sel name",
    (const char *const[]){
    "paint 8 style=1",
    NULL},
    (const char *const[]){
    "> 8\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 104.800003 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 109.600006 226 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 113.200005 226 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 118.600006 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 122.800003 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null",
    NULL}},
  {"voice name",
    (const char *const[]){
    "current_voice_client=3\n"
    "paint 63 style=2 scale=0.2",
    NULL},
    (const char *const[]){
    "> 63\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.small 100 224 6.4000001 9.60000038 0.30078125 0 0.3046875 1\n"
    "pic font.small 106.400002 224 6.4000001 9.60000038 0.37890625 0 0.3828125 1\n"
    "pic font.small 112.800003 224 6.4000001 9.60000038 0.4140625 0 0.41796875 1\n"
    "pic font.small 118.400002 224 6.4000001 9.60000038 0.43359375 0 0.4375 1\n"
    "pic font.small 124 224 6.4000001 9.60000038 0.4453125 0 0.44921875 1\n"
    "color null",
    NULL}},
  {"voice bad",
    (const char *const[]){
    "current_voice_client=64\n"
    "paint 63",
    NULL},
    (const char *const[]){
    "THROW Invalid owner-draw slot 64",
    NULL}},
  {"sel location",
    (const char *const[]){
    "paint 9",
    NULL},
    (const char *const[]){
    "> 9\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 106 226 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 110.199997 226 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "pic font.normal 116.199997 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 120.399994 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null",
    NULL}},
  {"sel location unknown",
    (const char *const[]){
    "selected=2\n"
    "paint 9 scale=0.6",
    NULL},
    (const char *const[]){
    "> 9\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.big 100 226 4.80000019 7.20000029 0.45703125 0 0.4609375 1\n"
    "pic font.big 104.800003 226 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.big 108.400002 226 4.80000019 7.20000029 0.41796875 0 0.421875 1\n"
    "pic font.big 113.200005 226 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.big 116.800003 226 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.big 121 226 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "pic font.big 127 226 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "color null",
    NULL}},
  {"player location",
    (const char *const[]){
    "paint 33",
    NULL},
    (const char *const[]){
    "> 33\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 104.800003 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 109 226 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 117.400002 226 4.80000019 7.20000029 0.2578125 0 0.26171875 1\n"
    "pic font.normal 121.599998 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 126.400002 226 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 130 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null",
    NULL}},
  {"status 0",
    (const char *const[]){
    "status 0",
    NULL},
    (const char *const[]){
    "status g.assaultShader",
    NULL}},
  {"status 1",
    (const char *const[]){
    "status 1",
    NULL},
    (const char *const[]){
    "status g.assaultShader",
    NULL}},
  {"status 2",
    (const char *const[]){
    "status 2",
    NULL},
    (const char *const[]){
    "status g.defendShader",
    NULL}},
  {"status 3",
    (const char *const[]){
    "status 3",
    NULL},
    (const char *const[]){
    "status g.patrolShader",
    NULL}},
  {"status 4",
    (const char *const[]){
    "status 4",
    NULL},
    (const char *const[]){
    "status g.followShader",
    NULL}},
  {"status 5",
    (const char *const[]){
    "status 5",
    NULL},
    (const char *const[]){
    "status g.retrieveShader",
    NULL}},
  {"status 6",
    (const char *const[]){
    "status 6",
    NULL},
    (const char *const[]){
    "status g.escortShader",
    NULL}},
  {"status 7",
    (const char *const[]){
    "status 7",
    NULL},
    (const char *const[]){
    "status g.campShader",
    NULL}},
  {"status 8",
    (const char *const[]){
    "status 8",
    NULL},
    (const char *const[]){
    "status g.assaultShader",
    NULL}},
  {"sel status",
    (const char *const[]){
    "paint 10",
    NULL},
    (const char *const[]){
    "> 10\n"
    "pic g.patrolShader 100 200 64 32 0 0 1 1",
    NULL}},
  {"sel status order on",
    (const char *const[]){
    "order_pending=1\n"
    "current_order=6\n"
    "order_time=100000\n"
    "time=70144\n"
    "paint 10",
    NULL},
    (const char *const[]){
    "> 10\n"
    "pic g.escortShader 100 200 64 32 0 0 1 1",
    NULL}},
  {"sel status order off",
    (const char *const[]){
    "order_pending=1\n"
    "current_order=6\n"
    "order_time=70000\n"
    "time=70656\n"
    "paint 10",
    NULL},
    (const char *const[]){
    "> 10\n"
    "pic g.escortShader 100 200 64 32 0 0 1 1",
    NULL}},
  {"sel status order early",
    (const char *const[]){
    "order_pending=1\n"
    "current_order=6\n"
    "order_time=100000\n"
    "time=70656\n"
    "paint 10",
    NULL},
    (const char *const[]){
    "> 10\n"
    "pic g.escortShader 100 200 64 32 0 0 1 1",
    NULL}},
  {"player status",
    (const char *const[]){
    "paint 42",
    NULL},
    (const char *const[]){
    "> 42\n"
    "pic g.defendShader 100 200 64 32 0 0 1 1",
    NULL}},
  {"sel weapon null",
    (const char *const[]){
    "paint 11",
    NULL},
    (const char *const[]){
    "> 11\n"
    "pic g.deferShader 100 200 64 32 0 0 1 1",
    NULL}},
  {"sel weapon",
    (const char *const[]){
    "selected=0\n"
    "paint 11",
    NULL},
    (const char *const[]){
    "> 11\n"
    "pic wicon2 100 200 64 32 0 0 1 1",
    NULL}},
  {"sel powerup",
    (const char *const[]){
    "paint 12",
    NULL},
    (const char *const[]){
    "> 12\n"
    "shader icons/quad\n"
    "pic icons/quad 100 200 64 32 0 0 1 1",
    NULL}},
  {"sel powerup none",
    (const char *const[]){
    "selected=2\n"
    "paint 12",
    NULL},
    (const char *const[]){
    "> 12",
    NULL}},
  {"sel powerup skip",
    (const char *const[]){
    "client.5.powerups=1\n"
    "paint 12",
    NULL},
    (const char *const[]){
    "> 12\n"
    "shader icons/iconh_rorb\n"
    "pic icons/iconh_rorb 100 200 64 32 0 0 1 1",
    NULL}},
  {"sel powerup scout",
    (const char *const[]){
    "client.5.powerups=1024\n"
    "paint 12",
    NULL},
    (const char *const[]){
    "> 12\n"
    "shader icons/scout\n"
    "pic icons/scout 100 200 64 32 0 0 1 1",
    NULL}},
  {"slot 32",
    (const char *const[]){
    "selected=32\n"
    "paint 8",
    NULL},
    (const char *const[]){
    "THROW Invalid owner-draw slot 32",
    NULL}},
  {"slot 8 everyone",
    (const char *const[]){
    "selected=8\n"
    "paint 8",
    NULL},
    (const char *const[]){
    "> 8\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null",
    NULL}},
  {"slot 31",
    (const char *const[]){
    "selected=31\n"
    "sorted.31=5\n"
    "paint 8",
    NULL},
    (const char *const[]){
    "> 8\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 104.800003 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 109.600006 226 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 113.200005 226 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 118.600006 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 122.800003 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null",
    NULL}},
  {"slot fraction",
    (const char *const[]){
    "selected=1.5\n"
    "paint 8",
    NULL},
    (const char *const[]){
    "THROW Invalid owner-draw slot 1.5",
    NULL}},
  {"head idle",
    (const char *const[]){
    "client.2.head=1\n"
    "head_end_time=80000\n"
    "head_start_time=69000\n"
    "head_end_yaw=200\n"
    "head_start_yaw=160\n"
    "head_end_pitch=3\n"
    "head_start_pitch=1\n"
    "paint 3",
    NULL},
    (const char *const[]){
    "> 3\n"
    "scene clear\n"
    "scene entity head2.model origin 41.7910423 0 0 axis -0.944971681 0.326641858 -0.0182652771 -0.326696366 -0.945129335 0 -0.0172630493 0.0059671998 0.999833167\n"
    "scene render 100 200 64 32 70000 1",
    NULL}},
  {"head advance",
    (const char *const[]){
    "client.2.head=1\n"
    "head_end_time=60000\n"
    "head_end_yaw=200\n"
    "head_end_pitch=3\n"
    "paint 3\n"
    "time=70500\n"
    "paint 3\n"
    "time=75000\n"
    "paint 3",
    NULL},
    (const char *const[]){
    "> 3\n"
    "scene clear\n"
    "scene entity head2.model origin 41.7910423 0 0 axis -0.998097837 -0.000869373849 -0.061644271 0.000871030381 -0.999999642 0 -0.0616442487 -0.0000536940315 0.998098195\n"
    "scene render 100 200 64 32 70000 1\n"
    "> 3\n"
    "scene clear\n"
    "scene entity head2.model origin 41.7910423 0 0 axis -0.998047173 0.00627767108 -0.0621486939 -0.00628982997 -0.999980211 0 -0.0621474646 0.000390904723 0.998066902\n"
    "scene render 100 200 64 32 70500 1\n"
    "> 3\n"
    "scene clear\n"
    "scene entity head2.model origin 41.7910423 0 0 axis -0.996467054 0.0523389727 -0.0656812042 -0.0524522364 -0.998623431 0 -0.0655907914 0.00344512612 0.997840643\n"
    "scene render 100 200 64 32 75000 1",
    NULL}},
  {"head damage",
    (const char *const[]){
    "client.2.head=1\n"
    "damage_time=69900\n"
    "damage_x=0.5\n"
    "paint 3\n"
    "time=70100\n"
    "paint 3\n"
    "time=71000\n"
    "paint 3",
    NULL},
    (const char *const[]){
    "> 3\n"
    "scene clear\n"
    "scene entity head2.model origin 41.7910423 0 0 axis -0.923879504 -0.382683426 0 0.382683426 -0.923879504 0 0 0 1\n"
    "scene render 76 200 64 32 70000 1\n"
    "> 3\n"
    "scene clear\n"
    "scene entity head2.model origin 41.7910423 0 0 axis -0.923879504 -0.382683426 0 0.382683426 -0.923879504 0 0 0 1\n"
    "scene render 82 200 64 32 70100 1\n"
    "> 3\n"
    "scene clear\n"
    "scene entity head2.model origin 41.7910423 0 0 axis -0.994500458 -0.0979321525 -0.0371237434 0.0979997069 -0.995186448 0 -0.036945045 -0.00363811594 0.999310672\n"
    "scene render 100 200 64 32 71000 1",
    NULL}},
  {"head damage large clock",
    (const char *const[]){
    "client.2.head=1\n"
    "time=33554433\n"
    "damage_time=33554431\n"
    "damage_x=-0.5\n"
    "paint 3 x=10.5 w=48",
    NULL},
    (const char *const[]){
    "> 3\n"
    "scene clear\n"
    "scene entity head2.model origin 41.7910423 0 0 axis -0.923879504 0.382683486 0 -0.382683486 -0.923879504 0 0 0 1\n"
    "scene render 3 200 48 32 33554433 1",
    NULL}},
  {"head start ahead",
    (const char *const[]){
    "client.2.head=1\n"
    "head_end_time=90000\n"
    "head_start_time=80000\n"
    "paint 3",
    NULL},
    (const char *const[]){
    "> 3\n"
    "scene clear\n"
    "scene entity head2.model origin 41.7910423 0 0 axis 1 0 0 0 1 0 0 0 1\n"
    "scene render 100 200 64 32 70000 1",
    NULL}},
  {"head icon",
    (const char *const[]){
    "cvar.cg_draw3dIcons=0\n"
    "client.2.model_icon=icons/grunt\n"
    "head_end_time=80000\n"
    "paint 3",
    NULL},
    (const char *const[]){
    "> 3\n"
    "pic icons/grunt 100 200 64 32 0 0 1 1",
    NULL}},
  {"item none",
    (const char *const[]){
    "paint 19",
    NULL},
    (const char *const[]){
    "> 19",
    NULL}},
  {"item",
    (const char *const[]){
    "ps.holdable=27\n"
    "paint 19",
    NULL},
    (const char *const[]){
    "> 19\n"
    "item 27\n"
    "item 27\n"
    "pic item27 100 200 64 32 0 0 1 1",
    NULL}},
  {"item bad",
    (const char *const[]){
    "ps.holdable=999\n"
    "paint 19",
    NULL},
    (const char *const[]){
    "THROW bad item",
    NULL}},
  {"ctf powerup ffa",
    (const char *const[]){
    "ps.persistant_powerup=29\n"
    "paint 35",
    NULL},
    (const char *const[]){
    "> 35",
    NULL}},
  {"ctf powerup team",
    (const char *const[]){
    "game_type=3\n"
    "ps.persistant_powerup=29\n"
    "paint 35",
    NULL},
    (const char *const[]){
    "> 35",
    NULL}},
  {"ctf powerup ctf",
    (const char *const[]){
    "game_type=4\n"
    "ps.persistant_powerup=29\n"
    "paint 35",
    NULL},
    (const char *const[]){
    "> 35\n"
    "item 29\n"
    "pic item29 100 200 64 32 0 0 1 1",
    NULL}},
  {"ctf powerup zero",
    (const char *const[]){
    "game_type=4\n"
    "paint 35",
    NULL},
    (const char *const[]){
    "> 35",
    NULL}},
  {"red score",
    (const char *const[]){
    "paint 28 style=3",
    NULL},
    (const char *const[]){
    "> 28\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 161 227 4.80000019 7.20000029 0.21875 0 0.22265625 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 160 226 4.80000019 7.20000029 0.21875 0 0.22265625 1\n"
    "color null",
    NULL}},
  {"blue score",
    (const char *const[]){
    "paint 27 scale=0.5 style=1",
    NULL},
    (const char *const[]){
    "> 27\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.big 161 227 4 6 0.19921875 0 0.203125 1\n"
    "color null",
    NULL}},
  {"score missing",
    (const char *const[]){
    "scores1=-9999\n"
    "scores2=-9999\n"
    "paint 28\n"
    "paint 27\n"
    "value 28",
    NULL},
    (const char *const[]){
    "> 28\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 161 226 4.80000019 7.20000029 0.17578125 0 0.1796875 1\n"
    "color null\n"
    "> 27\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 161 226 4.80000019 7.20000029 0.17578125 0 0.1796875 1\n"
    "color null\n"
    "value -9999",
    NULL}},
  {"score bg ignored",
    (const char *const[]){
    "paint 28 bg=numbg",
    NULL},
    (const char *const[]){
    "> 28\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 160 226 4.80000019 7.20000029 0.21875 0 0.22265625 1\n"
    "color null",
    NULL}},
  {"score negative",
    (const char *const[]){
    "scores1=-12\n"
    "paint 28",
    NULL},
    (const char *const[]){
    "> 28\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 151 226 4.80000019 7.20000029 0.17578125 0 0.1796875 1\n"
    "pic font.normal 154.600006 226 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "pic font.normal 160.600006 226 4.80000019 7.20000029 0.1953125 0 0.19921875 1\n"
    "color null",
    NULL}},
  {"names",
    (const char *const[]){
    "paint 29 style=3\n"
    "paint 30\n"
    "cvar.g_redteam=Alpha\n"
    "paint 29\n"
    "width 29 0.3\n"
    "width 30 0.5",
    NULL},
    (const char *const[]){
    "> 29\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 101 227 4.80000019 7.20000029 0.32421875 0 0.328125 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.32421875 0 0.328125 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 106.400002 227 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105.400002 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 110.599998 227 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 109.599998 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 116.599998 227 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 115.599998 226 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 120.799995 227 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 119.799995 226 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 126.199997 227 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 125.199997 226 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 131.599991 227 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 130.599991 226 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "color null\n"
    "> 30\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.3125 0 0.31640625 1\n"
    "pic font.normal 103.599998 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 108.400002 226 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 113.800003 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 118.600006 226 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 122.200005 226 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "color null\n"
    "> 29\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.25390625 0 0.2578125 1\n"
    "pic font.normal 103.599998 226 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 109 226 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 113.800003 226 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 119.800003 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "width 24\n"
    "width 21",
    NULL}},
  {"flag head none",
    (const char *const[]){
    "paint 21\n"
    "paint 24\n"
    "paint 23\n"
    "paint 26",
    NULL},
    (const char *const[]){
    "> 21\n"
    "> 24\n"
    "> 23\n"
    "> 26",
    NULL}},
  {"blue flag carried",
    (const char *const[]){
    "client.0.head=1\n"
    "client.2.powerups=256\n"
    "paint 21\n"
    "paint 23 style=1\n"
    "paint 24",
    NULL},
    (const char *const[]){
    "> 21\n"
    "scene clear\n"
    "scene entity head0.model origin 41.7910423 0 0 axis -0.964135945 -0.265408874 0 0.265408874 -0.964135945 0 0 0 1\n"
    "scene render 100 200 64 32 70000 1\n"
    "> 23\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.27734375 0 0.28125 1\n"
    "pic font.normal 104.199997 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 110.199997 226 4.80000019 7.20000029 0.45703125 0 0.4609375 1\n"
    "pic font.normal 115 226 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 118.599998 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "> 24",
    NULL}},
  {"red flag carried",
    (const char *const[]){
    "client.0.head=1\n"
    "client.3.powerups=128\n"
    "paint 24\n"
    "paint 26\n"
    "paint 21",
    NULL},
    (const char *const[]){
    "> 24\n"
    "scene clear\n"
    "scene entity head0.model origin 41.7910423 0 0 axis -0.964135945 -0.265408874 0 0.265408874 -0.964135945 0 0 0 1\n"
    "scene render 100 200 64 32 70000 1\n"
    "> 26\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.30078125 0 0.3046875 1\n"
    "pic font.normal 104.800003 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 109.600006 226 4.80000019 7.20000029 0.4140625 0 0.41796875 1\n"
    "pic font.normal 113.800003 226 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 118 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "> 21",
    NULL}},
  {"flag carrier wrong team",
    (const char *const[]){
    "client.3.powerups=256\n"
    "paint 23",
    NULL},
    (const char *const[]){
    "> 23",
    NULL}},
  {"flag carrier invalid",
    (const char *const[]){
    "client.2.powerups=256\n"
    "client.2.info_valid=0\n"
    "paint 23",
    NULL},
    (const char *const[]){
    "> 23",
    NULL}},
  {"flag maxclients bad",
    (const char *const[]){
    "maxclients=65\n"
    "paint 23",
    NULL},
    (const char *const[]){
    "THROW Invalid owner-draw slot 64",
    NULL}},
  {"flagstatus gt0",
    (const char *const[]){
    "game_type=0\n"
    "redflag=1\n"
    "blueflag=2\n"
    "paint 22\n"
    "paint 25",
    NULL},
    (const char *const[]){
    "> 22\n"
    "> 25",
    NULL}},
  {"flagstatus gt3",
    (const char *const[]){
    "game_type=3\n"
    "redflag=1\n"
    "blueflag=2\n"
    "paint 22\n"
    "paint 25",
    NULL},
    (const char *const[]){
    "> 22\n"
    "> 25",
    NULL}},
  {"flagstatus gt4",
    (const char *const[]){
    "game_type=4\n"
    "redflag=1\n"
    "blueflag=2\n"
    "paint 22\n"
    "paint 25",
    NULL},
    (const char *const[]){
    "> 22\n"
    "color 0 0 1 1\n"
    "pic flag2 100 200 64 32 0 0 1 1\n"
    "color null\n"
    "> 25\n"
    "color 1 0 0 1\n"
    "pic flag1 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"flagstatus gt5",
    (const char *const[]){
    "game_type=5\n"
    "redflag=1\n"
    "blueflag=2\n"
    "paint 22\n"
    "paint 25",
    NULL},
    (const char *const[]){
    "> 22\n"
    "color 0 0 1 1\n"
    "pic flag2 100 200 64 32 0 0 1 1\n"
    "color null\n"
    "> 25\n"
    "color 1 0 0 1\n"
    "pic flag1 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"flagstatus gt6",
    (const char *const[]){
    "game_type=6\n"
    "redflag=1\n"
    "blueflag=2\n"
    "paint 22\n"
    "paint 25",
    NULL},
    (const char *const[]){
    "> 22\n"
    "> 25",
    NULL}},
  {"flagstatus gt7",
    (const char *const[]){
    "game_type=7\n"
    "redflag=1\n"
    "blueflag=2\n"
    "paint 22\n"
    "paint 25",
    NULL},
    (const char *const[]){
    "> 22\n"
    "color 0 0 1 1\n"
    "pic g.blueCubeIcon 100 200 64 32 0 0 1 1\n"
    "color null\n"
    "> 25\n"
    "color 1 0 0 1\n"
    "pic g.redCubeIcon 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"flagstatus bad",
    (const char *const[]){
    "game_type=4\n"
    "redflag=7\n"
    "blueflag=-1\n"
    "paint 25\n"
    "paint 22",
    NULL},
    (const char *const[]){
    "> 25\n"
    "color 1 0 0 1\n"
    "pic flag0 100 200 64 32 0 0 1 1\n"
    "color null\n"
    "> 22\n"
    "color 0 0 1 1\n"
    "pic flag0 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"flagstatus bg",
    (const char *const[]){
    "game_type=4\n"
    "paint 25 bg=flagbg\n"
    "paint 22 bglazy=flaglazy",
    NULL},
    (const char *const[]){
    "> 25\n"
    "pic flagbg 100 200 64 32 0 0 1 1\n"
    "> 22\n"
    "pic flaglazy 100 200 64 32 0 0 1 1",
    NULL}},
  {"flagstatus bg harvester",
    (const char *const[]){
    "game_type=7\n"
    "paint 25 bg=flagbg",
    NULL},
    (const char *const[]){
    "> 25\n"
    "color 1 0 0 1\n"
    "pic g.redCubeIcon 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"oneflag -1",
    (const char *const[]){
    "game_type=5\n"
    "flag_status=-1\n"
    "paint 32",
    NULL},
    (const char *const[]){
    "> 32",
    NULL}},
  {"oneflag 0",
    (const char *const[]){
    "game_type=5\n"
    "flag_status=0\n"
    "paint 32",
    NULL},
    (const char *const[]){
    "> 32\n"
    "color 1 1 1 1\n"
    "pic flag0 100 200 64 32 0 0 1 1",
    NULL}},
  {"oneflag 1",
    (const char *const[]){
    "game_type=5\n"
    "flag_status=1\n"
    "paint 32",
    NULL},
    (const char *const[]){
    "> 32\n"
    "color 1 1 1 1\n"
    "pic flag0 100 200 64 32 0 0 1 1",
    NULL}},
  {"oneflag 2",
    (const char *const[]){
    "game_type=5\n"
    "flag_status=2\n"
    "paint 32",
    NULL},
    (const char *const[]){
    "> 32\n"
    "color 1 0 0 1\n"
    "pic flag1 100 200 64 32 0 0 1 1",
    NULL}},
  {"oneflag 3",
    (const char *const[]){
    "game_type=5\n"
    "flag_status=3\n"
    "paint 32",
    NULL},
    (const char *const[]){
    "> 32\n"
    "color 0 0 1 1\n"
    "pic flag1 100 200 64 32 0 0 1 1",
    NULL}},
  {"oneflag 4",
    (const char *const[]){
    "game_type=5\n"
    "flag_status=4\n"
    "paint 32",
    NULL},
    (const char *const[]){
    "> 32\n"
    "color 1 1 1 1\n"
    "pic flag2 100 200 64 32 0 0 1 1",
    NULL}},
  {"oneflag 5",
    (const char *const[]){
    "game_type=5\n"
    "flag_status=5\n"
    "paint 32",
    NULL},
    (const char *const[]){
    "> 32",
    NULL}},
  {"oneflag wrong game",
    (const char *const[]){
    "game_type=4\n"
    "flag_status=2\n"
    "paint 32",
    NULL},
    (const char *const[]){
    "> 32",
    NULL}},
  {"skulls ffa",
    (const char *const[]){
    "ps.generic1=4\n"
    "paint 31",
    NULL},
    (const char *const[]){
    "> 31",
    NULL}},
  {"skulls 3d",
    (const char *const[]){
    "game_type=7\n"
    "ps.generic1=4\n"
    "paint 31 style=3",
    NULL},
    (const char *const[]){
    "> 31\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 161 227 4.80000019 7.20000029 0.203125 0 0.20703125 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 160 226 4.80000019 7.20000029 0.203125 0 0.20703125 1\n"
    "color null\n"
    "scene clear\n"
    "scene entity bluecube.model origin 90 0 -10 axis 0.427555114 0.903989315 0 -0.903989315 0.427555114 0 0 0 1\n"
    "scene render 100 200 35 35 70000 1",
    NULL}},
  {"skulls 3d blue",
    (const char *const[]){
    "game_type=7\n"
    "ps.team=2\n"
    "ps.generic1=120\n"
    "paint 31",
    NULL},
    (const char *const[]){
    "> 31\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 155 226 4.80000019 7.20000029 0.22265625 0 0.2265625 1\n"
    "pic font.normal 159.800003 226 4.80000019 7.20000029 0.22265625 0 0.2265625 1\n"
    "color null\n"
    "scene clear\n"
    "scene entity redcube.model origin 90 0 -10 axis 0.427555114 0.903989315 0 -0.903989315 0.427555114 0 0 0 1\n"
    "scene render 100 200 35 35 70000 1",
    NULL}},
  {"skulls 2d",
    (const char *const[]){
    "game_type=7\n"
    "ps.generic1=4\n"
    "paint 65",
    NULL},
    (const char *const[]){
    "> 65\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 160 226 4.80000019 7.20000029 0.203125 0 0.20703125 1\n"
    "color null\n"
    "pic g.blueCubeIcon 103 216 20 20 0 0 1 1",
    NULL}},
  {"skulls 2d blue",
    (const char *const[]){
    "game_type=7\n"
    "ps.team=2\n"
    "paint 65",
    NULL},
    (const char *const[]){
    "> 65\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 159 226 4.80000019 7.20000029 0.1875 0 0.19140625 1\n"
    "color null\n"
    "pic g.redCubeIcon 103 216 20 20 0 0 1 1",
    NULL}},
  {"skulls cvar 2d",
    (const char *const[]){
    "game_type=7\n"
    "cvar.cg_draw3dIcons=0\n"
    "paint 31",
    NULL},
    (const char *const[]){
    "> 31\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 159 226 4.80000019 7.20000029 0.1875 0 0.19140625 1\n"
    "color null\n"
    "pic g.blueCubeIcon 103 216 20 20 0 0 1 1",
    NULL}},
  {"skulls icons off",
    (const char *const[]){
    "game_type=7\n"
    "cvar.cg_drawIcons=0\n"
    "paint 31",
    NULL},
    (const char *const[]){
    "> 31\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 159 226 4.80000019 7.20000029 0.1875 0 0.19140625 1\n"
    "color null",
    NULL}},
  {"team color 0",
    (const char *const[]){
    "ps.team=0\n"
    "paint 34 x=10.75 y=20.25 w=30.5 h=40.5",
    NULL},
    (const char *const[]){
    "> 34",
    NULL}},
  {"team color 1",
    (const char *const[]){
    "ps.team=1\n"
    "paint 34 x=10.75 y=20.25 w=30.5 h=40.5",
    NULL},
    (const char *const[]){
    "> 34\n"
    "color 1 0 0 0.800000012\n"
    "pic g.teamStatusBar 10 20 30 40 0 0 1 1\n"
    "color null",
    NULL}},
  {"team color 2",
    (const char *const[]){
    "ps.team=2\n"
    "paint 34 x=10.75 y=20.25 w=30.5 h=40.5",
    NULL},
    (const char *const[]){
    "> 34\n"
    "color 0 0 1 0.800000012\n"
    "pic g.teamStatusBar 10 20 30 40 0 0 1 1\n"
    "color null",
    NULL}},
  {"team color 3",
    (const char *const[]){
    "ps.team=3\n"
    "paint 34 x=10.75 y=20.25 w=30.5 h=40.5",
    NULL},
    (const char *const[]){
    "> 34",
    NULL}},
  {"area",
    (const char *const[]){
    "ps.powerup.1=75000\n"
    "ps.powerup.2=71500\n"
    "ps.powerup.3=73500\n"
    "ps.powerup.4=1500000\n"
    "ps.powerup.5=60000\n"
    "ps.powerup.0=72000\n"
    "ps.powerup.15=71000\n"
    "paint 36",
    NULL},
    (const char *const[]){
    "> 36\n"
    "color 0.5 0.5 0.5 0.5\n"
    "shader icons/envirosuit\n"
    "pic icons/envirosuit 100 200 48 32 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 151 226 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "color null\n"
    "color 0 0 0 0\n"
    "shader icons/iconh_rorb\n"
    "pic icons/iconh_rorb 100 264 48 32 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 151 290 4.80000019 7.20000029 0.1953125 0 0.19921875 1\n"
    "color null\n"
    "color 0.5 0.5 0.5 0.5\n"
    "shader icons/haste\n"
    "pic icons/haste 100 328 48 32 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 151 354 4.80000019 7.20000029 0.19921875 0 0.203125 1\n"
    "color null\n"
    "color null\n"
    "shader icons/quad\n"
    "pic icons/quad 100 392 48 32 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 151 418 4.80000019 7.20000029 0.20703125 0 0.2109375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"area special",
    (const char *const[]){
    "ps.powerup.1=75000\n"
    "ps.powerup.2=71500\n"
    "ps.powerup.3=73500\n"
    "ps.powerup.4=1500000\n"
    "ps.powerup.5=60000\n"
    "ps.powerup.0=72000\n"
    "ps.powerup.15=71000\n"
    "paint 36 align=1 special=4 scale=0.5",
    NULL},
    (const char *const[]){
    "> 36\n"
    "color 0.5 0.5 0.5 0.5\n"
    "shader icons/envirosuit\n"
    "pic icons/envirosuit 100 200 48 32 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.big 151 227 4 6 0.19140625 0 0.1953125 1\n"
    "color null\n"
    "color 0 0 0 0\n"
    "shader icons/iconh_rorb\n"
    "pic icons/iconh_rorb 168 200 48 32 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.big 219 227 4 6 0.1953125 0 0.19921875 1\n"
    "color null\n"
    "color 0.5 0.5 0.5 0.5\n"
    "shader icons/haste\n"
    "pic icons/haste 236 200 48 32 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.big 287 227 4 6 0.19921875 0 0.203125 1\n"
    "color null\n"
    "color null\n"
    "shader icons/quad\n"
    "pic icons/quad 304 200 48 32 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.big 355 227 4 6 0.20703125 0 0.2109375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"area vertical special",
    (const char *const[]){
    "ps.powerup.1=75000\n"
    "ps.powerup.2=71500\n"
    "ps.powerup.3=73500\n"
    "ps.powerup.4=1500000\n"
    "ps.powerup.5=60000\n"
    "ps.powerup.0=72000\n"
    "ps.powerup.15=71000\n"
    "paint 36 align=0 special=2",
    NULL},
    (const char *const[]){
    "> 36\n"
    "color 0.5 0.5 0.5 0.5\n"
    "shader icons/envirosuit\n"
    "pic icons/envirosuit 100 200 48 32 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 151 226 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "color null\n"
    "color 0 0 0 0\n"
    "shader icons/iconh_rorb\n"
    "pic icons/iconh_rorb 100 266 48 32 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 151 292 4.80000019 7.20000029 0.1953125 0 0.19921875 1\n"
    "color null\n"
    "color 0.5 0.5 0.5 0.5\n"
    "shader icons/haste\n"
    "pic icons/haste 100 332 48 32 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 151 358 4.80000019 7.20000029 0.19921875 0 0.203125 1\n"
    "color null\n"
    "color null\n"
    "shader icons/quad\n"
    "pic icons/quad 100 398 48 32 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 151 424 4.80000019 7.20000029 0.20703125 0 0.2109375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"area dead",
    (const char *const[]){
    "ps.powerup.1=75000\n"
    "ps.powerup.2=71500\n"
    "ps.powerup.3=73500\n"
    "ps.powerup.4=1500000\n"
    "ps.powerup.5=60000\n"
    "ps.powerup.0=72000\n"
    "ps.powerup.15=71000\n"
    "ps.health=0\n"
    "paint 36",
    NULL},
    (const char *const[]){
    "> 36",
    NULL}},
  {"area none",
    (const char *const[]){
    "paint 36",
    NULL},
    (const char *const[]){
    "> 36\n"
    "color null",
    NULL}},
  {"area tie",
    (const char *const[]){
    "ps.powerup.1=72000\n"
    "ps.powerup.2=72000\n"
    "paint 36",
    NULL},
    (const char *const[]){
    "> 36\n"
    "color 0 0 0 0\n"
    "shader icons/envirosuit\n"
    "pic icons/envirosuit 100 200 48 32 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 151 226 4.80000019 7.20000029 0.1953125 0 0.19921875 1\n"
    "color null\n"
    "color 0 0 0 0\n"
    "shader icons/quad\n"
    "pic icons/quad 100 264 48 32 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 151 290 4.80000019 7.20000029 0.1953125 0 0.19921875 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"hasflag none",
    (const char *const[]){
    "paint 38\n"
    "paint 64",
    NULL},
    (const char *const[]){
    "> 38\n"
    "> 64",
    NULL}},
  {"hasflag red",
    (const char *const[]){
    "pred.powerup.7=1\n"
    "paint 38\n"
    "paint 64",
    NULL},
    (const char *const[]){
    "> 38\n"
    "scene clear\n"
    "scene entity redflag.model origin 29.8507462 0 0 axis 0.901145458 -0.43351689 0 0.43351689 0.901145458 0 0 0 1\n"
    "scene render 102 202 62 30 70000 1\n"
    "> 64\n"
    "pic item34 100 200 64 32 0 0 1 1",
    NULL}},
  {"hasflag blue",
    (const char *const[]){
    "pred.powerup.8=1\n"
    "paint 38\n"
    "paint 64",
    NULL},
    (const char *const[]){
    "> 38\n"
    "scene clear\n"
    "scene entity blueflag.model origin 29.8507462 0 0 axis 0.901145458 -0.43351689 0 0.43351689 0.901145458 0 0 0 1\n"
    "scene render 102 202 62 30 70000 1\n"
    "> 64\n"
    "pic item35 100 200 64 32 0 0 1 1",
    NULL}},
  {"hasflag neutral",
    (const char *const[]){
    "pred.powerup.9=1\n"
    "paint 38\n"
    "paint 64",
    NULL},
    (const char *const[]){
    "> 38\n"
    "scene clear\n"
    "scene entity neutralflag.model origin 29.8507462 0 0 axis 0.901145458 -0.43351689 0 0.43351689 0.901145458 0 0 0 1\n"
    "scene render 102 202 62 30 70000 1\n"
    "> 64\n"
    "pic item46 100 200 64 32 0 0 1 1",
    NULL}},
  {"hasflag priority",
    (const char *const[]){
    "pred.powerup.7=1\n"
    "pred.powerup.8=1\n"
    "paint 64",
    NULL},
    (const char *const[]){
    "> 64\n"
    "pic item34 100 200 64 32 0 0 1 1",
    NULL}},
  {"hasflag icons off",
    (const char *const[]){
    "cvar.cg_drawIcons=0\n"
    "pred.powerup.7=1\n"
    "paint 64",
    NULL},
    (const char *const[]){
    "> 64",
    NULL}},
  {"chat",
    (const char *const[]){
    "paint 46 style=3\n"
    "paint 47 style=3\n"
    "paint 48 style=3 scale=0.2",
    NULL},
    (const char *const[]){
    "> 46\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 103.599998 226 4.80000019 7.20000029 0.47265625 0 0.4765625 1\n"
    "pic font.normal 107.799995 226 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 116.199997 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 120.399994 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 124.599991 226 4.80000019 7.20000029 0.46875 0 0.47265625 1\n"
    "pic font.normal 128.199997 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "> 47\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 104.199997 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 108.399994 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 113.199997 226 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 124 226 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 128.199997 226 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 131.800003 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "> 48\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.small 100 224 6.4000001 9.60000038 0.453125 0 0.45703125 1\n"
    "pic font.small 105.599998 224 6.4000001 9.60000038 0.39453125 0 0.3984375 1\n"
    "pic font.small 111.199997 224 6.4000001 9.60000038 0.37890625 0 0.3828125 1\n"
    "pic font.small 117.599998 224 6.4000001 9.60000038 0.42578125 0 0.4296875 1\n"
    "pic font.small 132 224 6.4000001 9.60000038 0.453125 0 0.45703125 1\n"
    "pic font.small 137.600006 224 6.4000001 9.60000038 0.46484375 0 0.46875 1\n"
    "pic font.small 145.600006 224 6.4000001 9.60000038 0.43359375 0 0.4375 1\n"
    "color null",
    NULL}},
  {"game type 0",
    (const char *const[]){
    "game_type=0\n"
    "paint 39 style=1\n"
    "width 39 0.3",
    NULL},
    (const char *const[]){
    "> 39\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.2734375 0 0.27734375 1\n"
    "pic font.normal 103.599998 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 109.599998 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 113.799995 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 122.799995 226 4.80000019 7.20000029 0.2734375 0 0.27734375 1\n"
    "pic font.normal 126.399994 226 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 130.599991 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 141.399994 226 4.80000019 7.20000029 0.25390625 0 0.2578125 1\n"
    "pic font.normal 145 226 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 150.399994 226 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "color null\n"
    "width 55",
    NULL}},
  {"game type 1",
    (const char *const[]){
    "game_type=1\n"
    "paint 39 style=1\n"
    "width 39 0.3",
    NULL},
    (const char *const[]){
    "> 39\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "width 0",
    NULL}},
  {"game type 2",
    (const char *const[]){
    "game_type=2\n"
    "paint 39 style=1\n"
    "width 39 0.3",
    NULL},
    (const char *const[]){
    "> 39\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "width 0",
    NULL}},
  {"game type 3",
    (const char *const[]){
    "game_type=3\n"
    "paint 39 style=1\n"
    "width 39 0.3",
    NULL},
    (const char *const[]){
    "> 39\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 106 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 110.199997 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 115 226 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 125.800003 226 4.80000019 7.20000029 0.265625 0 0.26953125 1\n"
    "pic font.normal 131.199997 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 135.399994 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 140.199997 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 144.399994 226 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 150.399994 226 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 156.399994 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 161.199997 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 165.399994 226 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 171.399994 226 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "color null\n"
    "width 77",
    NULL}},
  {"game type 4",
    (const char *const[]){
    "game_type=4\n"
    "paint 39 style=1\n"
    "width 39 0.3",
    NULL},
    (const char *const[]){
    "> 39\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.26171875 0 0.265625 1\n"
    "pic font.normal 104.800003 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 109.600006 226 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 114.400009 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 118.600006 226 4.80000019 7.20000029 0.45703125 0 0.4609375 1\n"
    "pic font.normal 123.400009 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 129.400009 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 138.400009 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 142.600006 226 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 148.600006 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 157.600006 226 4.80000019 7.20000029 0.2734375 0 0.27734375 1\n"
    "pic font.normal 161.200012 226 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 166.600006 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 171.400009 226 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "color null\n"
    "width 76",
    NULL}},
  {"game type 5",
    (const char *const[]){
    "game_type=5\n"
    "paint 39 style=1\n"
    "width 39 0.3",
    NULL},
    (const char *const[]){
    "> 39\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.30859375 0 0.3125 1\n"
    "pic font.normal 106 226 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 109.599998 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 118.599998 226 4.80000019 7.20000029 0.2734375 0 0.27734375 1\n"
    "pic font.normal 122.199997 226 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 127.599998 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 132.399994 226 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 142.599991 226 4.80000019 7.20000029 0.26171875 0 0.265625 1\n"
    "pic font.normal 147.399994 226 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 153.399994 226 4.80000019 7.20000029 0.2734375 0 0.27734375 1\n"
    "color null\n"
    "width 57",
    NULL}},
  {"game type 6",
    (const char *const[]){
    "game_type=6\n"
    "paint 39 style=1\n"
    "width 39 0.3",
    NULL},
    (const char *const[]){
    "> 39\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.30859375 0 0.3125 1\n"
    "pic font.normal 106 226 4.80000019 7.20000029 0.4609375 0 0.46484375 1\n"
    "pic font.normal 111.400002 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 115.599998 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 121.599998 226 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 127 226 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 131.199997 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 136 226 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "width 39",
    NULL}},
  {"game type 7",
    (const char *const[]){
    "game_type=7\n"
    "paint 39 style=1\n"
    "width 39 0.3",
    NULL},
    (const char *const[]){
    "> 39\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.28125 0 0.28515625 1\n"
    "pic font.normal 104.800003 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 109.600006 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 115.600006 226 4.80000019 7.20000029 0.4609375 0 0.46484375 1\n"
    "pic font.normal 121.000008 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 125.200005 226 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 128.800003 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 133 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 137.199997 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "width 43",
    NULL}},
  {"status place",
    (const char *const[]){
    "paint 49 style=3\n"
    "width 49 0.3",
    NULL},
    (const char *const[]){
    "> 49\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color 1 0 0 0.800000012\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 101 227 4.80000019 7.20000029 0.1953125 0 0.19921875 1\n"
    "color 1 0 0 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.1953125 0 0.19921875 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 104.599998 227 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "color 1 0 0 0.800000012\n"
    "pic font.normal 103.599998 226 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 108.199997 227 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color 1 0 0 0.800000012\n"
    "pic font.normal 107.199997 226 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color 1 1 1 0.800000012\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 116.599998 227 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "color 1 1 1 0.800000012\n"
    "pic font.normal 115.599998 226 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 121.400002 227 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "color 1 1 1 0.800000012\n"
    "pic font.normal 120.400002 226 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 126.800003 227 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color 1 1 1 0.800000012\n"
    "pic font.normal 125.800003 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 131.600006 227 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "color 1 1 1 0.800000012\n"
    "pic font.normal 130.600006 226 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 137.600006 227 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color 1 1 1 0.800000012\n"
    "pic font.normal 136.600006 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 146.600006 227 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "color 1 1 1 0.800000012\n"
    "pic font.normal 145.600006 226 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 152.600006 227 4.80000019 7.20000029 0.41015625 0 0.4140625 1\n"
    "color 1 1 1 0.800000012\n"
    "pic font.normal 151.600006 226 4.80000019 7.20000029 0.41015625 0 0.4140625 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 156.200012 227 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color 1 1 1 0.800000012\n"
    "pic font.normal 155.200012 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 160.400009 227 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "color 1 1 1 0.800000012\n"
    "pic font.normal 159.400009 226 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 171.200012 227 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "color 1 1 1 0.800000012\n"
    "pic font.normal 170.200012 226 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 177.200012 227 4.80000019 7.20000029 0.21484375 0 0.21875 1\n"
    "color 1 1 1 0.800000012\n"
    "pic font.normal 176.200012 226 4.80000019 7.20000029 0.21484375 0 0.21875 1\n"
    "color null\n"
    "width 79",
    NULL}},
  {"status tied place",
    (const char *const[]){
    "ps.rank=16384\n"
    "paint 49",
    NULL},
    (const char *const[]){
    "> 49\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 106 226 4.80000019 7.20000029 0.41015625 0 0.4140625 1\n"
    "pic font.normal 109.599998 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 113.799995 226 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 122.199997 226 4.80000019 7.20000029 0.3984375 0 0.40234375 1\n"
    "pic font.normal 127 226 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 131.199997 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color 0 0 1 0.800000012\n"
    "pic font.normal 142 226 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "pic font.normal 148 226 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 151.600006 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color 1 1 1 0.800000012\n"
    "pic font.normal 160.600006 226 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 165.400009 226 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 170.800003 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 175.600006 226 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 181.600006 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 190.600006 226 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "pic font.normal 196.600006 226 4.80000019 7.20000029 0.41015625 0 0.4140625 1\n"
    "pic font.normal 200.200012 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 204.400009 226 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 215.200012 226 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "pic font.normal 221.200012 226 4.80000019 7.20000029 0.21484375 0 0.21875 1\n"
    "color null",
    NULL}},
  {"status spectator",
    (const char *const[]){
    "ps.team=3\n"
    "paint 49\n"
    "width 49 0.3",
    NULL},
    (const char *const[]){
    "> 49\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "width 0",
    NULL}},
  {"status tied",
    (const char *const[]){
    "game_type=3\n"
    "paint 49",
    NULL},
    (const char *const[]){
    "> 49\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 106 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 110.199997 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 115 226 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 121 226 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 129.399994 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 134.199997 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 140.199997 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 149.199997 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 153.399994 226 4.80000019 7.20000029 0.41015625 0 0.4140625 1\n"
    "pic font.normal 157 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 161.199997 226 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 169.600006 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 174.400009 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 183.400009 226 4.80000019 7.20000029 0.1875 0 0.19140625 1\n"
    "color null",
    NULL}},
  {"killer none",
    (const char *const[]){
    "paint 50\n"
    "width 50 0.3",
    NULL},
    (const char *const[]){
    "> 50\n"
    "width 0",
    NULL}},
  {"killer",
    (const char *const[]){
    "killer=Visor\n"
    "paint 50 style=3\n"
    "width 50 0.3",
    NULL},
    (const char *const[]){
    "> 50\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 97 227 4.80000019 7.20000029 0.2734375 0 0.27734375 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 96 226 4.80000019 7.20000029 0.2734375 0 0.27734375 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 100.599998 227 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 99.5999985 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 106.599998 227 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105.599998 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 111.400002 227 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 110.400002 226 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 116.800003 227 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 115.800003 226 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 122.200005 227 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 121.200005 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 126.400002 227 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 125.400002 226 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 134.800003 227 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 133.800003 226 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 140.199997 227 4.80000019 7.20000029 0.47265625 0 0.4765625 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 139.199997 226 4.80000019 7.20000029 0.47265625 0 0.4765625 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 149.199997 227 4.80000019 7.20000029 0.3359375 0 0.33984375 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 148.199997 226 4.80000019 7.20000029 0.3359375 0 0.33984375 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 153.399994 227 4.80000019 7.20000029 0.41015625 0 0.4140625 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 152.399994 226 4.80000019 7.20000029 0.41015625 0 0.4140625 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 157 227 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 156 226 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 160.600006 227 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 159.600006 226 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 164.800003 227 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 163.800003 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "width 73",
    NULL}},
  {"killer odd rect",
    (const char *const[]){
    "killer=Visor^1Red\n"
    "paint 50 x=101.5 w=63\n"
    "width 50 0.5",
    NULL},
    (const char *const[]){
    "> 50\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 90 226 4.80000019 7.20000029 0.2734375 0 0.27734375 1\n"
    "pic font.normal 93.5999985 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 99.5999985 226 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 104.400002 226 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 109.800003 226 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 115.200005 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 119.400002 226 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 127.800003 226 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 133.199997 226 4.80000019 7.20000029 0.47265625 0 0.4765625 1\n"
    "pic font.normal 142.199997 226 4.80000019 7.20000029 0.3359375 0 0.33984375 1\n"
    "pic font.normal 146.399994 226 4.80000019 7.20000029 0.41015625 0 0.4140625 1\n"
    "pic font.normal 150 226 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 153.600006 226 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 157.800003 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color 1 0 0 0.800000012\n"
    "pic font.normal 163.800003 226 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 168.600006 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 172.800003 226 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "width 72",
    NULL}},
  {"medal accuracy zero",
    (const char *const[]){
    "paint 53",
    NULL},
    (const char *const[]){
    "> 53\n"
    "color 0.5 0.75 1 0.25\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"medal accuracy one",
    (const char *const[]){
    "score.0.accuracy=1\n"
    "paint 53",
    NULL},
    (const char *const[]){
    "> 53\n"
    "color 0.5 0.75 1 0.25\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 127 236 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "pic font.normal 133 236 4.80000019 7.20000029 0.14453125 0 0.1484375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal accuracy big bg",
    (const char *const[]){
    "score.1.accuracy=75\n"
    "selected_score=1\n"
    "paint 53 bg=medalbg",
    NULL},
    (const char *const[]){
    "> 53\n"
    "color 0.5 0.75 1 1\n"
    "pic medalbg 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 125.5 236 4.80000019 7.20000029 0.21484375 0 0.21875 1\n"
    "pic font.normal 129.100006 236 4.80000019 7.20000029 0.20703125 0 0.2109375 1\n"
    "pic font.normal 134.5 236 4.80000019 7.20000029 0.14453125 0 0.1484375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal accuracy lazy",
    (const char *const[]){
    "score.0.accuracy=3\n"
    "paint 53 bglazy=medallazy scale=0.5",
    NULL},
    (const char *const[]){
    "> 53\n"
    "color 0.5 0.75 1 0.25\n"
    "pic medallazy 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.big 128.5 237 4 6 0.19921875 0 0.203125 1\n"
    "pic font.big 132 237 4 6 0.14453125 0 0.1484375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal assist zero",
    (const char *const[]){
    "paint 54",
    NULL},
    (const char *const[]){
    "> 54\n"
    "color 0.5 0.75 1 0.25\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"medal assist one",
    (const char *const[]){
    "score.0.assist=1\n"
    "paint 54",
    NULL},
    (const char *const[]){
    "> 54\n"
    "color 0.5 0.75 1 1\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 129 236 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal assist big bg",
    (const char *const[]){
    "score.1.assist=75\n"
    "selected_score=1\n"
    "paint 54 bg=medalbg",
    NULL},
    (const char *const[]){
    "> 54\n"
    "color 0.5 0.75 1 1\n"
    "pic medalbg 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 127.5 236 4.80000019 7.20000029 0.21484375 0 0.21875 1\n"
    "pic font.normal 131.100006 236 4.80000019 7.20000029 0.20703125 0 0.2109375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal assist lazy",
    (const char *const[]){
    "score.0.assist=3\n"
    "paint 54 bglazy=medallazy scale=0.5",
    NULL},
    (const char *const[]){
    "> 54\n"
    "color 0.5 0.75 1 1\n"
    "pic medallazy 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.big 130.5 237 4 6 0.19921875 0 0.203125 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal defend zero",
    (const char *const[]){
    "paint 55",
    NULL},
    (const char *const[]){
    "> 55\n"
    "color 0.5 0.75 1 0.25\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"medal defend one",
    (const char *const[]){
    "score.0.defend=1\n"
    "paint 55",
    NULL},
    (const char *const[]){
    "> 55\n"
    "color 0.5 0.75 1 1\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 129 236 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal defend big bg",
    (const char *const[]){
    "score.1.defend=75\n"
    "selected_score=1\n"
    "paint 55 bg=medalbg",
    NULL},
    (const char *const[]){
    "> 55\n"
    "color 0.5 0.75 1 1\n"
    "pic medalbg 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 127.5 236 4.80000019 7.20000029 0.21484375 0 0.21875 1\n"
    "pic font.normal 131.100006 236 4.80000019 7.20000029 0.20703125 0 0.2109375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal defend lazy",
    (const char *const[]){
    "score.0.defend=3\n"
    "paint 55 bglazy=medallazy scale=0.5",
    NULL},
    (const char *const[]){
    "> 55\n"
    "color 0.5 0.75 1 1\n"
    "pic medallazy 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.big 130.5 237 4 6 0.19921875 0 0.203125 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal excellent zero",
    (const char *const[]){
    "paint 56",
    NULL},
    (const char *const[]){
    "> 56\n"
    "color 0.5 0.75 1 0.25\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"medal excellent one",
    (const char *const[]){
    "score.0.excellent=1\n"
    "paint 56",
    NULL},
    (const char *const[]){
    "> 56\n"
    "color 0.5 0.75 1 1\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 129 236 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal excellent big bg",
    (const char *const[]){
    "score.1.excellent=75\n"
    "selected_score=1\n"
    "paint 56 bg=medalbg",
    NULL},
    (const char *const[]){
    "> 56\n"
    "color 0.5 0.75 1 1\n"
    "pic medalbg 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 127.5 236 4.80000019 7.20000029 0.21484375 0 0.21875 1\n"
    "pic font.normal 131.100006 236 4.80000019 7.20000029 0.20703125 0 0.2109375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal excellent lazy",
    (const char *const[]){
    "score.0.excellent=3\n"
    "paint 56 bglazy=medallazy scale=0.5",
    NULL},
    (const char *const[]){
    "> 56\n"
    "color 0.5 0.75 1 1\n"
    "pic medallazy 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.big 130.5 237 4 6 0.19921875 0 0.203125 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal impressive zero",
    (const char *const[]){
    "paint 57",
    NULL},
    (const char *const[]){
    "> 57\n"
    "color 0.5 0.75 1 0.25\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"medal impressive one",
    (const char *const[]){
    "score.0.impressive=1\n"
    "paint 57",
    NULL},
    (const char *const[]){
    "> 57\n"
    "color 0.5 0.75 1 1\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 129 236 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal impressive big bg",
    (const char *const[]){
    "score.1.impressive=75\n"
    "selected_score=1\n"
    "paint 57 bg=medalbg",
    NULL},
    (const char *const[]){
    "> 57\n"
    "color 0.5 0.75 1 1\n"
    "pic medalbg 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 127.5 236 4.80000019 7.20000029 0.21484375 0 0.21875 1\n"
    "pic font.normal 131.100006 236 4.80000019 7.20000029 0.20703125 0 0.2109375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal impressive lazy",
    (const char *const[]){
    "score.0.impressive=3\n"
    "paint 57 bglazy=medallazy scale=0.5",
    NULL},
    (const char *const[]){
    "> 57\n"
    "color 0.5 0.75 1 1\n"
    "pic medallazy 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.big 130.5 237 4 6 0.19921875 0 0.203125 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal perfect zero",
    (const char *const[]){
    "paint 58",
    NULL},
    (const char *const[]){
    "> 58\n"
    "color 0.5 0.75 1 0.25\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"medal perfect one",
    (const char *const[]){
    "score.0.perfect=1\n"
    "paint 58",
    NULL},
    (const char *const[]){
    "> 58\n"
    "color 0.5 0.75 1 1\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 124.5 236 4.80000019 7.20000029 0.33984375 0 0.34375 1\n"
    "pic font.normal 129.300003 236 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 133.5 236 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal perfect big bg",
    (const char *const[]){
    "score.1.perfect=75\n"
    "selected_score=1\n"
    "paint 58 bg=medalbg",
    NULL},
    (const char *const[]){
    "> 58\n"
    "color 0.5 0.75 1 1\n"
    "pic medalbg 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 124.5 236 4.80000019 7.20000029 0.33984375 0 0.34375 1\n"
    "pic font.normal 129.300003 236 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 133.5 236 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal perfect lazy",
    (const char *const[]){
    "score.0.perfect=3\n"
    "paint 58 bglazy=medallazy scale=0.5",
    NULL},
    (const char *const[]){
    "> 58\n"
    "color 0.5 0.75 1 1\n"
    "pic medallazy 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.big 126 237 4 6 0.33984375 0 0.34375 1\n"
    "pic font.big 130 237 4 6 0.43359375 0 0.4375 1\n"
    "pic font.big 133.5 237 4 6 0.46484375 0 0.46875 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal gauntlet zero",
    (const char *const[]){
    "paint 59",
    NULL},
    (const char *const[]){
    "> 59\n"
    "color 0.5 0.75 1 0.25\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"medal gauntlet one",
    (const char *const[]){
    "score.0.gauntlet=1\n"
    "paint 59",
    NULL},
    (const char *const[]){
    "> 59\n"
    "color 0.5 0.75 1 1\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 129 236 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal gauntlet big bg",
    (const char *const[]){
    "score.1.gauntlet=75\n"
    "selected_score=1\n"
    "paint 59 bg=medalbg",
    NULL},
    (const char *const[]){
    "> 59\n"
    "color 0.5 0.75 1 1\n"
    "pic medalbg 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 127.5 236 4.80000019 7.20000029 0.21484375 0 0.21875 1\n"
    "pic font.normal 131.100006 236 4.80000019 7.20000029 0.20703125 0 0.2109375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal gauntlet lazy",
    (const char *const[]){
    "score.0.gauntlet=3\n"
    "paint 59 bglazy=medallazy scale=0.5",
    NULL},
    (const char *const[]){
    "> 59\n"
    "color 0.5 0.75 1 1\n"
    "pic medallazy 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.big 130.5 237 4 6 0.19921875 0 0.203125 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal captures zero",
    (const char *const[]){
    "paint 69",
    NULL},
    (const char *const[]){
    "> 69\n"
    "color 0.5 0.75 1 0.25\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color null",
    NULL}},
  {"medal captures one",
    (const char *const[]){
    "score.0.captures=1\n"
    "paint 69",
    NULL},
    (const char *const[]){
    "> 69\n"
    "color 0.5 0.75 1 1\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 129 236 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal captures big bg",
    (const char *const[]){
    "score.1.captures=75\n"
    "selected_score=1\n"
    "paint 69 bg=medalbg",
    NULL},
    (const char *const[]){
    "> 69\n"
    "color 0.5 0.75 1 1\n"
    "pic medalbg 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 127.5 236 4.80000019 7.20000029 0.21484375 0 0.21875 1\n"
    "pic font.normal 131.100006 236 4.80000019 7.20000029 0.20703125 0 0.2109375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal captures lazy",
    (const char *const[]){
    "score.0.captures=3\n"
    "paint 69 bglazy=medallazy scale=0.5",
    NULL},
    (const char *const[]){
    "> 69\n"
    "color 0.5 0.75 1 1\n"
    "pic medallazy 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.big 130.5 237 4 6 0.19921875 0 0.203125 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal accuracy 50",
    (const char *const[]){
    "score.0.accuracy=50\n"
    "paint 53",
    NULL},
    (const char *const[]){
    "> 53\n"
    "color 0.5 0.75 1 0.25\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 124.5 236 4.80000019 7.20000029 0.20703125 0 0.2109375 1\n"
    "pic font.normal 129.899994 236 4.80000019 7.20000029 0.1875 0 0.19140625 1\n"
    "pic font.normal 135.299988 236 4.80000019 7.20000029 0.14453125 0 0.1484375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal accuracy 51",
    (const char *const[]){
    "score.0.accuracy=51\n"
    "paint 53",
    NULL},
    (const char *const[]){
    "> 53\n"
    "color 0.5 0.75 1 1\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 124 236 4.80000019 7.20000029 0.20703125 0 0.2109375 1\n"
    "pic font.normal 129.399994 236 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "pic font.normal 135.399994 236 4.80000019 7.20000029 0.14453125 0 0.1484375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal accuracy fraction",
    (const char *const[]){
    "score.0.accuracy=33.75\n"
    "paint 53",
    NULL},
    (const char *const[]){
    "> 53\n"
    "color 0.5 0.75 1 0.25\n"
    "pic <null> 100 200 64 32 0 0 1 1\n"
    "color 0.5 0.75 1 1\n"
    "pic font.normal 125.5 236 4.80000019 7.20000029 0.19921875 0 0.203125 1\n"
    "pic font.normal 129.699997 236 4.80000019 7.20000029 0.19921875 0 0.203125 1\n"
    "pic font.normal 133.899994 236 4.80000019 7.20000029 0.14453125 0 0.1484375 1\n"
    "color null\n"
    "color null",
    NULL}},
  {"medal bad slot",
    (const char *const[]){
    "selected_score=64\n"
    "paint 54",
    NULL},
    (const char *const[]){
    "THROW Invalid owner-draw slot 64",
    NULL}},
  {"spectators empty",
    (const char *const[]){
    "paint 60",
    NULL},
    (const char *const[]){
    "> 60",
    NULL}},
  {"spectators scroll",
    (const char *const[]){
    "spectators=alpha beta gamma delta\n"
    "spectator_len=22\n"
    "spectator_width=-1\n"
    "spectator_paint_x2=-1\n"
    "time=70000\n"
    "paint 60\n"
    "time=70011\n"
    "paint 60\n"
    "time=70022\n"
    "paint 60\n"
    "time=70033\n"
    "paint 60\n"
    "time=70044\n"
    "paint 60\n"
    "time=70055\n"
    "paint 60\n"
    "time=70066\n"
    "paint 60\n"
    "time=70077\n"
    "paint 60\n"
    "time=70088\n"
    "paint 60\n"
    "time=70099\n"
    "paint 60\n"
    "time=70110\n"
    "paint 60\n"
    "time=70121\n"
    "paint 60\n"
    "time=70132\n"
    "paint 60\n"
    "time=70143\n"
    "paint 60\n"
    "time=70154\n"
    "paint 60\n"
    "time=70165\n"
    "paint 60\n"
    "time=70176\n"
    "paint 60\n"
    "time=70187\n"
    "paint 60\n"
    "time=70198\n"
    "paint 60\n"
    "time=70209\n"
    "paint 60\n"
    "time=70220\n"
    "paint 60\n"
    "time=70231\n"
    "paint 60\n"
    "time=70242\n"
    "paint 60\n"
    "time=70253\n"
    "paint 60\n"
    "time=70264\n"
    "paint 60\n"
    "time=70275\n"
    "paint 60\n"
    "time=70286\n"
    "paint 60\n"
    "time=70297\n"
    "paint 60\n"
    "time=70308\n"
    "paint 60\n"
    "time=70319\n"
    "paint 60\n"
    "time=70330\n"
    "paint 60\n"
    "time=70341\n"
    "paint 60\n"
    "time=70352\n"
    "paint 60\n"
    "time=70363\n"
    "paint 60\n"
    "time=70374\n"
    "paint 60\n"
    "time=70385\n"
    "paint 60\n"
    "time=70396\n"
    "paint 60\n"
    "time=70407\n"
    "paint 60\n"
    "time=70418\n"
    "paint 60\n"
    "time=70429\n"
    "paint 60\n"
    "time=70440\n"
    "paint 60\n"
    "time=70451\n"
    "paint 60\n"
    "time=70462\n"
    "paint 60\n"
    "time=70473\n"
    "paint 60\n"
    "time=70484\n"
    "paint 60\n"
    "time=70495\n"
    "paint 60\n"
    "time=70506\n"
    "paint 60\n"
    "time=70517\n"
    "paint 60\n"
    "time=70528\n"
    "paint 60\n"
    "time=70539\n"
    "paint 60\n"
    "time=70550\n"
    "paint 60\n"
    "time=70561\n"
    "paint 60\n"
    "time=70572\n"
    "paint 60\n"
    "time=70583\n"
    "paint 60\n"
    "time=70594\n"
    "paint 60\n"
    "time=70605\n"
    "paint 60\n"
    "time=70616\n"
    "paint 60\n"
    "time=70627\n"
    "paint 60\n"
    "time=70638\n"
    "paint 60\n"
    "time=70649\n"
    "paint 60\n"
    "time=70660\n"
    "paint 60\n"
    "time=70671\n"
    "paint 60\n"
    "time=70682\n"
    "paint 60\n"
    "time=70693\n"
    "paint 60\n"
    "time=70704\n"
    "paint 60\n"
    "time=70715\n"
    "paint 60\n"
    "time=70726\n"
    "paint 60\n"
    "time=70737\n"
    "paint 60\n"
    "time=70748\n"
    "paint 60\n"
    "time=70759\n"
    "paint 60",
    NULL},
    (const char *const[]){
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 109.400002 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 114.200005 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 120.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 129.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 135.199997 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 139.399994 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 143.599991 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 153.199997 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 108.400002 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 113.200005 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 119.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 128.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 134.199997 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 138.399994 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 142.599991 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 152.199997 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 157.599991 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 107.400002 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 112.200005 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 118.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 127.800011 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 133.200012 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 137.400009 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 141.600006 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 151.200012 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 156.600006 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 106 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 110.800003 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 116.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 126.400009 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 131.800003 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 136 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 140.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 149.800003 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 155.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 109.800003 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 115.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 125.400009 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 130.800003 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 135 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 139.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 148.800003 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 154.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "",
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 108.800003 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 114.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 124.400009 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 129.800003 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 134 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 138.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 147.800003 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 153.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 107.800003 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 113.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 123.400009 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 128.800003 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 133 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 137.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 146.800003 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 152.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 106.800003 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 112.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 122.400009 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 127.800011 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 132.000015 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 136.200012 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 145.800018 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 151.200012 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 111 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 120.600006 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 126.000008 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 130.200012 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 134.400009 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 144.000015 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 149.400009 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 154.200012 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 110 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 119.600006 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 125.000008 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 129.200012 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 133.400009 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 143.000015 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 148.400009 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 153.200012 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "",
    "pic font.normal 109 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 118.600006 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 124.000008 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 128.200012 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 132.400009 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 142.000015 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 147.400009 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 152.200012 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 108 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 117.600006 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 123.000008 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 127.200005 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 131.400009 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 141.000015 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 146.400009 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 151.200012 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 107 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 116.600006 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 122.000008 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 126.200005 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 130.400009 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 140.000015 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 145.400009 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 150.200012 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 106 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 115.600006 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 121.000008 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 125.200005 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 129.400009 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 139.000015 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 144.400009 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 149.200012 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 155.200012 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 114.600006 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 120.000008 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 124.200005 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 128.400009 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 138.000015 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 143.400009 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 148.200012 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 154.200012 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 113.600006 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 119.000008 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "",
    "pic font.normal 123.200005 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 127.400002 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 137 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 142.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 147.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 153.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 112.600006 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 118.000008 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 122.200005 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 126.400002 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 136 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 141.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 146.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 152.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 111.600006 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 117.000008 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 121.200005 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 125.400002 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 135 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 140.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 145.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 151.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 157.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 109.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 115.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 119.400002 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 123.599998 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 133.199997 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 138.599991 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 143.399994 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 149.399994 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 155.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 108.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 114.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 118.400002 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 122.599998 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 132.199997 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 137.599991 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 142.399994 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 148.399994 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 154.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 107.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 113.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 117.400002 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "",
    "pic font.normal 121.599998 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 131.199997 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 136.599991 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 141.399994 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 147.399994 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 153.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 106.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 112.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 116.400002 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 120.599998 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 130.199997 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 135.599991 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 140.399994 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 146.399994 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 152.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 110.400002 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 114.599998 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 118.799995 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 128.399994 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 133.799988 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 138.599991 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 144.599991 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 150.599991 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 109.400002 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 113.599998 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 117.799995 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 127.400002 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 132.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 137.600006 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 143.600006 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 149.600006 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 108.400002 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 112.599998 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 116.799995 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 126.400002 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 131.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 136.600006 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 142.600006 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 148.600006 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 158.200012 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 107.400002 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 111.599998 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "",
    "pic font.normal 115.799995 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 125.400002 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 130.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 135.600006 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 141.600006 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 147.600006 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 157.200012 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 106 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 110.199997 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 114.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 124 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 129.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 134.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 140.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 146.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 155.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 109.199997 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 113.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 123 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 128.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 133.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 139.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 145.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 154.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 108.199997 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 112.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 122 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 127.400002 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 132.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 138.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 144.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 153.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 157.400009 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 107.199997 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 111.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 121 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 126.400002 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 131.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 137.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 143.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 152.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 156.400009 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "",
    "pic font.normal 106.199997 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 110.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 120 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 125.400002 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 130.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 136.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 142.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 151.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 155.400009 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 109.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 118.800003 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 124.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 129 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 135 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 141 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 150.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 154.200012 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 108.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 117.800003 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 123.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 128 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 134 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 140 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 149.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 153.200012 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 107.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 116.800003 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 122.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 127.000008 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 133 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 139 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 148.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 152.200012 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 156.400009 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 106.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 115.800003 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 121.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 126.000008 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 132 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 138 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 147.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 151.200012 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 155.400009 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "",
    "pic font.normal 105 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 114.600006 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 120.000008 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 124.800011 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 130.800018 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 136.800018 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 146.400024 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 150.000031 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 154.200027 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 113.600006 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 119.000008 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 123.800011 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 129.800018 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 135.800018 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 145.400024 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 149.000031 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 153.200027 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 112.600006 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 118.000008 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 122.800011 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 128.800018 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 134.800018 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 144.400024 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 148.000031 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 152.200027 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 157.600021 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 111.600006 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 117.000008 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 121.800011 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 127.800011 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 133.800018 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 143.400024 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 147.000031 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 151.200027 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 156.600021 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 109.800003 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 115.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 120.000008 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 126.000008 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 132 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 141.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 145.200012 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 149.400009 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 154.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "",
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 108.800003 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 114.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 119.000008 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 125.000008 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 131 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 140.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 144.200012 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 148.400009 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 153.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 158 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 107.800003 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 113.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 118.000008 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 124.000008 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 130 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 139.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 143.200012 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 147.400009 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 152.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 157 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 106.800003 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 112.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 117.000008 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 123.000008 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 129 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 138.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 142.200012 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 146.400009 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 151.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 156 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 110.400002 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 115.200005 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 121.200005 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 127.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 136.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 140.400009 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 144.600006 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 150 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 154.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 109.400002 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 114.200005 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 120.200005 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 126.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "",
    "pic font.normal 135.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 139.400009 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 143.600006 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 149 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 153.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 108.400002 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 113.200005 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 119.200005 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 125.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 134.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 138.400009 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 142.600006 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 148 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 152.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 158 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 107.400002 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 112.200005 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 118.200005 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 124.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 133.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 137.400009 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 141.600006 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 147 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 151.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 157 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 106 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 110.800003 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 116.800003 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 122.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 132.400009 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 136.000015 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 140.200012 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 145.600006 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 149.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 157 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 109.800003 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 115.800003 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 121.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 131.400009 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 135.000015 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 139.200012 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 144.600006 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "",
    "pic font.normal 148.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 156 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 108.800003 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 114.800003 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 120.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 130.400009 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 134.000015 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 138.200012 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 143.600006 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 147.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 155 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 107.800003 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 113.800003 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 119.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 129.400009 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 133.000015 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 137.200012 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 142.600006 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 146.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 154 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 106.800003 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 112.800003 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 118.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 128.400009 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 132.000015 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 136.200012 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 141.600006 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 145.800003 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 153 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 111 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 117 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 126.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 130.200012 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 134.400009 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 139.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 144 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 153 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 110 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 116 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "",
    "pic font.normal 125.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 129.200012 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 133.400009 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 138.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 143 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 152 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 156.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 109 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 115 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 124.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 128.200012 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 132.400009 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 137.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 142 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 151 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 155.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 108 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 114 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 123.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 127.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 131.400009 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 136.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 141 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 150 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 154.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 107 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 113 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 122.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 126.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 130.400009 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 135.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 140 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 150 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 154.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 106 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 112 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 121.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 125.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 129.400009 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 134.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 139 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 149 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 153.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "",
    "pic font.normal 105 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 111 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 120.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 124.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 128.400009 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 133.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 138 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 148 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 152.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 110 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 119.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 123.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 127.400002 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 132.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 137 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 147 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 151.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 157.199997 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 109 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 118.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 122.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 126.400002 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 131.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 136 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 146 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 150.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 156.199997 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 108 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 117.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 121.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 125.400002 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 130.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 135 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 145 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 149.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 155.199997 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 107 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 116.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 120.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 124.400002 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 129.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 134 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "",
    "pic font.normal 145 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 149.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 155.199997 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 106 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 115.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 119.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 123.400002 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 128.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 133 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 144 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 148.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 154.199997 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 114.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 118.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 122.400002 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 127.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 132 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 143 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 147.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 153.199997 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 113.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 117.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 121.400002 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 126.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 131 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 142 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 146.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 152.199997 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 112.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 116.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 120.400002 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 125.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 130 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 141 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 145.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 151.199997 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 156 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 111.600006 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 115.200005 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 119.400002 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 124.800003 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "",
    "pic font.normal 129 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 140 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 144.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 150.199997 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 155 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 109.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 113.400002 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 117.599998 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 123 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 127.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 140 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 144.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 150.199997 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 155 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 108.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 112.400002 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 116.599998 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 122 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 126.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 139 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 143.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 149.199997 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 154 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "color null",
    NULL}},
  {"spectators wrap",
    (const char *const[]){
    "spectators=alpha beta gamma delta\n"
    "spectator_len=22\n"
    "spectator_width=-1\n"
    "spectator_paint_x2=-1\n"
    "spectator_len=5\n"
    "spectators=ab cd\n"
    "spectator_paint_x=95\n"
    "spectator_width=0\n"
    "time=70000\n"
    "paint 60 x=100 w=40\n"
    "time=70011\n"
    "paint 60 x=100 w=40\n"
    "time=70022\n"
    "paint 60 x=100 w=40\n"
    "time=70033\n"
    "paint 60 x=100 w=40\n"
    "time=70044\n"
    "paint 60 x=100 w=40\n"
    "time=70055\n"
    "paint 60 x=100 w=40\n"
    "time=70066\n"
    "paint 60 x=100 w=40\n"
    "time=70077\n"
    "paint 60 x=100 w=40\n"
    "time=70088\n"
    "paint 60 x=100 w=40\n"
    "time=70099\n"
    "paint 60 x=100 w=40\n"
    "time=70110\n"
    "paint 60 x=100 w=40\n"
    "time=70121\n"
    "paint 60 x=100 w=40\n"
    "time=70132\n"
    "paint 60 x=100 w=40\n"
    "time=70143\n"
    "paint 60 x=100 w=40\n"
    "time=70154\n"
    "paint 60 x=100 w=40\n"
    "time=70165\n"
    "paint 60 x=100 w=40\n"
    "time=70176\n"
    "paint 60 x=100 w=40\n"
    "time=70187\n"
    "paint 60 x=100 w=40\n"
    "time=70198\n"
    "paint 60 x=100 w=40\n"
    "time=70209\n"
    "paint 60 x=100 w=40\n"
    "time=70220\n"
    "paint 60 x=100 w=40\n"
    "time=70231\n"
    "paint 60 x=100 w=40\n"
    "time=70242\n"
    "paint 60 x=100 w=40\n"
    "time=70253\n"
    "paint 60 x=100 w=40\n"
    "time=70264\n"
    "paint 60 x=100 w=40\n"
    "time=70275\n"
    "paint 60 x=100 w=40\n"
    "time=70286\n"
    "paint 60 x=100 w=40\n"
    "time=70297\n"
    "paint 60 x=100 w=40\n"
    "time=70308\n"
    "paint 60 x=100 w=40\n"
    "time=70319\n"
    "paint 60 x=100 w=40\n"
    "time=70330\n"
    "paint 60 x=100 w=40\n"
    "time=70341\n"
    "paint 60 x=100 w=40\n"
    "time=70352\n"
    "paint 60 x=100 w=40\n"
    "time=70363\n"
    "paint 60 x=100 w=40\n"
    "time=70374\n"
    "paint 60 x=100 w=40\n"
    "time=70385\n"
    "paint 60 x=100 w=40\n"
    "time=70396\n"
    "paint 60 x=100 w=40\n"
    "time=70407\n"
    "paint 60 x=100 w=40\n"
    "time=70418\n"
    "paint 60 x=100 w=40\n"
    "time=70429\n"
    "paint 60 x=100 w=40\n"
    "time=70440\n"
    "paint 60 x=100 w=40\n"
    "time=70451\n"
    "paint 60 x=100 w=40\n"
    "time=70462\n"
    "paint 60 x=100 w=40\n"
    "time=70473\n"
    "paint 60 x=100 w=40\n"
    "time=70484\n"
    "paint 60 x=100 w=40\n"
    "time=70495\n"
    "paint 60 x=100 w=40\n"
    "time=70506\n"
    "paint 60 x=100 w=40\n"
    "time=70517\n"
    "paint 60 x=100 w=40\n"
    "time=70528\n"
    "paint 60 x=100 w=40\n"
    "time=70539\n"
    "paint 60 x=100 w=40\n"
    "time=70550\n"
    "paint 60 x=100 w=40\n"
    "time=70561\n"
    "paint 60 x=100 w=40\n"
    "time=70572\n"
    "paint 60 x=100 w=40\n"
    "time=70583\n"
    "paint 60 x=100 w=40\n"
    "time=70594\n"
    "paint 60 x=100 w=40\n"
    "time=70605\n"
    "paint 60 x=100 w=40\n"
    "time=70616\n"
    "paint 60 x=100 w=40\n"
    "time=70627\n"
    "paint 60 x=100 w=40\n"
    "time=70638\n"
    "paint 60 x=100 w=40\n"
    "time=70649\n"
    "paint 60 x=100 w=40\n"
    "time=70660\n"
    "paint 60 x=100 w=40\n"
    "time=70671\n"
    "paint 60 x=100 w=40\n"
    "time=70682\n"
    "paint 60 x=100 w=40\n"
    "time=70693\n"
    "paint 60 x=100 w=40\n"
    "time=70704\n"
    "paint 60 x=100 w=40\n"
    "time=70715\n"
    "paint 60 x=100 w=40\n"
    "time=70726\n"
    "paint 60 x=100 w=40\n"
    "time=70737\n"
    "paint 60 x=100 w=40\n"
    "time=70748\n"
    "paint 60 x=100 w=40\n"
    "time=70759\n"
    "paint 60 x=100 w=40",
    NULL},
    (const char *const[]){
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 98 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 108.200005 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 114.200005 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 106.800003 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 112.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 111 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 110 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 109 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 108 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 107 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 106 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 134 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 133 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 132 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 131 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 130 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 130 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 129 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 128 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 132.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 128 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 132.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 127 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 131.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 126 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "",
    "pic font.normal 130.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 125 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 129.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 124 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 128.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 123 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 127.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 122 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 126.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 121 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 125.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 120 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 124.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 119 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 123.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 118 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 122.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 117 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 121.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 132 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 116 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 120.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 131 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 115 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 119.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 130 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 114 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 118.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 129 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 135 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 113 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 117.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 128 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 134 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 112 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 116.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 127.000008 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 133 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 111 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 115.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 126.000008 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 132 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "",
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 110 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 114.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 125.000008 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 131 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 109 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 113.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 124.000008 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 130 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 108 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 112.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 123.000008 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 129 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 107 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 111.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 122.000008 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 128 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 106 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 110.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 121.000008 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 127.000008 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 109.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 120.000008 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 126.000008 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 108.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 119.000008 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 125.000008 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 107.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 118.000008 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 124.000008 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 106.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 117.000008 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 123.000008 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 115.200005 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 121.200005 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 114.200005 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 120.200005 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "",
    "pic font.normal 113.200005 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 119.200005 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 112.200005 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 118.200005 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 110.800003 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 116.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 109.800003 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 115.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 134 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 108.800003 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 114.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 133 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 107.800003 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 113.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 132 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 106.800003 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 112.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 131 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 111 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 131 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 110 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 130 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 109 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 129 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "pic font.normal 108 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 128 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 132.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 107 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 128 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 132.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 106 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 127 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "",
    "pic font.normal 131.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 105 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 126 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 130.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 125 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 129.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 124 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 128.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 102 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 123 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 127.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 123 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 127.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 122 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 126.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 121 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 125.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 121 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 125.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 120 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 124.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 119 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 123.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 118 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 122.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 117 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 121.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 132 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 116 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 120.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 131 223 4.80000019 7.20000029 0.38671875 0 0.390625 1\n"
    "color null",
    NULL}},
  {"spectators no tick",
    (const char *const[]){
    "spectators=alpha beta gamma delta\n"
    "spectator_len=22\n"
    "spectator_width=-1\n"
    "spectator_paint_x2=-1\n"
    "spectator_time=90000\n"
    "paint 60",
    NULL},
    (const char *const[]){
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 101 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 105.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 111.200005 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 116.000008 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 122.000008 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 131.600006 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 137 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 141.199997 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 145.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 155 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "color null",
    NULL}},
  {"spectators reset offset",
    (const char *const[]){
    "spectators=alpha beta gamma delta\n"
    "spectator_len=22\n"
    "spectator_width=-1\n"
    "spectator_paint_x2=-1\n"
    "spectator_width=0\n"
    "spectator_offset=30\n"
    "spectator_paint_x=300\n"
    "paint 60",
    NULL},
    (const char *const[]){
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 109.400002 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 114.200005 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 120.200005 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 129.800003 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 135.199997 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 139.399994 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 143.599991 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 153.199997 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "color null",
    NULL}},
  {"spectators off end",
    (const char *const[]){
    "spectators=alpha beta gamma delta\n"
    "spectator_len=22\n"
    "spectator_width=-1\n"
    "spectator_paint_x2=-1\n"
    "spectator_width=0\n"
    "spectator_offset=22\n"
    "spectator_paint_x=50\n"
    "spectator_paint_x2=120\n"
    "time=70001\n"
    "paint 60",
    NULL},
    (const char *const[]){
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 120 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 124.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 130.199997 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "pic font.normal 135 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 141 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 150.600006 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 156 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null",
    NULL}},
  {"spectators x2 decrement",
    (const char *const[]){
    "spectators=alpha beta gamma delta\n"
    "spectator_len=22\n"
    "spectator_width=-1\n"
    "spectator_paint_x2=-1\n"
    "spectator_width=0\n"
    "spectator_offset=3\n"
    "spectator_paint_x=150\n"
    "spectator_paint_x2=160\n"
    "paint 60 w=300",
    NULL},
    (const char *const[]){
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 149 223 4.80000019 7.20000029 0.40625 0 0.41015625 1\n"
    "pic font.normal 155 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 164.600006 223 4.80000019 7.20000029 0.3828125 0 0.38671875 1\n"
    "pic font.normal 170 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 174.199997 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 178.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 188 223 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 193.399994 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 198.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 204.199997 223 4.80000019 7.20000029 0.42578125 0 0.4296875 1\n"
    "pic font.normal 210.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 219.800003 223 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 223.400009 223 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 227.600006 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 233 223 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 237.199997 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 159 223 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 163.800003 223 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 169.199997 223 4.80000019 7.20000029 0.4375 0 0.44140625 1\n"
    "color null",
    NULL}},
  {"spectators scale big",
    (const char *const[]){
    "spectators=alpha beta gamma delta\n"
    "spectator_len=22\n"
    "spectator_width=-1\n"
    "spectator_paint_x2=-1\n"
    "time=70000\n"
    "paint 60 scale=0.5\n"
    "time=70011\n"
    "paint 60 scale=0.5\n"
    "time=70022\n"
    "paint 60 scale=0.5\n"
    "time=70033\n"
    "paint 60 scale=0.5",
    NULL},
    (const char *const[]){
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.big 104 224 4 6 0.421875 0 0.42578125 1\n"
    "pic font.big 108.5 224 4 6 0.4375 0 0.44140625 1\n"
    "pic font.big 112.5 224 4 6 0.40625 0 0.41015625 1\n"
    "pic font.big 117.5 224 4 6 0.37890625 0 0.3828125 1\n"
    "pic font.big 125.5 224 4 6 0.3828125 0 0.38671875 1\n"
    "pic font.big 130 224 4 6 0.39453125 0 0.3984375 1\n"
    "pic font.big 133.5 224 4 6 0.453125 0 0.45703125 1\n"
    "pic font.big 137 224 4 6 0.37890625 0 0.3828125 1\n"
    "pic font.big 145 224 4 6 0.40234375 0 0.40625 1\n"
    "pic font.big 149.5 224 4 6 0.37890625 0 0.3828125 1\n"
    "pic font.big 153.5 224 4 6 0.42578125 0 0.4296875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.big 103 224 4 6 0.421875 0 0.42578125 1\n"
    "pic font.big 107.5 224 4 6 0.4375 0 0.44140625 1\n"
    "pic font.big 111.5 224 4 6 0.40625 0 0.41015625 1\n"
    "pic font.big 116.5 224 4 6 0.37890625 0 0.3828125 1\n"
    "pic font.big 124.5 224 4 6 0.3828125 0 0.38671875 1\n"
    "pic font.big 129 224 4 6 0.39453125 0 0.3984375 1\n"
    "pic font.big 132.5 224 4 6 0.453125 0 0.45703125 1\n"
    "pic font.big 136 224 4 6 0.37890625 0 0.3828125 1\n"
    "pic font.big 144 224 4 6 0.40234375 0 0.40625 1\n"
    "pic font.big 148.5 224 4 6 0.37890625 0 0.3828125 1\n"
    "pic font.big 152.5 224 4 6 0.42578125 0 0.4296875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.big 102 224 4 6 0.421875 0 0.42578125 1\n"
    "pic font.big 106.5 224 4 6 0.4375 0 0.44140625 1\n"
    "pic font.big 110.5 224 4 6 0.40625 0 0.41015625 1\n"
    "pic font.big 115.5 224 4 6 0.37890625 0 0.3828125 1\n"
    "pic font.big 123.5 224 4 6 0.3828125 0 0.38671875 1\n"
    "pic font.big 128 224 4 6 0.39453125 0 0.3984375 1\n"
    "pic font.big 131.5 224 4 6 0.453125 0 0.45703125 1\n"
    "pic font.big 135 224 4 6 0.37890625 0 0.3828125 1\n"
    "pic font.big 143 224 4 6 0.40234375 0 0.40625 1\n"
    "pic font.big 147.5 224 4 6 0.37890625 0 0.3828125 1\n"
    "pic font.big 151.5 224 4 6 0.42578125 0 0.4296875 1\n"
    "pic font.big 156.5 224 4 6 0.42578125 0 0.4296875 1\n"
    "color null\n"
    "> 60\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.big 105 224 4 6 0.4375 0 0.44140625 1\n"
    "pic font.big 109 224 4 6 0.40625 0 0.41015625 1\n"
    "pic font.big 114 224 4 6 0.37890625 0 0.3828125 1\n"
    "pic font.big 122 224 4 6 0.3828125 0 0.38671875 1\n"
    "pic font.big 126.5 224 4 6 0.39453125 0 0.3984375 1\n"
    "pic font.big 130 224 4 6 0.453125 0 0.45703125 1\n"
    "pic font.big 133.5 224 4 6 0.37890625 0 0.3828125 1\n"
    "pic font.big 141.5 224 4 6 0.40234375 0 0.40625 1\n"
    "pic font.big 146 224 4 6 0.37890625 0 0.3828125 1\n"
    "pic font.big 150 224 4 6 0.42578125 0 0.4296875 1\n"
    "pic font.big 155 224 4 6 0.42578125 0 0.4296875 1\n"
    "color null",
    NULL}},
  {"teaminfo mismatch",
    (const char *const[]){
    "cvar.cg_currentSelectedPlayer=1\n"
    "paint 61",
    NULL},
    (const char *const[]){
    "> 61",
    NULL}},
  {"teaminfo",
    (const char *const[]){
    "cvar.cg_currentSelectedPlayer=3\n"
    "paint 61 w=300 h=200",
    NULL},
    (const char *const[]){
    "> 61\n"
    "color 1 1 1 1\n"
    "pic g.heartShader 138 201 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.defendShader 151 200 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 206 4.80000019 7.20000029 0.27734375 0 0.28125 1\n"
    "pic font.normal 168.199997 206 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 174.199997 206 4.80000019 7.20000029 0.45703125 0 0.4609375 1\n"
    "pic font.normal 179 206 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 182.600006 206 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 206 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 215.800003 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 220 206 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 228.400009 206 4.80000019 7.20000029 0.2578125 0 0.26171875 1\n"
    "pic font.normal 232.600006 206 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 237.400009 206 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 241.000015 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "shader icons/quad\n"
    "pic icons/quad 101 214 12 12 0 0 1 1\n"
    "shader icons/iconf_red1\n"
    "pic icons/iconf_red1 113 214 12 12 0 0 1 1\n"
    "color 1 0.333333343 0 1\n"
    "pic g.heartShader 138 215 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.patrolShader 151 214 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 220 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 168.800003 220 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 173.600006 220 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 177.200012 220 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 182.600006 220 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 186.800003 220 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 220 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 217 220 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 221.199997 220 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "pic font.normal 227.199997 220 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 231.399994 220 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null",
    NULL}},
  {"teaminfo text_y",
    (const char *const[]){
    "cvar.cg_currentSelectedPlayer=3\n"
    "paint 61 x=20.5 w=300 h=200 text_y=9 scale=0.2",
    NULL},
    (const char *const[]){
    "> 61\n"
    "color 1 1 1 1\n"
    "pic g.heartShader 58 201 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.defendShader 71 200 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.small 84 201 6.4000001 9.60000038 0.27734375 0 0.28125 1\n"
    "pic font.small 89.5999985 201 6.4000001 9.60000038 0.4453125 0 0.44921875 1\n"
    "pic font.small 97.5999985 201 6.4000001 9.60000038 0.45703125 0 0.4609375 1\n"
    "pic font.small 104 201 6.4000001 9.60000038 0.4296875 0 0.43359375 1\n"
    "pic font.small 108.800003 201 6.4000001 9.60000038 0.453125 0 0.45703125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.small 158 201 6.4000001 9.60000038 0.3203125 0 0.32421875 1\n"
    "pic font.small 164.399994 201 6.4000001 9.60000038 0.39453125 0 0.3984375 1\n"
    "pic font.small 170 201 6.4000001 9.60000038 0.390625 0 0.39453125 1\n"
    "pic font.small 181.199997 201 6.4000001 9.60000038 0.2578125 0 0.26171875 1\n"
    "pic font.small 186.800003 201 6.4000001 9.60000038 0.37890625 0 0.3828125 1\n"
    "pic font.small 193.199997 201 6.4000001 9.60000038 0.44921875 0 0.453125 1\n"
    "pic font.small 198 201 6.4000001 9.60000038 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "shader icons/quad\n"
    "pic icons/quad 21 211 12 12 0 0 1 1\n"
    "shader icons/iconf_red1\n"
    "pic icons/iconf_red1 33 211 12 12 0 0 1 1\n"
    "color 1 0.333333343 0 1\n"
    "pic g.heartShader 58 212 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.patrolShader 71 211 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.small 84 212 6.4000001 9.60000038 0.3203125 0 0.32421875 1\n"
    "pic font.small 90.4000015 212 6.4000001 9.60000038 0.37890625 0 0.3828125 1\n"
    "pic font.small 96.8000031 212 6.4000001 9.60000038 0.4296875 0 0.43359375 1\n"
    "pic font.small 101.600006 212 6.4000001 9.60000038 0.40234375 0 0.40625 1\n"
    "pic font.small 108.800003 212 6.4000001 9.60000038 0.39453125 0 0.3984375 1\n"
    "pic font.small 114.400002 212 6.4000001 9.60000038 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.small 158 212 6.4000001 9.60000038 0.328125 0 0.33203125 1\n"
    "pic font.small 166 212 6.4000001 9.60000038 0.43359375 0 0.4375 1\n"
    "pic font.small 171.600006 212 6.4000001 9.60000038 0.46484375 0 0.46875 1\n"
    "pic font.small 179.600006 212 6.4000001 9.60000038 0.39453125 0 0.3984375 1\n"
    "pic font.small 185.200012 212 6.4000001 9.60000038 0.4453125 0 0.44921875 1\n"
    "color null",
    NULL}},
  {"teaminfo clip",
    (const char *const[]){
    "cvar.cg_currentSelectedPlayer=3\n"
    "paint 61 w=300 h=20",
    NULL},
    (const char *const[]){
    "> 61\n"
    "color 1 1 1 1\n"
    "pic g.heartShader 138 201 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.defendShader 151 200 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 206 4.80000019 7.20000029 0.27734375 0 0.28125 1\n"
    "pic font.normal 168.199997 206 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 174.199997 206 4.80000019 7.20000029 0.45703125 0 0.4609375 1\n"
    "pic font.normal 179 206 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 182.600006 206 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 206 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 215.800003 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 220 206 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 228.400009 206 4.80000019 7.20000029 0.2578125 0 0.26171875 1\n"
    "pic font.normal 232.600006 206 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 237.400009 206 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 241.000015 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null",
    NULL}},
  {"teaminfo narrow",
    (const char *const[]){
    "cvar.cg_currentSelectedPlayer=3\n"
    "paint 61 x=0 w=90 h=200",
    NULL},
    (const char *const[]){
    "> 61\n"
    "color 1 1 1 1\n"
    "pic g.heartShader 38 201 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.defendShader 51 200 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 64 206 4.80000019 7.20000029 0.27734375 0 0.28125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 74 206 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 78.8000031 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 83 206 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "color null\n"
    "shader icons/quad\n"
    "pic icons/quad 1 214 12 12 0 0 1 1\n"
    "shader icons/iconf_red1\n"
    "pic icons/iconf_red1 13 214 12 12 0 0 1 1\n"
    "color 1 0.333333343 0 1\n"
    "pic g.heartShader 38 215 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.patrolShader 51 214 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 64 220 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 74 220 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 80 220 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "color null",
    NULL}},
  {"teaminfo blink",
    (const char *const[]){
    "cvar.cg_currentSelectedPlayer=3\n"
    "order_pending=1\n"
    "current_order=7\n"
    "order_time=80000\n"
    "time=70656\n"
    "paint 61 w=300 h=200",
    NULL},
    (const char *const[]){
    "> 61\n"
    "color 1 1 1 1\n"
    "pic g.heartShader 138 201 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.campShader 151 200 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 206 4.80000019 7.20000029 0.27734375 0 0.28125 1\n"
    "pic font.normal 168.199997 206 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 174.199997 206 4.80000019 7.20000029 0.45703125 0 0.4609375 1\n"
    "pic font.normal 179 206 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 182.600006 206 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 206 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 215.800003 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 220 206 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 228.400009 206 4.80000019 7.20000029 0.2578125 0 0.26171875 1\n"
    "pic font.normal 232.600006 206 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 237.400009 206 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 241.000015 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "shader icons/quad\n"
    "pic icons/quad 101 214 12 12 0 0 1 1\n"
    "shader icons/iconf_red1\n"
    "pic icons/iconf_red1 113 214 12 12 0 0 1 1\n"
    "color 1 0.333333343 0 1\n"
    "pic g.heartShader 138 215 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.campShader 151 214 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 220 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 168.800003 220 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 173.600006 220 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 177.200012 220 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 182.600006 220 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 186.800003 220 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 220 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 217 220 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 221.199997 220 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "pic font.normal 227.199997 220 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 231.399994 220 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null",
    NULL}},
  {"teaminfo order",
    (const char *const[]){
    "cvar.cg_currentSelectedPlayer=3\n"
    "order_pending=1\n"
    "current_order=7\n"
    "order_time=80000\n"
    "time=70144\n"
    "paint 61 w=300 h=200",
    NULL},
    (const char *const[]){
    "> 61\n"
    "color 1 1 1 1\n"
    "pic g.heartShader 138 201 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.campShader 151 200 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 206 4.80000019 7.20000029 0.27734375 0 0.28125 1\n"
    "pic font.normal 168.199997 206 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 174.199997 206 4.80000019 7.20000029 0.45703125 0 0.4609375 1\n"
    "pic font.normal 179 206 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 182.600006 206 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 206 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 215.800003 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 220 206 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 228.400009 206 4.80000019 7.20000029 0.2578125 0 0.26171875 1\n"
    "pic font.normal 232.600006 206 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 237.400009 206 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 241.000015 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "shader icons/quad\n"
    "pic icons/quad 101 214 12 12 0 0 1 1\n"
    "shader icons/iconf_red1\n"
    "pic icons/iconf_red1 113 214 12 12 0 0 1 1\n"
    "color 1 0.333333343 0 1\n"
    "pic g.heartShader 138 215 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.campShader 151 214 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 220 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 168.800003 220 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 173.600006 220 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 177.200012 220 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 182.600006 220 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 186.800003 220 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 220 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 217 220 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 221.199997 220 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "pic font.normal 227.199997 220 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 231.399994 220 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null",
    NULL}},
  {"teaminfo invalid",
    (const char *const[]){
    "cvar.cg_currentSelectedPlayer=3\n"
    "client.2.info_valid=0\n"
    "paint 61 w=300 h=200",
    NULL},
    (const char *const[]){
    "> 61\n"
    "shader icons/quad\n"
    "pic icons/quad 101 200 12 12 0 0 1 1\n"
    "shader icons/iconf_red1\n"
    "pic icons/iconf_red1 113 200 12 12 0 0 1 1\n"
    "color 1 0.333333343 0 1\n"
    "pic g.heartShader 138 201 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.patrolShader 151 200 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 206 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 168.800003 206 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 173.600006 206 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 177.200012 206 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 182.600006 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 186.800003 206 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 206 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 217 206 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 221.199997 206 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "pic font.normal 227.199997 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 231.399994 206 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null",
    NULL}},
  {"teaminfo powerups",
    (const char *const[]){
    "cvar.cg_currentSelectedPlayer=3\n"
    "client.2.powerups=33026\n"
    "paint 61 w=300 h=200",
    NULL},
    (const char *const[]){
    "> 61\n"
    "shader icons/quad\n"
    "pic icons/quad 101 200 12 12 0 0 1 1\n"
    "shader icons/iconf_blu1\n"
    "pic icons/iconf_blu1 113 200 12 12 0 0 1 1\n"
    "color 1 1 1 1\n"
    "pic g.heartShader 138 201 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.defendShader 151 200 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 206 4.80000019 7.20000029 0.27734375 0 0.28125 1\n"
    "pic font.normal 168.199997 206 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 174.199997 206 4.80000019 7.20000029 0.45703125 0 0.4609375 1\n"
    "pic font.normal 179 206 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 182.600006 206 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 206 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 215.800003 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 220 206 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 228.400009 206 4.80000019 7.20000029 0.2578125 0 0.26171875 1\n"
    "pic font.normal 232.600006 206 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 237.400009 206 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 241.000015 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "shader icons/quad\n"
    "pic icons/quad 101 214 12 12 0 0 1 1\n"
    "shader icons/iconf_red1\n"
    "pic icons/iconf_red1 113 214 12 12 0 0 1 1\n"
    "color 1 0.333333343 0 1\n"
    "pic g.heartShader 138 215 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.patrolShader 151 214 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 220 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 168.800003 220 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 173.600006 220 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 177.200012 220 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 182.600006 220 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 186.800003 220 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 220 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 217 220 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 221.199997 220 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "pic font.normal 227.199997 220 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 231.399994 220 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null",
    NULL}},
  {"teaminfo cap",
    (const char *const[]){
    "cvar.cg_currentSelectedPlayer=10\n"
    "num_sorted=10\n"
    "sorted.3=2\n"
    "sorted.4=5\n"
    "sorted.5=2\n"
    "sorted.6=5\n"
    "sorted.7=2\n"
    "sorted.8=5\n"
    "sorted.9=2\n"
    "paint 61 w=300 h=400",
    NULL},
    (const char *const[]){
    "> 61\n"
    "color 1 1 1 1\n"
    "pic g.heartShader 138 201 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.defendShader 151 200 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 206 4.80000019 7.20000029 0.27734375 0 0.28125 1\n"
    "pic font.normal 168.199997 206 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 174.199997 206 4.80000019 7.20000029 0.45703125 0 0.4609375 1\n"
    "pic font.normal 179 206 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 182.600006 206 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 206 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 215.800003 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 220 206 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 228.400009 206 4.80000019 7.20000029 0.2578125 0 0.26171875 1\n"
    "pic font.normal 232.600006 206 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 237.400009 206 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 241.000015 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "shader icons/quad\n"
    "pic icons/quad 101 214 12 12 0 0 1 1\n"
    "shader icons/iconf_red1\n"
    "pic icons/iconf_red1 113 214 12 12 0 0 1 1\n"
    "color 1 0.333333343 0 1\n"
    "pic g.heartShader 138 215 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.patrolShader 151 214 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 220 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 168.800003 220 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 173.600006 220 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 177.200012 220 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 182.600006 220 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 186.800003 220 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 220 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 217 220 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 221.199997 220 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "pic font.normal 227.199997 220 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 231.399994 220 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "color 1 1 1 1\n"
    "pic g.heartShader 138 229 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.defendShader 151 228 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 234 4.80000019 7.20000029 0.27734375 0 0.28125 1\n"
    "pic font.normal 168.199997 234 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 174.199997 234 4.80000019 7.20000029 0.45703125 0 0.4609375 1\n"
    "pic font.normal 179 234 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 182.600006 234 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 234 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 215.800003 234 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 220 234 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 228.400009 234 4.80000019 7.20000029 0.2578125 0 0.26171875 1\n"
    "pic font.normal 232.600006 234 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 237.400009 234 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 241.000015 234 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "shader icons/quad\n"
    "pic icons/quad 101 242 12 12 0 0 1 1\n"
    "shader icons/iconf_red1\n"
    "pic icons/iconf_red1 113 242 12 12 0 0 1 1\n"
    "color 1 0.333333343 0 1\n"
    "pic g.heartShader 138 243 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.patrolShader 151 242 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 248 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 168.800003 248 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "",
    "pic font.normal 173.600006 248 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 177.200012 248 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 182.600006 248 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 186.800003 248 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 248 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 217 248 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 221.199997 248 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "pic font.normal 227.199997 248 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 231.399994 248 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "color 1 1 1 1\n"
    "pic g.heartShader 138 257 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.defendShader 151 256 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 262 4.80000019 7.20000029 0.27734375 0 0.28125 1\n"
    "pic font.normal 168.199997 262 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 174.199997 262 4.80000019 7.20000029 0.45703125 0 0.4609375 1\n"
    "pic font.normal 179 262 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 182.600006 262 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 262 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 215.800003 262 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 220 262 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 228.400009 262 4.80000019 7.20000029 0.2578125 0 0.26171875 1\n"
    "pic font.normal 232.600006 262 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 237.400009 262 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 241.000015 262 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "shader icons/quad\n"
    "pic icons/quad 101 270 12 12 0 0 1 1\n"
    "shader icons/iconf_red1\n"
    "pic icons/iconf_red1 113 270 12 12 0 0 1 1\n"
    "color 1 0.333333343 0 1\n"
    "pic g.heartShader 138 271 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.patrolShader 151 270 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 276 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 168.800003 276 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 173.600006 276 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 177.200012 276 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 182.600006 276 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 186.800003 276 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 276 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 217 276 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 221.199997 276 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "pic font.normal 227.199997 276 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 231.399994 276 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "color 1 1 1 1\n"
    "pic g.heartShader 138 285 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.defendShader 151 284 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 290 4.80000019 7.20000029 0.27734375 0 0.28125 1\n"
    "pic font.normal 168.199997 290 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 174.199997 290 4.80000019 7.20000029 0.45703125 0 0.4609375 1\n"
    "pic font.normal 179 290 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 182.600006 290 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 290 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 215.800003 290 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 220 290 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "",
    "pic font.normal 228.400009 290 4.80000019 7.20000029 0.2578125 0 0.26171875 1\n"
    "pic font.normal 232.600006 290 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 237.400009 290 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 241.000015 290 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null",
    NULL}},
  {"teaminfo health colors",
    (const char *const[]){
    "cvar.cg_currentSelectedPlayer=3\n"
    "client.2.health=-5\n"
    "client.5.health=200\n"
    "client.5.armor=200\n"
    "paint 61 w=300 h=200",
    NULL},
    (const char *const[]){
    "> 61\n"
    "color 0 0 0 1\n"
    "pic g.heartShader 138 201 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.defendShader 151 200 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 206 4.80000019 7.20000029 0.27734375 0 0.28125 1\n"
    "pic font.normal 168.199997 206 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 174.199997 206 4.80000019 7.20000029 0.45703125 0 0.4609375 1\n"
    "pic font.normal 179 206 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 182.600006 206 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 206 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 215.800003 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 220 206 4.80000019 7.20000029 0.390625 0 0.39453125 1\n"
    "pic font.normal 228.400009 206 4.80000019 7.20000029 0.2578125 0 0.26171875 1\n"
    "pic font.normal 232.600006 206 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 237.400009 206 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "pic font.normal 241.000015 206 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "color null\n"
    "shader icons/quad\n"
    "pic icons/quad 101 214 12 12 0 0 1 1\n"
    "shader icons/iconf_red1\n"
    "pic icons/iconf_red1 113 214 12 12 0 0 1 1\n"
    "color 1 1 1 1\n"
    "pic g.heartShader 138 215 10 10 0 0 1 1\n"
    "color null\n"
    "pic g.patrolShader 151 214 12 12 0 0 1 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 164 220 4.80000019 7.20000029 0.3203125 0 0.32421875 1\n"
    "pic font.normal 168.800003 220 4.80000019 7.20000029 0.37890625 0 0.3828125 1\n"
    "pic font.normal 173.600006 220 4.80000019 7.20000029 0.4296875 0 0.43359375 1\n"
    "pic font.normal 177.200012 220 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 182.600006 220 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 186.800003 220 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 211 220 4.80000019 7.20000029 0.328125 0 0.33203125 1\n"
    "pic font.normal 217 220 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 221.199997 220 4.80000019 7.20000029 0.46484375 0 0.46875 1\n"
    "pic font.normal 227.199997 220 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 231.399994 220 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "color null",
    NULL}},
  {"capfrag ffa",
    (const char *const[]){
    "paint 66 style=3",
    NULL},
    (const char *const[]){
    "> 66\n"
    "color 0.5 0.75 1 0.800000012\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 101 195 4.80000019 7.20000029 0.1953125 0 0.19921875 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 194 4.80000019 7.20000029 0.1953125 0 0.19921875 1\n"
    "color 0 0 0 0.800000012\n"
    "pic font.normal 104.599998 195 4.80000019 7.20000029 0.1875 0 0.19140625 1\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 103.599998 194 4.80000019 7.20000029 0.1875 0 0.19140625 1\n"
    "color null",
    NULL}},
  {"capfrag ctf",
    (const char *const[]){
    "game_type=4\n"
    "paint 66",
    NULL},
    (const char *const[]){
    "> 66\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104.800003 194 4.80000019 7.20000029 0.20703125 0 0.2109375 1\n"
    "color null",
    NULL}},
  {"capfrag wide",
    (const char *const[]){
    "fraglimit=123\n"
    "paint 66 y=210.5",
    NULL},
    (const char *const[]){
    "> 66\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 204.5 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "pic font.normal 106 204.5 4.80000019 7.20000029 0.1953125 0 0.19921875 1\n"
    "pic font.normal 109.599998 204.5 4.80000019 7.20000029 0.19921875 0 0.203125 1\n"
    "color null",
    NULL}},
  {"first",
    (const char *const[]){
    "paint 67 style=1",
    NULL},
    (const char *const[]){
    "> 67\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104.800003 194 4.80000019 7.20000029 0.21875 0 0.22265625 1\n"
    "color null",
    NULL}},
  {"second",
    (const char *const[]){
    "paint 68",
    NULL},
    (const char *const[]){
    "> 68\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 104.800003 194 4.80000019 7.20000029 0.19921875 0 0.203125 1\n"
    "color null",
    NULL}},
  {"places wide",
    (const char *const[]){
    "scores1=100\n"
    "scores2=-3\n"
    "paint 67\n"
    "paint 68",
    NULL},
    (const char *const[]){
    "> 67\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 194 4.80000019 7.20000029 0.19140625 0 0.1953125 1\n"
    "pic font.normal 106 194 4.80000019 7.20000029 0.1875 0 0.19140625 1\n"
    "pic font.normal 111.400002 194 4.80000019 7.20000029 0.1875 0 0.19140625 1\n"
    "color null\n"
    "> 68\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 194 4.80000019 7.20000029 0.17578125 0 0.1796875 1\n"
    "pic font.normal 103.599998 194 4.80000019 7.20000029 0.19921875 0 0.203125 1\n"
    "color null",
    NULL}},
  {"places missing",
    (const char *const[]){
    "scores1=-9999\n"
    "scores2=-9999\n"
    "paint 67\n"
    "paint 68",
    NULL},
    (const char *const[]){
    "> 67\n"
    "> 68",
    NULL}},
  {"unknown id",
    (const char *const[]){
    "paint 13\n"
    "paint 37\n"
    "paint 70\n"
    "paint 0",
    NULL},
    (const char *const[]){
    "> 13\n"
    "> 37\n"
    "> 70\n"
    "> 0",
    NULL}},
  {"values",
    (const char *const[]){
    "value 1\n"
    "value 2\n"
    "value 3\n"
    "value 4\n"
    "value 5\n"
    "value 6\n"
    "value 7\n"
    "value 8\n"
    "value 9\n"
    "value 10\n"
    "value 11\n"
    "value 12\n"
    "value 19\n"
    "value 20\n"
    "value 21\n"
    "value 22\n"
    "value 23\n"
    "value 24\n"
    "value 25\n"
    "value 26\n"
    "value 27\n"
    "value 28\n"
    "value 29\n"
    "value 30\n"
    "value 31\n"
    "value 32\n"
    "value 33\n"
    "value 34\n"
    "value 35\n"
    "value 36\n"
    "value 38\n"
    "value 39\n"
    "value 40\n"
    "value 41\n"
    "value 42\n"
    "value 46\n"
    "value 47\n"
    "value 48\n"
    "value 49\n"
    "value 50\n"
    "value 51\n"
    "value 52\n"
    "value 53\n"
    "value 54\n"
    "value 55\n"
    "value 56\n"
    "value 57\n"
    "value 58\n"
    "value 59\n"
    "value 60\n"
    "value 61\n"
    "value 62\n"
    "value 63\n"
    "value 64\n"
    "value 65\n"
    "value 66\n"
    "value 67\n"
    "value 68\n"
    "value 69\n"
    "value 9999",
    NULL},
    (const char *const[]){
    "value -1\n"
    "value 44\n"
    "value -1\n"
    "value 88\n"
    "value -1\n"
    "value 7\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value 17\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value 3\n"
    "value 8\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value 10\n"
    "value 30\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1\n"
    "value -1",
    NULL}},
  {"widths",
    (const char *const[]){
    "width 1 0.3\n"
    "width 2 0.3\n"
    "width 3 0.3\n"
    "width 4 0.3\n"
    "width 5 0.3\n"
    "width 6 0.3\n"
    "width 7 0.3\n"
    "width 8 0.3\n"
    "width 9 0.3\n"
    "width 10 0.3\n"
    "width 11 0.3\n"
    "width 12 0.3\n"
    "width 19 0.3\n"
    "width 20 0.3\n"
    "width 21 0.3\n"
    "width 22 0.3\n"
    "width 23 0.3\n"
    "width 24 0.3\n"
    "width 25 0.3\n"
    "width 26 0.3\n"
    "width 27 0.3\n"
    "width 28 0.3\n"
    "width 29 0.3\n"
    "width 30 0.3\n"
    "width 31 0.3\n"
    "width 32 0.3\n"
    "width 33 0.3\n"
    "width 34 0.3\n"
    "width 35 0.3\n"
    "width 36 0.3\n"
    "width 38 0.3\n"
    "width 39 0.3\n"
    "width 40 0.3\n"
    "width 41 0.3\n"
    "width 42 0.3\n"
    "width 46 0.3\n"
    "width 47 0.3\n"
    "width 48 0.3\n"
    "width 49 0.3\n"
    "width 50 0.3\n"
    "width 51 0.3\n"
    "width 52 0.3\n"
    "width 53 0.3\n"
    "width 54 0.3\n"
    "width 55 0.3\n"
    "width 56 0.3\n"
    "width 57 0.3\n"
    "width 58 0.3\n"
    "width 59 0.3\n"
    "width 60 0.3\n"
    "width 61 0.3\n"
    "width 62 0.3\n"
    "width 63 0.3\n"
    "width 64 0.3\n"
    "width 65 0.3\n"
    "width 66 0.3\n"
    "width 67 0.3\n"
    "width 68 0.3\n"
    "width 69 0.3",
    NULL},
    (const char *const[]){
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 34\n"
    "width 25\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 55\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 79\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0\n"
    "width 0",
    NULL}},
  {"visible gt0",
    (const char *const[]){
    "game_type=0\n"
    "redflag=1\n"
    "ps.health=20\n"
    "flag_status=2\n"
    "visible 1\n"
    "visible 2\n"
    "visible 4\n"
    "visible 8\n"
    "visible 16\n"
    "visible 32\n"
    "visible 64\n"
    "visible 128\n"
    "visible 256\n"
    "visible 512\n"
    "visible 1024\n"
    "visible 2048\n"
    "visible 4096\n"
    "visible 8192\n"
    "visible 16384\n"
    "visible 32768\n"
    "visible 65536\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 524288\n"
    "visible 268435456\n"
    "other\n"
    "visible 0\n"
    "visible 12\n"
    "visible 96",
    NULL},
    (const char *const[]){
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "other 0 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0",
    NULL}},
  {"visible gt1",
    (const char *const[]){
    "game_type=1\n"
    "redflag=1\n"
    "ps.health=20\n"
    "flag_status=2\n"
    "visible 1\n"
    "visible 2\n"
    "visible 4\n"
    "visible 8\n"
    "visible 16\n"
    "visible 32\n"
    "visible 64\n"
    "visible 128\n"
    "visible 256\n"
    "visible 512\n"
    "visible 1024\n"
    "visible 2048\n"
    "visible 4096\n"
    "visible 8192\n"
    "visible 16384\n"
    "visible 32768\n"
    "visible 65536\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 524288\n"
    "visible 268435456\n"
    "other\n"
    "visible 0\n"
    "visible 12\n"
    "visible 96",
    NULL},
    (const char *const[]){
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "other 0 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0",
    NULL}},
  {"visible gt2",
    (const char *const[]){
    "game_type=2\n"
    "redflag=1\n"
    "ps.health=20\n"
    "flag_status=2\n"
    "visible 1\n"
    "visible 2\n"
    "visible 4\n"
    "visible 8\n"
    "visible 16\n"
    "visible 32\n"
    "visible 64\n"
    "visible 128\n"
    "visible 256\n"
    "visible 512\n"
    "visible 1024\n"
    "visible 2048\n"
    "visible 4096\n"
    "visible 8192\n"
    "visible 16384\n"
    "visible 32768\n"
    "visible 65536\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 524288\n"
    "visible 268435456\n"
    "other\n"
    "visible 0\n"
    "visible 12\n"
    "visible 96",
    NULL},
    (const char *const[]){
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "other 0 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0",
    NULL}},
  {"visible gt3",
    (const char *const[]){
    "game_type=3\n"
    "redflag=1\n"
    "ps.health=20\n"
    "flag_status=2\n"
    "visible 1\n"
    "visible 2\n"
    "visible 4\n"
    "visible 8\n"
    "visible 16\n"
    "visible 32\n"
    "visible 64\n"
    "visible 128\n"
    "visible 256\n"
    "visible 512\n"
    "visible 1024\n"
    "visible 2048\n"
    "visible 4096\n"
    "visible 8192\n"
    "visible 16384\n"
    "visible 32768\n"
    "visible 65536\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 524288\n"
    "visible 268435456\n"
    "other\n"
    "visible 0\n"
    "visible 12\n"
    "visible 96",
    NULL},
    (const char *const[]){
    "visible 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "other 0 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0",
    NULL}},
  {"visible gt4",
    (const char *const[]){
    "game_type=4\n"
    "redflag=1\n"
    "ps.health=20\n"
    "flag_status=2\n"
    "visible 1\n"
    "visible 2\n"
    "visible 4\n"
    "visible 8\n"
    "visible 16\n"
    "visible 32\n"
    "visible 64\n"
    "visible 128\n"
    "visible 256\n"
    "visible 512\n"
    "visible 1024\n"
    "visible 2048\n"
    "visible 4096\n"
    "visible 8192\n"
    "visible 16384\n"
    "visible 32768\n"
    "visible 65536\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 524288\n"
    "visible 268435456\n"
    "other\n"
    "visible 0\n"
    "visible 12\n"
    "visible 96",
    NULL},
    (const char *const[]){
    "visible 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "other 1 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 1",
    NULL}},
  {"visible gt5",
    (const char *const[]){
    "game_type=5\n"
    "redflag=1\n"
    "ps.health=20\n"
    "flag_status=2\n"
    "visible 1\n"
    "visible 2\n"
    "visible 4\n"
    "visible 8\n"
    "visible 16\n"
    "visible 32\n"
    "visible 64\n"
    "visible 128\n"
    "visible 256\n"
    "visible 512\n"
    "visible 1024\n"
    "visible 2048\n"
    "visible 4096\n"
    "visible 8192\n"
    "visible 16384\n"
    "visible 32768\n"
    "visible 65536\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 524288\n"
    "visible 268435456\n"
    "other\n"
    "visible 0\n"
    "visible 12\n"
    "visible 96",
    NULL},
    (const char *const[]){
    "visible 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "other 0 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0",
    NULL}},
  {"visible gt6",
    (const char *const[]){
    "game_type=6\n"
    "redflag=1\n"
    "ps.health=20\n"
    "flag_status=2\n"
    "visible 1\n"
    "visible 2\n"
    "visible 4\n"
    "visible 8\n"
    "visible 16\n"
    "visible 32\n"
    "visible 64\n"
    "visible 128\n"
    "visible 256\n"
    "visible 512\n"
    "visible 1024\n"
    "visible 2048\n"
    "visible 4096\n"
    "visible 8192\n"
    "visible 16384\n"
    "visible 32768\n"
    "visible 65536\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 524288\n"
    "visible 268435456\n"
    "other\n"
    "visible 0\n"
    "visible 12\n"
    "visible 96",
    NULL},
    (const char *const[]){
    "visible 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "other 0 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 1",
    NULL}},
  {"visible gt7",
    (const char *const[]){
    "game_type=7\n"
    "redflag=1\n"
    "ps.health=20\n"
    "flag_status=2\n"
    "visible 1\n"
    "visible 2\n"
    "visible 4\n"
    "visible 8\n"
    "visible 16\n"
    "visible 32\n"
    "visible 64\n"
    "visible 128\n"
    "visible 256\n"
    "visible 512\n"
    "visible 1024\n"
    "visible 2048\n"
    "visible 4096\n"
    "visible 8192\n"
    "visible 16384\n"
    "visible 32768\n"
    "visible 65536\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 524288\n"
    "visible 268435456\n"
    "other\n"
    "visible 0\n"
    "visible 12\n"
    "visible 96",
    NULL},
    (const char *const[]){
    "visible 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 0\n"
    "other 0 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0",
    NULL}},
  {"has flag 4 1 0 0 1",
    (const char *const[]){
    "game_type=4\n"
    "redflag=1\n"
    "blueflag=0\n"
    "flag_status=0\n"
    "ps.team=1\n"
    "other\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 1\n"
    "visible 2",
    NULL},
    (const char *const[]){
    "other 1 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0",
    NULL}},
  {"has flag 4 0 1 0 1",
    (const char *const[]){
    "game_type=4\n"
    "redflag=0\n"
    "blueflag=1\n"
    "flag_status=0\n"
    "ps.team=1\n"
    "other\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 1\n"
    "visible 2",
    NULL},
    (const char *const[]){
    "other 0 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 1",
    NULL}},
  {"has flag 4 1 0 0 2",
    (const char *const[]){
    "game_type=4\n"
    "redflag=1\n"
    "blueflag=0\n"
    "flag_status=0\n"
    "ps.team=2\n"
    "other\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 1\n"
    "visible 2",
    NULL},
    (const char *const[]){
    "other 0 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 1\n"
    "visible 0",
    NULL}},
  {"has flag 4 0 1 0 2",
    (const char *const[]){
    "game_type=4\n"
    "redflag=0\n"
    "blueflag=1\n"
    "flag_status=0\n"
    "ps.team=2\n"
    "other\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 1\n"
    "visible 2",
    NULL},
    (const char *const[]){
    "other 1 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1",
    NULL}},
  {"has flag 5 0 0 2 1",
    (const char *const[]){
    "game_type=5\n"
    "redflag=0\n"
    "blueflag=0\n"
    "flag_status=2\n"
    "ps.team=1\n"
    "other\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 1\n"
    "visible 2",
    NULL},
    (const char *const[]){
    "other 0 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 1\n"
    "visible 0",
    NULL}},
  {"has flag 5 0 0 3 1",
    (const char *const[]){
    "game_type=5\n"
    "redflag=0\n"
    "blueflag=0\n"
    "flag_status=3\n"
    "ps.team=1\n"
    "other\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 1\n"
    "visible 2",
    NULL},
    (const char *const[]){
    "other 1 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1",
    NULL}},
  {"has flag 5 0 0 2 2",
    (const char *const[]){
    "game_type=5\n"
    "redflag=0\n"
    "blueflag=0\n"
    "flag_status=2\n"
    "ps.team=2\n"
    "other\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 1\n"
    "visible 2",
    NULL},
    (const char *const[]){
    "other 1 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0",
    NULL}},
  {"has flag 5 0 0 3 2",
    (const char *const[]){
    "game_type=5\n"
    "redflag=0\n"
    "blueflag=0\n"
    "flag_status=3\n"
    "ps.team=2\n"
    "other\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 1\n"
    "visible 2",
    NULL},
    (const char *const[]){
    "other 0 1\n"
    "visible 0\n"
    "visible 1\n"
    "visible 0\n"
    "visible 1",
    NULL}},
  {"has flag 0 1 1 2 1",
    (const char *const[]){
    "game_type=0\n"
    "redflag=1\n"
    "blueflag=1\n"
    "flag_status=2\n"
    "ps.team=1\n"
    "other\n"
    "visible 131072\n"
    "visible 262144\n"
    "visible 1\n"
    "visible 2",
    NULL},
    (const char *const[]){
    "other 0 0\n"
    "visible 0\n"
    "visible 0\n"
    "visible 1\n"
    "visible 1",
    NULL}},
  {"visible teaminfo",
    (const char *const[]){
    "cvar.cg_currentSelectedPlayer=3\n"
    "visible 32768\n"
    "visible 65536",
    NULL},
    (const char *const[]){
    "visible 1\n"
    "visible 0",
    NULL}},
  {"visible flag carried",
    (const char *const[]){
    "ps.powerup.9=1\n"
    "visible 2048",
    NULL},
    (const char *const[]){
    "visible 1",
    NULL}},
  {"wrong draw",
    (const char *const[]){
    "paint 4 draw=other",
    NULL},
    (const char *const[]){
    "THROW Mission owner drawing must share the ordered HUD recorder",
    NULL}},
  {"no snapshot paint",
    (const char *const[]){
    "snap=0\n"
    "paint 4",
    NULL},
    (const char *const[]){
    "THROW Mission owner drawing requires a current snapshot",
    NULL}},
  {"no snapshot head",
    (const char *const[]){
    "snap=0\n"
    "client.2.head=1\n"
    "paint 3",
    NULL},
    (const char *const[]){
    "THROW Mission owner drawing requires a current snapshot",
    NULL}},
  {"no snapshot location",
    (const char *const[]){
    "snap=0\n"
    "paint 33",
    NULL},
    (const char *const[]){
    "THROW Mission owner drawing requires a current snapshot",
    NULL}},
  {"no snapshot status",
    (const char *const[]){
    "snap=0\n"
    "paint 42",
    NULL},
    (const char *const[]){
    "THROW Mission owner drawing requires a current snapshot",
    NULL}},
  {"no snapshot skulls",
    (const char *const[]){
    "snap=0\n"
    "game_type=7\n"
    "paint 31",
    NULL},
    (const char *const[]){
    "THROW Mission owner drawing requires a current snapshot",
    NULL}},
  {"no snapshot item",
    (const char *const[]){
    "snap=0\n"
    "paint 19",
    NULL},
    (const char *const[]){
    "THROW Mission owner drawing requires a current snapshot",
    NULL}},
  {"no snapshot team",
    (const char *const[]){
    "snap=0\n"
    "paint 34",
    NULL},
    (const char *const[]){
    "THROW Mission owner drawing requires a current snapshot",
    NULL}},
  {"no snapshot value",
    (const char *const[]){
    "snap=0\n"
    "value 28",
    NULL},
    (const char *const[]){
    "THROW Mission owner drawing requires a current snapshot",
    NULL}},
  {"no snapshot flags ok",
    (const char *const[]){
    "snap=0\n"
    "visible 4\n"
    "paint 29\n"
    "paint 39",
    NULL},
    (const char *const[]){
    "visible 0\n"
    "> 29\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.32421875 0 0.328125 1\n"
    "pic font.normal 105.400002 226 4.80000019 7.20000029 0.453125 0 0.45703125 1\n"
    "pic font.normal 109.599998 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 115.599998 226 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 119.799995 226 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 125.199997 226 4.80000019 7.20000029 0.40234375 0 0.40625 1\n"
    "pic font.normal 130.599991 226 4.80000019 7.20000029 0.44921875 0 0.453125 1\n"
    "color null\n"
    "> 39\n"
    "color 0.5 0.75 1 0.800000012\n"
    "pic font.normal 100 226 4.80000019 7.20000029 0.2734375 0 0.27734375 1\n"
    "pic font.normal 103.599998 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 109.599998 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 113.799995 226 4.80000019 7.20000029 0.39453125 0 0.3984375 1\n"
    "pic font.normal 122.799995 226 4.80000019 7.20000029 0.2734375 0 0.27734375 1\n"
    "pic font.normal 126.399994 226 4.80000019 7.20000029 0.43359375 0 0.4375 1\n"
    "pic font.normal 130.599991 226 4.80000019 7.20000029 0.4453125 0 0.44921875 1\n"
    "pic font.normal 141.399994 226 4.80000019 7.20000029 0.25390625 0 0.2578125 1\n"
    "pic font.normal 145 226 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "pic font.normal 150.399994 226 4.80000019 7.20000029 0.421875 0 0.42578125 1\n"
    "color null",
    NULL}},
};

/* ---- oracle world ---- */
enum { WORLD_SENTINEL = 0x0D3A11ED, MAX_INTERNED = 512, WORLD_CVARS = 8, WORLD_STRINGS = 16 };
typedef struct od_world {
  uint32_t host_sentinel, hud_sentinel, sink_sentinel, resources_sentinel, tools_sentinel, settings_sentinel, items_sentinel, lazy_sentinel;
  qa_q3_pres_client_game_state state;
  qa_q3_pres_client_game_static_state *cgs;
  qa_q3_pres_retail_snapshot snap;
  qa_q3_pres_client_media_graphics *graphics;
  qa_q3_pres_client_draw_tools tools;
  qa_q3_pres_client_draw_icons icons;
  qa_q3_pres_mission_owner_draw owner;
  qa_draw2d other_draw;
  char *log;
  size_t log_length, log_capacity;
  struct { char *name, *value; } cvars[WORLD_CVARS];
  struct { double index; char *value; } strings[WORLD_STRINGS];
  size_t string_count;
  qa_q3_pres_scene_shader interned[MAX_INTERNED];
  size_t interned_count;
  qa_q3_pres_packet_item_visual *items;
  size_t item_count;
  double selected;
  bool hud_on;
  unsigned crandom_calls, random_calls;
  char lazy_name[64];
  qa_material_picture lazy_picture;
} od_world;
static od_world *world_from(void *ctx, size_t offset) {
  od_world *w = (od_world *)((char *)ctx - offset);
  if (*(uint32_t *)ctx != WORLD_SENTINEL) abort();
  return w;
}
#define W(field) world_from(ctx, offsetof(od_world, field))
static void log_line(od_world *w, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void log_line(od_world *w, const char *format, ...) {
  char line[768];
  va_list args;
  va_start(args, format);
  int length = vsnprintf(line, sizeof line, format, args);
  va_end(args);
  if (length < 0 || (size_t)length >= sizeof line) abort();
  size_t needed = w->log_length + (size_t)length + 2;
  if (needed > w->log_capacity) {
    w->log_capacity = needed * 2;
    w->log = realloc(w->log, w->log_capacity);
    if (!w->log) abort();
  }
  if (w->log_length) w->log[w->log_length++] = '\n';
  memcpy(w->log + w->log_length, line, (size_t)length + 1);
  w->log_length += (size_t)length;
}
/* JS String(parseFloat(fround(x).toPrecision(9))). */
static const char *num(char *buffer, double value) {
  char rounded[32];
  float narrow = (float)value;
  snprintf(rounded, sizeof rounded, "%.9g", narrow == 0 ? 0.0 : (double)narrow);
  qa_net_js_number(strtod(rounded, NULL), buffer, 32);
  return buffer;
}
static const qa_q3_pres_scene_shader *shader_named(od_world *w, const char *name) {
  for (size_t i = 0; i < w->interned_count; i++) if (strcmp(w->interned[i].name, name) == 0) return &w->interned[i];
  if (w->interned_count == MAX_INTERNED) abort();
  w->interned[w->interned_count].name = strdup(name);
  return &w->interned[w->interned_count++];
}
static qa_q3_pres_scene_model inline_model(const char *path) {
  qa_q3_pres_scene_model model;
  memset(&model, 0, sizeof model);
  model.kind = QA_Q3_PRES_SCENE_MODEL_INLINE;
  model.path = path;
  model.index = 1;
  model.bounds.min = (qa_vec3){-8, -8, -8};
  model.bounds.max = (qa_vec3){8, 8, 8};
  return model;
}
/* sink */
static void w_set_color(void *ctx, const qa_vec4 *color) {
  od_world *w = W(sink_sentinel);
  char a[32], b[32], c[32], d[32];
  if (!color) log_line(w, "color null");
  else log_line(w, "color %s %s %s %s", num(a, color->x), num(b, color->y), num(c, color->z), num(d, color->w));
}
static void w_stretch(void *ctx, qa_rect r, qa_text_texture_rect uv, const qa_picture_asset *p) {
  od_world *w = W(sink_sentinel);
  char a[32], b[32], c[32], d[32], e[32], g[32], h[32], i[32];
  log_line(w, "pic %s %s %s %s %s %s %s %s %s", p->material.name, num(a, r.x), num(b, r.y), num(c, r.width), num(d, r.height),
           num(e, uv.s), num(g, uv.t), num(h, uv.s2), num(i, uv.t2));
}
/* resources */
static qa_material_picture named_picture(const qa_q3_pres_scene_shader *shader) {
  qa_material_picture picture;
  memset(&picture, 0, sizeof picture);
  picture.name = shader ? shader->name : "<null>";
  return picture;
}
static qa_material_picture w_res_picture(void *ctx, const qa_q3_pres_scene_shader *shader) { (void)W(resources_sentinel); return named_picture(shader); }
static qa_material_picture w_tool_picture(void *ctx, const qa_q3_pres_scene_shader *shader) { (void)W(tools_sentinel); return named_picture(shader); }
static void w_clear(void *ctx) { log_line(W(resources_sentinel), "scene clear"); }
static void w_add_ref(void *ctx, const qa_q3_pres_admitted_ref_entity *entity) {
  od_world *w = W(resources_sentinel);
  const qa_q3_pres_ref_model_entity *e = &entity->entity.as.model;
  char s[12][32];
  const qa_axis *x = &e->axis;
  log_line(w, "scene entity %s origin %s %s %s axis %s %s %s %s %s %s %s %s %s", e->model.path,
           num(s[0], e->origin.x), num(s[1], e->origin.y), num(s[2], e->origin.z),
           num(s[3], x->vectors[0].x), num(s[4], x->vectors[0].y), num(s[5], x->vectors[0].z),
           num(s[6], x->vectors[1].x), num(s[7], x->vectors[1].y), num(s[8], x->vectors[1].z),
           num(s[9], x->vectors[2].x), num(s[10], x->vectors[2].y), num(s[11], x->vectors[2].z));
}
static void w_render(void *ctx, const qa_q3_pres_refdef *r) {
  log_line(W(resources_sentinel), "scene render %.0f %.0f %.0f %.0f %.0f %.0f", r->x, r->y, r->width, r->height, r->time, (double)r->render_flags);
}
static qa_q3_pres_client_draw_icon_settings w_settings(void *ctx);
static const qa_q3_pres_packet_item_visual *w_items(void *ctx, size_t *count) {
  od_world *w = W(items_sentinel);
  *count = w->item_count;
  return w->items;
}
/* host */
static const qa_q3_font_set *w_fonts(void *ctx) {
  (void)W(host_sentinel);
  static qa_q3_registered_font small, normal, big;
  static qa_picture_asset pictures[3];
  static qa_q3_font_set set;
  if (!set.small) {
    qa_q3_registered_font *fonts[3] = {&small, &normal, &big};
    const char *names[3] = {"font.small", "font.normal", "font.big"};
    const double scales[3] = {4, 2, 1};
    for (int i = 0; i < 3; i++) {
      pictures[i].kind = QA_PICTURE_MATERIAL;
      pictures[i].material.name = names[i];
      fonts[i]->glyph_scale = scales[i];
      fonts[i]->glyph_count = QA_Q3_FONT_GLYPHS;
      for (int c = 0; c < QA_Q3_FONT_GLYPHS; c++) {
        qa_q3_glyph_metrics *m = &fonts[i]->glyphs[c].metrics;
        m->height = 12; m->top = 10; m->bottom = 2; m->x_skip = 6 + c % 5; m->image_width = 8; m->image_height = 12;
        m->s = c / 256.0; m->s2 = (c + 1) / 256.0; m->t2 = 1;
        fonts[i]->glyphs[c].picture = c == 32 ? NULL : &pictures[i];
      }
    }
    set.small = &small; set.normal = &normal; set.big = &big;
    set.profile = QA_Q3_FONT_CGAME; set.small_threshold = 0.25; set.big_threshold = 0.4;
  }
  return &set;
}
static qa_cvar_snapshot w_cvar(void *ctx, const char *name) {
  od_world *w = W(host_sentinel);
  for (int i = 0; i < WORLD_CVARS; i++) {
    if (w->cvars[i].name && strcmp(w->cvars[i].name, name) == 0) {
      qa_cvar_snapshot snapshot;
      memset(&snapshot, 0, sizeof snapshot);
      snapshot.name = strdup(name);
      snapshot.value = strdup(w->cvars[i].value);
      snapshot.reset_value = strdup("");
      snapshot.integer_value = trunc(strtod(w->cvars[i].value, NULL));
      return snapshot;
    }
  }
  abort();
}
static qa_q3_pres_client_draw_icon_settings w_settings(void *ctx) {
  od_world *w = W(settings_sentinel);
  void *host = &w->host_sentinel;
  qa_cvar_snapshot icons = w_cvar(host, "cg_drawIcons"), models = w_cvar(host, "cg_draw3dIcons");
  qa_q3_pres_client_draw_icon_settings settings = {strcmp(icons.value, "0") != 0, strcmp(models.value, "0") != 0};
  qa_cvar_read_free(&icons);
  qa_cvar_read_free(&models);
  return settings;
}
static const char *w_config_string(void *ctx, double index) {
  od_world *w = W(host_sentinel);
  for (size_t i = 0; i < w->string_count; i++) if (w->strings[i].index == index) return w->strings[i].value;
  return "";
}
static double w_selected(void *ctx) { return W(host_sentinel)->selected; }
static void w_chat(void *ctx, const char **system, const char **team1, const char **team2) {
  (void)W(host_sentinel);
  *system = "sys text"; *team1 = "team one"; *team2 = "team two";
}
static void check_weapon(double n) {
  if (n != trunc(n) || n < 0 || n >= 16) qa_range_error("Invalid weapon media index %g", n);
}
static const qa_q3_pres_scene_shader *w_weapon_icon(void *ctx, double n) {
  od_world *w = W(host_sentinel);
  check_weapon(n);
  char name[32];
  snprintf(name, sizeof name, "wicon%.0f", n);
  return n == 5 ? NULL : shader_named(w, name);
}
static const qa_q3_pres_scene_shader *w_ammo_icon(void *ctx, double n) {
  od_world *w = W(host_sentinel);
  check_weapon(n);
  char name[32];
  snprintf(name, sizeof name, "ammo%.0f", n);
  return n == 3 ? NULL : shader_named(w, name);
}
static qa_q3_pres_scene_model w_ammo_model(void *ctx, double n) {
  (void)W(host_sentinel);
  check_weapon(n);
  static char names[16][24];
  if (n == 0 || n == 4) return qa_q3_pres_default_model;
  snprintf(names[(int)n], sizeof names[0], "ammo%.0f.model", n);
  return inline_model(names[(int)n]);
}
static bool w_register_item(void *ctx, double n) {
  od_world *w = W(host_sentinel);
  char text[32];
  snprintf(text, sizeof text, "%.15g", n);
  log_line(w, "item %s", text);
  if (n < 0 || n >= (double)w->item_count) qa_range_error("bad item");
  return true;
}
static const qa_q3_pres_scene_shader *w_item_icon(void *ctx, double n) { return W(host_sentinel)->items[(size_t)n].icon; }
static const qa_q3_pres_scene_shader *w_register_shader(void *ctx, const char *path) {
  od_world *w = W(host_sentinel);
  log_line(w, "shader %s", path);
  return shader_named(w, path);
}
static double w_crandom(void *ctx) { static const double v[4] = {0.5, -0.25, 0.75, -0.125}; return v[W(host_sentinel)->crandom_calls++ % 4]; }
static double w_random(void *ctx) { static const double v[3] = {0.1, 0.9, 0.5}; return v[W(host_sentinel)->random_calls++ % 3]; }
static qa_q3_pres_weapon_hud_reading w_weapon_hud(void *ctx) {
  (void)W(hud_sentinel);
  qa_q3_pres_weapon_hud_reading reading;
  memset(&reading, 0, sizeof reading);
  reading.has_status = true;
  reading.status.ammo_is_finite = true;
  reading.status.ammo_count = 17;
  return reading;
}
static qa_picture_asset w_lazy(void *ctx) {
  od_world *w = W(lazy_sentinel);
  qa_picture_asset picture;
  memset(&picture, 0, sizeof picture);
  picture.kind = QA_PICTURE_MATERIAL;
  picture.material.name = w->lazy_name;
  return picture;
}
static void set_cvar_value(od_world *w, const char *name, const char *value) {
  for (int i = 0; i < WORLD_CVARS; i++) {
    if (!w->cvars[i].name || strcmp(w->cvars[i].name, name) == 0) {
      if (!w->cvars[i].name) w->cvars[i].name = strdup(name);
      free(w->cvars[i].value);
      w->cvars[i].value = strdup(value);
      return;
    }
  }
  abort();
}
static qa_q3_pres_mission_owner_draw_host world_host(od_world *w) {
  qa_q3_pres_mission_owner_draw_host host;
  memset(&host, 0, sizeof host);
  host.icons = &w->icons;
  host.ctx = &w->host_sentinel;
  host.fonts = w_fonts;
  host.read_vm_cvar = w_cvar;
  host.config_string = w_config_string;
  host.selected_player = w_selected;
  host.chat = w_chat;
  host.weapon_icon = w_weapon_icon;
  host.register_item_visuals = w_register_item;
  host.item_icon = w_item_icon;
  host.graphics = w->graphics;
  host.register_shader = w_register_shader;
  host.crandom = w_crandom;
  host.random_value = w_random;
  host.ammo_icon = w_ammo_icon;
  host.ammo_model = w_ammo_model;
  return host;
}
static od_world *world_create(void) {
  od_world *w = calloc(1, sizeof *w);
  w->host_sentinel = w->hud_sentinel = w->sink_sentinel = w->resources_sentinel = w->tools_sentinel = w->settings_sentinel = w->items_sentinel = w->lazy_sentinel = WORLD_SENTINEL;
  if (!qa_q3_pres_client_game_state_init(&w->state, QA_Q3_BASE_PRODUCT_MISSIONPACK, 2, 0)) abort();
  w->cgs = calloc(1, sizeof *w->cgs);
  qa_q3_pres_client_game_static_state_init(w->cgs, QA_Q3_BASE_PRODUCT_MISSIONPACK);
  if (!qa_q3_base_player_state_record_init(&w->snap.player_state, QA_Q3_BASE_PRODUCT_MISSIONPACK, 0, 0, 0, NULL)) abort();
  w->snap.player_state.client_num = 2;
  w->state.snap = &w->snap;
  w->graphics = calloc(1, sizeof *w->graphics);
  qa_q3_pres_client_media_graphics *g = w->graphics;
  g->defend_shader = shader_named(w, "g.defendShader"); g->patrol_shader = shader_named(w, "g.patrolShader");
  g->follow_shader = shader_named(w, "g.followShader"); g->retrieve_shader = shader_named(w, "g.retrieveShader");
  g->escort_shader = shader_named(w, "g.escortShader"); g->camp_shader = shader_named(w, "g.campShader");
  g->assault_shader = shader_named(w, "g.assaultShader"); g->defer_shader = shader_named(w, "g.deferShader");
  g->armor_icon = shader_named(w, "g.armorIcon"); g->blue_cube_icon = shader_named(w, "g.blueCubeIcon");
  g->red_cube_icon = shader_named(w, "g.redCubeIcon"); g->heart_shader = shader_named(w, "g.heartShader");
  g->team_status_bar = shader_named(w, "g.teamStatusBar");
  g->flag_shaders[0] = shader_named(w, "flag0"); g->flag_shaders[1] = shader_named(w, "flag1"); g->flag_shaders[2] = shader_named(w, "flag2");
  g->armor_model = inline_model("armor.model"); g->red_cube_model = inline_model("redcube.model"); g->blue_cube_model = inline_model("bluecube.model");
  qa_text_draw_sink sink;
  memset(&sink, 0, sizeof sink);
  sink.ctx = &w->sink_sentinel;
  sink.target.width = 640;
  sink.target.height = 480;
  sink.set_color = w_set_color;
  sink.stretch_pixels = w_stretch;
  qa_draw2d draw;
  memset(&draw, 0, sizeof draw);
  draw.commands = sink;
  draw.space = QA_COORD_STRETCH_640;
  w->other_draw = draw;
  qa_q3_pres_draw_tools_media tool_media;
  memset(&tool_media, 0, sizeof tool_media);
  tool_media.ctx = &w->tools_sentinel;
  tool_media.picture = w_tool_picture;
  qa_q3_pres_client_draw_tools_init(&w->tools, draw, tool_media);
  size_t count = 0;
  (void)qa_q3_base_item_list(QA_Q3_BASE_PRODUCT_MISSIONPACK, &count);
  w->item_count = count;
  w->items = calloc(count, sizeof *w->items);
  for (size_t i = 0; i < count; i++) {
    char name[32];
    snprintf(name, sizeof name, "item%zu", i);
    w->items[i].icon = shader_named(w, name);
  }
  qa_q3_pres_draw_icons_media icon_media;
  memset(&icon_media, 0, sizeof icon_media);
  icon_media.product = QA_Q3_BASE_PRODUCT_MISSIONPACK;
  icon_media.static_state = w->cgs;
  icon_media.resources.ctx = &w->resources_sentinel;
  icon_media.resources.picture = w_res_picture;
  icon_media.resources.clear_scene = w_clear;
  icon_media.resources.add_ref_entity = w_add_ref;
  icon_media.resources.render_scene = w_render;
  icon_media.red_flag_model = inline_model("redflag.model");
  icon_media.blue_flag_model = inline_model("blueflag.model");
  icon_media.neutral_flag_model = inline_model("neutralflag.model");
  icon_media.defer_shader = g->defer_shader;
  icon_media.team_status_bar = g->team_status_bar;
  icon_media.items_ctx = &w->items_sentinel;
  icon_media.items = w_items;
  qa_q3_pres_client_draw_icons_init(&w->icons, &w->state, &w->tools, icon_media, w_settings, &w->settings_sentinel, w->tools.draw.commands.ctx);
  qa_q3_pres_mission_owner_draw_init(&w->owner, &w->state, w->cgs, world_host(w));
  set_cvar_value(w, "cg_drawStatus", "1"); set_cvar_value(w, "cg_draw3dIcons", "1"); set_cvar_value(w, "cg_drawIcons", "1");
  set_cvar_value(w, "cg_currentSelectedPlayer", "0"); set_cvar_value(w, "g_redteam", "Stroggs"); set_cvar_value(w, "g_blueteam", "Pagans");
  return w;
}
static void world_destroy(od_world *w) {
  qa_q3_base_player_state_record_destroy(&w->snap.player_state);
  qa_q3_pres_client_game_state_destroy(&w->state);
  qa_q3_pres_client_game_static_state_destroy(w->cgs);
  free(w->cgs);
  free(w->graphics);
  free(w->items);
  free(w->log);
  for (int i = 0; i < WORLD_CVARS; i++) { free(w->cvars[i].name); free(w->cvars[i].value); }
  for (size_t i = 0; i < w->string_count; i++) free(w->strings[i].value);
  for (size_t i = 0; i < w->interned_count; i++) free((char *)w->interned[i].name);
  free(w);
}
static void world_apply(od_world *w, const char *op) {
  const char *eq = strchr(op, '=');
  if (!eq) abort();
  char key[64];
  snprintf(key, sizeof key, "%.*s", (int)(eq - op), op);
  const char *text = eq + 1;
  double n = strtod(text, NULL);
  qa_q3_pres_client_game_state *s = &w->state;
  qa_q3_pres_client_game_static_state *c = w->cgs;
  qa_q3_base_source_player_state *ps = &w->snap.player_state;
  const qa_q3_base_stat_schema *stats = qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_MISSIONPACK);
  int index = 0;
  char field[32];
#define NUMERIC(name, target) if (strcmp(key, name) == 0) { target = n; return; }
  NUMERIC("time", s->time) NUMERIC("damage_time", s->damage_time) NUMERIC("damage_x", s->damage_x)
  NUMERIC("head_start_time", s->head_start_time) NUMERIC("head_end_time", s->head_end_time)
  NUMERIC("head_start_yaw", s->head_start_yaw) NUMERIC("head_end_yaw", s->head_end_yaw)
  NUMERIC("head_start_pitch", s->head_start_pitch) NUMERIC("head_end_pitch", s->head_end_pitch)
  NUMERIC("num_sorted", s->num_sorted_team_players) NUMERIC("selected_score", s->selected_score)
  NUMERIC("spectator_len", s->spectator_len) NUMERIC("spectator_width", s->spectator_width) NUMERIC("spectator_time", s->spectator_time)
  NUMERIC("spectator_offset", s->spectator_offset) NUMERIC("spectator_paint_x", s->spectator_paint_x) NUMERIC("spectator_paint_x2", s->spectator_paint_x2)
  NUMERIC("flag_status", c->flag_status) NUMERIC("redflag", c->redflag) NUMERIC("blueflag", c->blueflag) NUMERIC("scores1", c->scores1) NUMERIC("scores2", c->scores2)
  NUMERIC("capturelimit", c->capturelimit) NUMERIC("fraglimit", c->fraglimit) NUMERIC("maxclients", c->maxclients) NUMERIC("order_time", c->order_time)
  NUMERIC("current_order", c->current_order) NUMERIC("current_voice_client", c->current_voice_client) NUMERIC("selected", w->selected)
  NUMERIC("ps.generic1", ps->generic1) NUMERIC("pred.weapon", s->predicted_player_state.weapon)
  NUMERIC("ent.weapon", qa_q3_pres_client_game_state_entity_at(s, 2)->current_state.weapon)
#undef NUMERIC
  if (strcmp(key, "game_type") == 0) { c->game_type = (qa_q3_base_game_type)(int)n; return; }
  if (strcmp(key, "order_pending") == 0) { c->order_pending = n != 0; return; }
  if (strcmp(key, "weapon_hud") == 0) {
    w->owner.host.weapon_hud = n != 0 ? w_weapon_hud : NULL;
    w->owner.host.weapon_hud_ctx = &w->hud_sentinel;
    return;
  }
  if (strcmp(key, "snap") == 0) { s->snap = NULL; return; }
  if (strcmp(key, "killer") == 0) { if (!qa_q3_pres_set_string(&s->killer_name, text)) abort(); return; }
  if (strcmp(key, "spectators") == 0) { if (!qa_q3_pres_set_string(&s->spectator_list, text)) abort(); return; }
  if (strcmp(key, "ps.team") == 0) { qa_q3_base_player_state_slots_set(&ps->persistant, QA_Q3_BASE_PERS_TEAM, n); return; }
  if (strcmp(key, "ps.score") == 0) { qa_q3_base_player_state_slots_set(&ps->persistant, QA_Q3_BASE_PERS_SCORE, n); return; }
  if (strcmp(key, "ps.rank") == 0) { qa_q3_base_player_state_slots_set(&ps->persistant, QA_Q3_BASE_PERS_RANK, n); return; }
  if (strcmp(key, "ps.health") == 0) { qa_q3_base_player_state_slots_set(&ps->stats, stats->health, n); return; }
  if (strcmp(key, "ps.armor") == 0) { qa_q3_base_player_state_slots_set(&ps->stats, stats->armor, n); return; }
  if (strcmp(key, "ps.holdable") == 0) { qa_q3_base_player_state_slots_set(&ps->stats, QA_Q3_BASE_MP_STAT_HOLDABLE_ITEM, n); return; }
  if (strcmp(key, "ps.persistant_powerup") == 0) { qa_q3_base_player_state_slots_set(&ps->stats, QA_Q3_BASE_MP_STAT_PERSISTANT_POWERUP, n); return; }
  if (sscanf(key, "ps.ammo.%d", &index) == 1) { qa_q3_base_player_state_slots_set(&ps->ammo, index, n); return; }
  if (sscanf(key, "ps.powerup.%d", &index) == 1) { qa_q3_base_player_state_slots_set(&ps->powerups, index, n); return; }
  if (sscanf(key, "pred.powerup.%d", &index) == 1) { qa_q3_base_player_state_slots_set(&s->predicted_player_state.powerups, index, n); return; }
  if (sscanf(key, "sorted.%d", &index) == 1) { s->sorted_team_players[index] = n; return; }
  if (sscanf(key, "cs.%d", &index) == 1) {
    if (w->string_count == WORLD_STRINGS) abort();
    w->strings[w->string_count].index = index;
    w->strings[w->string_count++].value = strdup(text);
    return;
  }
  if (strncmp(key, "cvar.", 5) == 0) { set_cvar_value(w, key + 5, text); return; }
  if (sscanf(key, "client.%d.%31s", &index, field) == 2) {
    qa_q3_pres_client_info *ci = &c->client_info[index];
    if (strcmp(field, "name") == 0) { if (!qa_q3_pres_set_string(&ci->name, text)) abort(); }
    else if (strcmp(field, "team") == 0) ci->team = (qa_q3_base_team)(int)n;
    else if (strcmp(field, "health") == 0) ci->health = n;
    else if (strcmp(field, "armor") == 0) ci->armor = n;
    else if (strcmp(field, "location") == 0) ci->location = n;
    else if (strcmp(field, "team_task") == 0) ci->team_task = n;
    else if (strcmp(field, "powerups") == 0) ci->powerups = n;
    else if (strcmp(field, "cur_weapon") == 0) ci->cur_weapon = n;
    else if (strcmp(field, "info_valid") == 0) ci->info_valid = n != 0;
    else if (strcmp(field, "deferred") == 0) ci->deferred = n != 0;
    else if (strcmp(field, "model_icon") == 0) ci->model_icon = shader_named(w, text);
    else if (strcmp(field, "head") == 0) {
      static char names[64][24];
      snprintf(names[index], sizeof names[0], "head%d.model", index);
      ci->head_model = inline_model(names[index]);
    } else abort();
    return;
  }
  if (sscanf(key, "score.%d.%31s", &index, field) == 2) {
    qa_q3_pres_client_score *row = &s->scores[index];
    if (strcmp(field, "accuracy") == 0) row->accuracy = n;
    else if (strcmp(field, "assist") == 0) row->assist_count = n;
    else if (strcmp(field, "defend") == 0) row->defend_count = n;
    else if (strcmp(field, "excellent") == 0) row->excellent_count = n;
    else if (strcmp(field, "impressive") == 0) row->impressive_count = n;
    else if (strcmp(field, "perfect") == 0) row->perfect = n;
    else if (strcmp(field, "gauntlet") == 0) row->guantlet_count = n;
    else if (strcmp(field, "captures") == 0) row->captures = n;
    else abort();
    return;
  }
  fprintf(stderr, "unknown oracle key %s\n", key);
  abort();
}
static void world_paint(od_world *w, char *arguments) {
  qa_ui_owner_draw_paint_request request;
  memset(&request, 0, sizeof request);
  request.draw = &w->tools.draw;
  request.rect = (qa_rect){100, 200, 64, 32};
  request.text_y = 12;
  request.text_scale = 0.3;
  request.color = (qa_vec4){0.5, 0.75, 1, (float)0.8};
  char *saved = NULL;
  char *token = strtok_r(arguments, " ", &saved);
  request.owner_draw = strtod(token, NULL);
  log_line(w, "> %s", token);
  static char background[32];
  while ((token = strtok_r(NULL, " ", &saved))) {
    char *eq = strchr(token, '=');
    if (!eq) abort();
    *eq = '\0';
    const char *value = eq + 1;
    double n = strtod(value, NULL);
    if (strcmp(token, "flags") == 0) request.owner_draw_flags = n;
    else if (strcmp(token, "x") == 0) request.rect.x = (float)n;
    else if (strcmp(token, "y") == 0) request.rect.y = (float)n;
    else if (strcmp(token, "w") == 0) request.rect.width = (float)n;
    else if (strcmp(token, "h") == 0) request.rect.height = (float)n;
    else if (strcmp(token, "scale") == 0) request.text_scale = n;
    else if (strcmp(token, "style") == 0) request.text_style = n;
    else if (strcmp(token, "text_y") == 0) request.text_y = n;
    else if (strcmp(token, "align") == 0) request.alignment = n;
    else if (strcmp(token, "special") == 0) request.special = n;
    else if (strcmp(token, "draw") == 0) request.draw = &w->other_draw;
    else if (strcmp(token, "bg") == 0) {
      snprintf(background, sizeof background, "%s", value);
      request.background.kind = QA_UI_PICTURE_VALUE;
      request.background.picture.kind = QA_PICTURE_MATERIAL;
      request.background.picture.material.name = background;
    } else if (strcmp(token, "bglazy") == 0) {
      snprintf(w->lazy_name, sizeof w->lazy_name, "%s", value);
      request.background.kind = QA_UI_PICTURE_LAZY;
      request.background.produce = w_lazy;
      request.background.produce_ctx = &w->lazy_sentinel;
    } else abort();
  }
  qa_q3_pres_mission_owner_draw_paint(&w->owner, &request);
}
static void world_op(od_world *w, char *op) {
  char a[32];
  if (strncmp(op, "paint ", 6) == 0) world_paint(w, op + 6);
  else if (strncmp(op, "value ", 6) == 0) log_line(w, "value %s", num(a, qa_q3_pres_mission_owner_draw_value(&w->owner, strtod(op + 6, NULL))));
  else if (strncmp(op, "width ", 6) == 0) {
    char *end;
    double id = strtod(op + 6, &end);
    log_line(w, "width %s", num(a, qa_q3_pres_mission_owner_draw_width(&w->owner, id, strtod(end, NULL))));
  } else if (strncmp(op, "visible ", 8) == 0) log_line(w, "visible %d", qa_q3_pres_mission_owner_draw_visible(&w->owner, strtod(op + 8, NULL)) ? 1 : 0);
  else if (strcmp(op, "other") == 0) log_line(w, "other %d %d", qa_q3_pres_mission_owner_draw_other_team_has_flag(&w->owner) ? 1 : 0, qa_q3_pres_mission_owner_draw_your_team_has_flag(&w->owner) ? 1 : 0);
  else if (strncmp(op, "status ", 7) == 0) log_line(w, "status %s", qa_q3_pres_mission_owner_draw_status_handle(&w->owner, strtod(op + 7, NULL))->name);
  else world_apply(w, op);
}
/* Joins a generated chunk list (C caps literals at 4095 bytes). */
static char *joined(const char *const *chunks) {
  size_t length = 0;
  for (size_t i = 0; chunks[i]; i++) length += strlen(chunks[i]);
  char *text = malloc(length + 1), *end = text;
  if (!text) abort();
  for (size_t i = 0; chunks[i]; i++) end = stpcpy(end, chunks[i]);
  return text;
}
static void world_run(od_world *w, const char *ops) {
  char *copy = strdup(ops), *saved = NULL;
  for (char *line = strtok_r(copy, "\n", &saved); line; line = strtok_r(NULL, "\n", &saved)) world_op(w, line);
  free(copy);
}
static void report_difference(const char *name, const char *expected, const char *actual) {
  size_t line = 1, i = 0, start = 0;
  while (expected[i] && expected[i] == actual[i]) { if (expected[i] == '\n') { line++; start = i + 1; } i++; }
  fprintf(stderr, "ORACLE %s: line %zu\n  ts: %.160s\n   c: %.160s\n", name, line, expected + start, actual + start);
}
/* A TS throw must abort here, with the same message on stderr. */
static void run_throw_case(const oracle_case *test) {
  int fds[2];
  if (pipe(fds) != 0) abort();
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    (void)prctl(PR_SET_DUMPABLE, 0);
    dup2(fds[1], 2);
    close(fds[0]);
    od_world *w = world_create();
    char *ops = joined(test->ops);
    world_run(w, ORACLE_BASE);
    world_run(w, ops);
    _exit(0);
  }
  close(fds[1]);
  char output[1024];
  size_t length = 0;
  ssize_t got;
  while ((got = read(fds[0], output + length, sizeof output - 1 - length)) > 0) length += (size_t)got;
  output[length] = '\0';
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  if (!strstr(output, test->expected[0] + 6)) {
    failures++;
    fprintf(stderr, "ORACLE %s: expected message '%s', got '%s'\n", test->name, test->expected[0] + 6, output);
  }
}
static void test_oracle_cases(void) {
  for (size_t i = 0; i < sizeof ORACLE_CASES / sizeof ORACLE_CASES[0]; i++) {
    const oracle_case *test = &ORACLE_CASES[i];
    checks++;
    if (strncmp(test->expected[0], "THROW ", 6) == 0) {
      run_throw_case(test);
      continue;
    }
    od_world *w = world_create();
    char *ops = joined(test->ops), *expected = joined(test->expected);
    world_run(w, ORACLE_BASE);
    world_run(w, ops);
    const char *actual = w->log ? w->log : "";
    if (strcmp(actual, expected) != 0) {
      failures++;
      report_difference(test->name, expected, actual);
    }
    free(ops);
    free(expected);
    world_destroy(w);
  }
}
/* TS constructor guard and the MissionOwnerDrawId reverse mapping. */
static void test_owner_constructor(void) {
  od_world *w = world_create();
  qa_q3_pres_mission_owner_draw other;
  qa_q3_pres_client_game_state foreign;
  CHECK(qa_q3_pres_client_game_state_init(&foreign, QA_Q3_BASE_PRODUCT_MISSIONPACK, 2, 0));
  qa_q3_pres_client_game_static_state *foreign_static = calloc(1, sizeof *foreign_static);
  qa_q3_pres_client_game_static_state_init(foreign_static, QA_Q3_BASE_PRODUCT_MISSIONPACK);
  qa_q3_pres_mission_owner_draw_host host = world_host(w);
  CHECK_ABORTS(qa_q3_pres_mission_owner_draw_init(&other, &foreign, w->cgs, host));
  CHECK_ABORTS(qa_q3_pres_mission_owner_draw_init(&other, &w->state, foreign_static, host));
  w->state.product = QA_Q3_BASE_PRODUCT_BASEQ3;
  CHECK_ABORTS(qa_q3_pres_mission_owner_draw_init(&other, &w->state, w->cgs, host));
  w->state.product = QA_Q3_BASE_PRODUCT_MISSIONPACK;
  w->cgs->product = QA_Q3_BASE_PRODUCT_BASEQ3;
  CHECK_ABORTS(qa_q3_pres_mission_owner_draw_init(&other, &w->state, w->cgs, host));
  w->cgs->product = QA_Q3_BASE_PRODUCT_MISSIONPACK;
  qa_q3_pres_mission_owner_draw_init(&other, &w->state, w->cgs, host);
  CHECK(other.state == &w->state);
  CHECK(strcmp(qa_q3_pres_mission_owner_draw_id_name(QA_Q3_PRES_OD_TEAMINFO), "CG_TEAMINFO") == 0);
  CHECK(strcmp(qa_q3_pres_mission_owner_draw_id_name(QA_Q3_PRES_OD_1STPLACE), "CG_1STPLACE") == 0);
  CHECK(strcmp(qa_q3_pres_mission_owner_draw_id_name(QA_Q3_PRES_OD_CAPTURES), "CG_CAPTURES") == 0);
  CHECK(qa_q3_pres_mission_owner_draw_id_name((qa_q3_pres_mission_owner_draw_id)13) == NULL);
  qa_q3_pres_client_game_static_state_destroy(foreign_static);
  free(foreign_static);
  qa_q3_pres_client_game_state_destroy(&foreign);
  world_destroy(w);
}

/* ---- menu generations ---- */
static uint32_t g_menu_sentinel = SENTINEL;
static const char MENU_SET[] = "{\n  loadMenu { \"ui/generation.menu\" }\n}\n";
static const char *g_menu_text;
static bool generation_named(const char *path, qa_ui_script_source *out) {
  const char *text = strcmp(path, "ui/set.txt") == 0 ? MENU_SET : strcmp(path, "ui/generation.menu") == 0 ? g_menu_text : NULL;
  if (!text) return false;
  memset(out, 0, sizeof *out);
  out->path = path;
  out->text = (const uint8_t *)text;
  out->text_length = strlen(text);
  return true;
}
static bool generation_root(void *ctx, const char *path, qa_ui_script_source *out) {
  if (*(uint32_t *)ctx != SENTINEL) abort();
  return generation_named(path, out);
}
static bool generation_include(void *ctx, const qa_ui_script_include_request *request, qa_ui_script_source *out) {
  if (*(uint32_t *)ctx != SENTINEL) abort();
  return generation_named(request->requested_path, out);
}
static double generation_random(void *ctx) {
  if (*(uint32_t *)ctx != SENTINEL) abort();
  return 7;
}
static qa_ui_menu_parse_host generation_host(void) {
  qa_ui_menu_parse_host host;
  memset(&host, 0, sizeof host);
  host.kind = QA_UI_PARSE_HOST_RESOLVED;
  host.resolver.resolve_root = generation_root;
  host.resolver.resolve_root_ctx = &g_menu_sentinel;
  host.resolver.resolve = generation_include;
  host.resolver.resolve_ctx = &g_menu_sentinel;
  host.random = generation_random;
  host.random_ctx = &g_menu_sentinel;
  return host;
}
#define GENERATION_MENU(asset, tag) \
  "{\n" asset "menuDef { name \"menu_" tag "\" background \"ui/bg_" tag "\" onOpen { close " tag " }\n" \
  "itemDef { name \"item_" tag "\" type 6 elementwidth 4 elementheight 4 feeder 5 background \"ui/item_" tag "\" action { play " tag " } focusSound \"sound/" tag "\" }\n" \
  "itemDef { name \"model_" tag "\" type 7 asset_model \"models/" tag "\" }\n" \
  "itemDef { name \"edit_" tag "\" type 4 cvar \"x\" maxchars 4 }\n" \
  "}\n}\n"
static const char GENERATION_A[] = GENERATION_MENU("assetGlobalDef { gradientBar \"ui/grad_a\" menuEnterSound \"sound/enter_a\" }\n", "a");
static const char GENERATION_B[] = GENERATION_MENU("", "b");
static const char GENERATION_C[] = GENERATION_MENU("assetGlobalDef { menuExitSound \"sound/exit_c\" }\n", "c");
static qa_ui_menu_definitions *generation_load(qa_ui_team_arena_memory *memory, const char *text, const qa_ui_global_assets *assets) {
  g_menu_text = text;
  qa_ui_menu_parse_host host = generation_host();
  qa_ui_menu_parse_options options;
  memset(&options, 0, sizeof options);
  if (memory) {
    options.has_memory = true;
    options.memory.kind = QA_UI_MENU_MEMORY_QVM32;
    options.memory.memory = memory;
  }
  options.initial_assets = assets;
  qa_ui_menu_load_plan plan = {"ui/set.txt"};
  qa_ui_menu_definitions *definitions = malloc(sizeof *definitions);
  qa_ui_load_menu_definitions(&host, &plan, NULL, &options, definitions);
  CHECK(definitions->menu_count == 1);
  return definitions;
}
static void generation_destroy(qa_ui_menu_definitions *definitions) {
  qa_ui_menu_definitions_destroy(definitions);
  free(definitions);
}
/* Everything a generation parsed must still be readable through the live menu record. */
static void generation_check(qa_ui_menu_definitions *definitions, const char *tag) {
  char expected[32];
  qa_ui_menu_definition *menu = definitions->menus[0];
  char *name = qa_ui_window_name(&menu->window);
  snprintf(expected, sizeof expected, "menu_%s", tag);
  CHECK(name && strcmp(name, expected) == 0);
  free(name);
  CHECK(qa_ui_menu_item_count(menu) == 3);
  qa_ui_item_definition *item = qa_ui_menu_item_at(menu, 0);
  name = qa_ui_window_name(&item->window);
  snprintf(expected, sizeof expected, "item_%s", tag);
  CHECK(name && strcmp(name, expected) == 0);
  free(name);
  CHECK(qa_ui_item_parent(item) == menu);
  snprintf(expected, sizeof expected, "ui/item_%s", tag);
  CHECK(strcmp(qa_ui_window_background(&item->window)->path, expected) == 0);
  char *script = qa_ui_menu_script_text(qa_ui_item_action(item));
  snprintf(expected, sizeof expected, "\"play\" %s ", tag);
  CHECK(script && strcmp(script, expected) == 0);
  free(script);
  snprintf(expected, sizeof expected, "sound/%s", tag);
  CHECK(strcmp(qa_ui_item_focus_sound(item)->path, expected) == 0);
  qa_ui_list_box_definition list;
  CHECK(qa_ui_item_list_data(item, &list));
  CHECK(qa_ui_list_element_width(&list) == 4);
  qa_ui_item_definition *model = qa_ui_menu_item_at(menu, 1);
  snprintf(expected, sizeof expected, "models/%s", tag);
  CHECK(strcmp(qa_ui_item_asset(model)->path, expected) == 0);
  qa_ui_edit_field_definition edit;
  CHECK(qa_ui_item_edit_data(qa_ui_menu_item_at(menu, 2), &edit));
  CHECK(qa_ui_edit_max_chars(&edit) == 4);
}
static void test_generation_order(bool oldest_first) {
  qa_ui_team_arena_memory *memory = qa_ui_team_arena_memory_create("qvm32", NULL, NULL, QA_UI_TEAM_ARENA_MODULE_CGAME);
  qa_ui_menu_definitions *a = generation_load(memory, GENERATION_A, NULL);
  generation_check(a, "a");
  qa_ui_menu_definitions *b = generation_load(memory, GENERATION_B, &a->assets);
  generation_check(b, "b");
  qa_ui_menu_definitions *c = generation_load(memory, GENERATION_C, &b->assets);
  generation_check(c, "c");
  /* All three view the one reused pool record, as in TS, but own distinct items. */
  CHECK(a->menus[0] != c->menus[0] && qa_ui_menu_item_at(a->menus[0], 0) == qa_ui_menu_item_at(c->menus[0], 0));
  CHECK(strcmp(c->assets.gradient_bar->path, "ui/grad_a") == 0 && c->assets.gradient_bar == a->assets.gradient_bar);
  CHECK(strcmp(c->assets.menu_exit_sound->path, "sound/exit_c") == 0);
  if (oldest_first) {
    generation_destroy(a);
    generation_check(c, "c");
    generation_destroy(b);
    generation_check(c, "c");
    CHECK(strcmp(c->assets.gradient_bar->path, "ui/grad_a") == 0);
    CHECK(strcmp(c->assets.menu_enter_sound->path, "sound/enter_a") == 0);
    generation_destroy(c);
  } else {
    generation_destroy(c);
    CHECK(strcmp(b->assets.gradient_bar->path, "ui/grad_a") == 0);
    generation_destroy(b);
    generation_destroy(a);
  }
  qa_ui_team_arena_memory_destroy(memory);
}
/* The unaccounted pool now belongs to the definitions instead of a leaked parser. */
static void test_generation_unaccounted(void) {
  qa_ui_menu_definitions *definitions = generation_load(NULL, GENERATION_A, NULL);
  generation_check(definitions, "a");
  generation_destroy(definitions);
}
static void od_fixture_destroy(fixture *f) {
  qa_q3_pres_scene_recorder_destroy(f->recorder);
  qa_q3_pres_presentation_sound_bank_destroy(f->bank);
  qa_command_buffer_destroy(f->buffer);
  qa_cvar_registry_destroy(f->cvars);
  for (size_t i = 0; i < f->shader_count; i++) free(f->shader_names[i]);
  free(f);
}
/* mission_hud reloads: every older generation is released by destroy. */
static void test_hud_reload(void) {
  fixture *f = fixture_create("0");
  qa_q3_pres_client_presentation *p = mission_create(f);
  CHECK(p != NULL);
  qa_q3_pres_mission_hud *hud = qa_q3_pres_client_presentation_menus(p);
  CHECK(hud->definition_count == 1);
  for (size_t i = 0; i < 4; i++) {
    qa_q3_pres_mission_hud_reset_strings(hud);
    CHECK(qa_q3_pres_mission_hud_load_hud_menu(hud));
    CHECK(hud->definition_count == i + 2);
  }
  attach_snapshot(p);
  qa_q3_pres_mission_hud_paint_all(hud);
  detach_snapshot(p);
  qa_q3_pres_client_presentation_close(p);
  qa_q3_pres_client_presentation_destroy(p);
  od_fixture_destroy(f);
}

/* ---- frame.ts weapon registration ---- */
typedef struct registration_log { size_t count; double order[32]; } registration_log;
static registration_log *g_registration;
static double g_frame_predicted, g_frame_owned;
static qa_q3_entity_state_record g_frame_entities[8];
static size_t g_frame_entity_count;
static bool frame_read(void *ctx, double n, qa_q3_wire_snapshot *out) {
  if (!snap_read(ctx, n, out)) return false;
  out->player_state.weapon = (int64_t)g_frame_predicted;
  out->player_state.stats[(size_t)qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_BASEQ3)->weapons] = (int64_t)g_frame_owned;
  /* The snapshot consumer owns the wire entities it is handed. */
  out->entities = calloc(g_frame_entity_count + 1, sizeof *out->entities);
  if (!out->entities) abort();
  memcpy(out->entities, g_frame_entities, g_frame_entity_count * sizeof *out->entities);
  out->entity_count = g_frame_entity_count;
  return true;
}
static bool frame_register_model(void *ctx, const char *path, qa_q3_pres_scene_model *out) {
  size_t count = 0;
  const qa_q3_base_item_definition *items = qa_q3_base_item_list(QA_Q3_BASE_PRODUCT_BASEQ3, &count);
  for (size_t i = 0; i < count; i++) {
    /* The item visuals and the weapon model both load this path: keep the first. */
    if (items[i].type == QA_Q3_BASE_IT_WEAPON && items[i].world_models[0] && strcmp(items[i].world_models[0], path) == 0 &&
        g_registration->count < 32 && (g_registration->count == 0 || g_registration->order[g_registration->count - 1] != items[i].tag))
      g_registration->order[g_registration->count++] = items[i].tag;
  }
  return r_model(ctx, path, out);
}
static void frame_entities(const int (*spec)[2], size_t count) {
  memset(g_frame_entities, 0, sizeof g_frame_entities);
  for (size_t i = 0; i < count; i++) {
    g_frame_entities[i].number = (int32_t)i + 10;
    g_frame_entities[i].e_type = spec[i][0];
    g_frame_entities[i].weapon = spec[i][1];
  }
  g_frame_entity_count = count;
}
static void frame_child(bool scene) {
  fixture *f = fixture_create("0");
  f->wire_enabled = true;
  f->wire_team_index = QA_Q3_BASE_PERS_TEAM;
  f->wire_team = QA_Q3_BASE_TEAM_FREE;
  qa_q3_pres_presentation_frame input = {1000, QA_Q3_PRES_STEREO_CENTER, false, 2};
  qa_q3_pres_client_presentation *p;
  if (scene) {
    qa_q3_pres_scene_presentation_options options;
    memset(&options, 0, sizeof options);
    options.common = make_common(f, QA_Q3_BASE_PRODUCT_BASEQ3);
    options.common.session.snapshots.read = frame_read;
    options.common.resources.register_model = frame_register_model;
    options.center_print = c_scene_center;
    p = qa_q3_pres_create_scene_presentation(options);
  } else {
    qa_q3_pres_client_presentation_options options = make_primary(f, QA_Q3_BASE_PRODUCT_BASEQ3);
    options.common.session.snapshots.read = frame_read;
    options.common.resources.register_model = frame_register_model;
    p = qa_q3_pres_create_client_presentation(options);
  }
  if (!p) _exit(2);
  g_registration->count = 0;
  bool ok;
  if (scene) {
    qa_scene_camera camera;
    memset(&camera, 0, sizeof camera);
    camera.viewport.width = 640;
    camera.viewport.height = 480;
    camera.projection.values[0] = 1;
    camera.projection.values[5] = 1;
    ok = qa_q3_pres_presentation_frame_runtime_draw_scene_frame(qa_q3_pres_client_presentation_frames(p), input, camera);
  } else {
    ok = qa_q3_pres_presentation_frame_runtime_draw_active_frame(qa_q3_pres_client_presentation_frames(p), input);
  }
  _exit(ok ? 0 : 3);
}
/* Fork: the fixture leaks by design in the depth test, and bad numbers abort. */
static void frame_case(bool scene, double predicted, double owned, const int (*spec)[2], size_t entity_count,
                       const double *expected, size_t expected_count, bool aborts) {
  g_frame_predicted = predicted;
  g_frame_owned = owned;
  frame_entities(spec, entity_count);
  g_registration->count = 0;
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    (void)prctl(PR_SET_DUMPABLE, 0);
    if (!freopen("/dev/null", "w", stderr)) _exit(3);
    frame_child(scene);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  if (aborts) CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  else CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  CHECK(g_registration->count == expected_count);
  for (size_t i = 0; i < expected_count && i < g_registration->count; i++) CHECK(g_registration->order[i] == expected[i]);
}
/* Expected numbers recorded from TS Q3PresentationFrameRuntime under bun; weapon 0 is
 * a registered no-op with no model, so it is absent from what the model log can see. */
static void test_frame_registration(void) {
  g_registration = mmap(NULL, sizeof *g_registration, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (g_registration == MAP_FAILED) abort();
  const int P = QA_Q3_BASE_ET_PLAYER, M = QA_Q3_BASE_ET_MISSILE, G = QA_Q3_BASE_ET_GRAPPLE, I = QA_Q3_BASE_ET_ITEM;
  const int mixed[][2] = {{P, 2}, {I, 9}, {M, 99}, {G, 3}, {M, 6}, {P, 2}};
  /* TS active: 7,3,5,2,0,6. Ascending order would be 2,3,5,6,7. */
  frame_case(false, 7, (1 << 5) | (1 << 3), mixed, 6, (const double[]){7, 3, 5, 2, 6}, 5, false);
  /* TS: owned weapons follow the predicted one, each number once (6,1). */
  const int single[][2] = {{P, 6}};
  frame_case(false, 6, (1 << 1) | (1 << 6), single, 1, (const double[]){6, 1}, 2, false);
  /* TS scene: 7,2,0,3,6; owned bits are not consulted. */
  frame_case(true, 7, 1 << 5, mixed, 6, (const double[]){7, 2, 3, 6}, 4, false);
  /* TS 2026-11-01 (frame.ts, weapons.ts registerWeapon): a player weapon above weaponCount (11 in baseq3) registers
     weapon 0, and registerWeapon ignores a number outside 0..15 or without a weapon item, so neither bad number throws
     any more. A predicted weapon 16 is skipped at registration and then fails requireWeapon in addViewWeapon
     ("Weapon 16 must finish registration"). These four expectations follow from reading the current TS; they were
     not re-recorded under bun. */
  const int high[][2] = {{P, 8}, {P, 16}, {P, 2}}, negative[][2] = {{P, 8}, {P, -1}, {P, 2}};
  frame_case(false, 4, 0, high, 3, (const double[]){4, 8, 2}, 3, false);
  frame_case(false, 4, 0, negative, 3, (const double[]){4, 8, 2}, 3, false);
  frame_case(false, 16, 8, NULL, 0, (const double[]){3}, 1, true);
  frame_case(true, 4, 0, negative, 3, (const double[]){4, 8, 2}, 3, false);
  frame_case(true, 4, 0, high, 3, (const double[]){4, 8, 2}, 3, false);
  munmap(g_registration, sizeof *g_registration);
}
int main(void) {
  test_oracle_cases();
  test_owner_constructor();
  test_generation_order(true);
  test_generation_order(false);
  test_generation_unaccounted();
  test_hud_reload();
  test_frame_registration();
  printf("q3 owner draw: %zu oracle cases, %d checks, %d failures\n", sizeof ORACLE_CASES / sizeof ORACLE_CASES[0], checks, failures);
  return failures ? 1 : 0;
}

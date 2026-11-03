/* Oracle check for src/content/q2/foundation/monsters/muzzle.ts: every id, table
 * length, a bit-exact FNV-1a hash of each table, and sampled offsets, generated from the TypeScript module by running it under
 * bun. GPL-2.0-or-later. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "qa/content/q2/foundation/monsters/muzzle.h"
static int bad=0;
#define CHECK(c) do{ if(!(c)){ bad++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);} }while(0)
static uint64_t hash(const qa_vec3 *t,size_t n){ uint64_t h=0xcbf29ce484222325ULL; for(size_t i=0;i<n;i++){ double v[3]={t[i].x,t[i].y,t[i].z}; const unsigned char *b=(const unsigned char*)v; for(size_t j=0;j<sizeof v;j++){ h^=b[j]; h*=0x100000001b3ULL; } } return h; }
int main(void){
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_1==26);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_2==27);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_3==28);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_4==29);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_5==30);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_6==31);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_7==32);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_8==33);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_9==34);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_10==35);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_11==36);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_12==37);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_13==38);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_BLASTER_1==39);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_BLASTER_2==40);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_SHOTGUN_1==41);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_SHOTGUN_2==42);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_MACHINEGUN_1==43);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_MACHINEGUN_2==44);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_BLASTER_3==83);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_SHOTGUN_3==84);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_MACHINEGUN_3==85);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_BLASTER_4==86);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_SHOTGUN_4==87);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_MACHINEGUN_4==88);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_BLASTER_5==89);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_SHOTGUN_5==90);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_MACHINEGUN_5==91);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_BLASTER_6==92);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_SHOTGUN_6==93);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_MACHINEGUN_6==94);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_BLASTER_7==95);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_SHOTGUN_7==96);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_MACHINEGUN_7==97);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_BLASTER_8==98);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_SHOTGUN_8==99);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_MACHINEGUN_8==100);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_RIPPER_1==211);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_RIPPER_2==212);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_RIPPER_3==213);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_RIPPER_4==214);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_RIPPER_5==215);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_RIPPER_6==216);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_RIPPER_7==217);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_RIPPER_8==218);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_HYPERGUN_1==219);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_HYPERGUN_2==220);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_HYPERGUN_3==221);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_HYPERGUN_4==222);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_HYPERGUN_5==223);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_HYPERGUN_6==224);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_HYPERGUN_7==225);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_HYPERGUN_8==226);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_14==232);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_15==233);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_16==234);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_17==235);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_18==236);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_19==237);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_20==238);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_21==239);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_BLASTER_9==251);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_SHOTGUN_9==252);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_MACHINEGUN_9==253);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_RIPPER_9==254);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_HYPERGUN_9==255);
  CHECK(qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_22==260);
  CHECK(qa_q2m_muzzle_classic_count==212);
  CHECK(hash(qa_q2m_muzzle_classic,qa_q2m_muzzle_classic_count)==0x1ec8c3fc21bc1c49ULL);
  { qa_vec3 v=qa_q2m_muzzle_offset(QA_Q2_EDITION_CLASSIC,0); CHECK(v.x==0&&v.y==0&&v.z==0); }
  { qa_vec3 v=qa_q2m_muzzle_offset(QA_Q2_EDITION_CLASSIC,1); CHECK(v.x==20.700000762939453&&v.y==-18.5&&v.z==28.700000762939453); }
  { qa_vec3 v=qa_q2m_muzzle_offset(QA_Q2_EDITION_CLASSIC,106); CHECK(v.x==3.4000000953674316&&v.y==1.2999999523162842&&v.z==59.5); }
  { qa_vec3 v=qa_q2m_muzzle_offset(QA_Q2_EDITION_CLASSIC,211); CHECK(v.x==0&&v.y==0&&v.z==0); }
  CHECK(qa_q2m_muzzle_rerelease_count==290);
  CHECK(hash(qa_q2m_muzzle_rerelease,qa_q2m_muzzle_rerelease_count)==0xb0b1c182ffaaa904ULL);
  { qa_vec3 v=qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE,0); CHECK(v.x==0&&v.y==0&&v.z==0); }
  { qa_vec3 v=qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE,1); CHECK(v.x==28.700000762939453&&v.y==-18.5&&v.z==28.700000762939453); }
  { qa_vec3 v=qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE,145); CHECK(v.x==1.7000000476837158&&v.y==7&&v.z==11.300000190734863); }
  { qa_vec3 v=qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE,289); CHECK(v.x==0&&v.y==0&&v.z==0); }
  printf("muzzle table bad=%d\n",bad); return bad!=0;
}

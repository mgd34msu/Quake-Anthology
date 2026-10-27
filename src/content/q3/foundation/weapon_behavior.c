/* Port of src/content/q3/foundation/weapon-behavior.ts. GPL-2.0-or-later. */
#include "qa/content/q3/foundation/weapon_behavior.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/foundation/support.h"
qa_q3_projectile_behavior_result qa_q3_projectile_behavior(double weapon){
  const qa_q3_weapon_item *item=qa_q3_get_weapon_item(weapon);
  if(!item) qa_range_error("Unknown Q3 projectile weapon %g",weapon);
  if(weapon==4||weapon==12) return (qa_q3_projectile_behavior_result){item->item,"grenade"};
  if(weapon==5) return (qa_q3_projectile_behavior_result){item->item,"rocket"};
  if(weapon==8) return (qa_q3_projectile_behavior_result){item->item,"plasma"};
  if(weapon==9) return (qa_q3_projectile_behavior_result){item->item,"energy"};
  if(weapon==10) return (qa_q3_projectile_behavior_result){item->item,"grapple"};
  if(weapon==11) return (qa_q3_projectile_behavior_result){item->item,"nail"};
  qa_range_error("Q3 weapon %g does not launch a projectile",weapon);
}

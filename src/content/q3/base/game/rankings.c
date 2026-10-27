/* Port of src/content/q3/base/game/rankings.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/rankings.h"
#include <stddef.h>
#include "qa/foundation/support.h"
static bool qa_q3_ranking_reports_warmup_check(qa_q3_ranking_reports *r) { return r->warmup ? r->warmup(r->ctx) : true; }
void qa_q3_ranking_reports_init(qa_q3_ranking_reports *r) { r->ctx=NULL; r->sink=NULL; r->warmup=NULL; r->attached=false; }
void qa_q3_ranking_reports_attach(qa_q3_ranking_reports *r, void *ctx, void (*sink)(void*,qa_q3_ranking_report), bool (*warmup)(void*)) {
  if (r->attached) qa_range_error("Ranking report owner already attached");
  r->ctx=ctx; r->sink=sink; r->warmup=warmup; r->attached=true;
}
void qa_q3_ranking_reports_detach(qa_q3_ranking_reports *r) { r->sink=NULL; r->warmup=NULL; r->attached=false; }
void qa_q3_ranking_reports_integer(qa_q3_ranking_reports *r, double self, double other, double key, double value, bool accumulate) {
  if (!qa_q3_ranking_reports_warmup_check(r) && r->sink) { qa_q3_ranking_report rep={QA_Q3_RANK_INTEGER,self,other,key,value,accumulate,NULL}; r->sink(r->ctx,rep); }
}
void qa_q3_ranking_reports_string(qa_q3_ranking_reports *r, double self, double other, double key, const char *value) {
  if (!qa_q3_ranking_reports_warmup_check(r) && r->sink) { qa_q3_ranking_report rep={QA_Q3_RANK_STRING,self,other,key,0,false,value}; r->sink(r->ctx,rep); }
}
void qa_q3_ranking_reports_fire_weapon(qa_q3_ranking_reports *r, double self, double weapon) {
  if (qa_q3_ranking_reports_warmup_check(r)) return;
  
  if( weapon == 1 )
  {
    
    return;
  }
  
  qa_q3_ranking_reports_integer(r,  self, -1, 1111020002, 1, true);
  
  switch ((int)weapon)
  {
  case 2:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020202, 1, true);
    break;
  case 3:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020302, 1, true);
    break;
  case 4:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020402, 1, true);
    break;
  case 5:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020502, 1, true);
    break;
  case 6:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020802, 1, true);
    break;
  case 7:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020702, 1, true);
    break;
  case 8:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020602, 1, true);
    break;
  case 9:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020902, 1, true);
    break;
  case 10:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111021002, 1, true);
    break;
  default:
    break;
  }
}

void qa_q3_ranking_reports_pickup_weapon(qa_q3_ranking_reports *r, double self, double weapon) {
  if (qa_q3_ranking_reports_warmup_check(r)) return;
  
  qa_q3_ranking_reports_integer(r,  self, -1, 1111020009, 1, true);
  switch ((int)weapon)
  {
  case 1:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020109, 1, true);
    break;
  case 2:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020209, 1, true);
    break;
  case 3:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020309, 1, true);
    break;
  case 4:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020409, 1, true);
    break;
  case 5:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020509, 1, true);
    break;
  case 6:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020809, 1, true);
    break;
  case 7:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020709, 1, true);
    break;
  case 8:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020609, 1, true);
    break;
  case 9:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020909, 1, true);
    break;
  case 10:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111021009, 1, true);
    break;
  default:
    break;
  }
}

void qa_q3_ranking_reports_pickup_ammo(qa_q3_ranking_reports *r, double self, double weapon, double quantity) {
  if (qa_q3_ranking_reports_warmup_check(r)) return;
  
  qa_q3_ranking_reports_integer(r,  self, -1, 1111030000, 1, true);
  qa_q3_ranking_reports_integer(r,  self, -1, 1111030001, quantity, true);
  
  switch ((int)weapon)
  {
  case 2:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030100, 1, true);
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030101, quantity, true);
    break;
  case 3:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030200, 1, true);
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030201, quantity, true);
    break;
  case 4:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030300, 1, true);
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030301, quantity, true);
    break;
  case 5:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030400, 1, true);
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030401, quantity, true);
    break;
  case 6:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030700, 1, true);
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030701, quantity, true);
    break;
  case 7:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030600, 1, true);
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030601, quantity, true);
    break;
  case 8:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030500, 1, true);
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030501, quantity, true);
    break;
  case 9:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030800, 1, true);
    qa_q3_ranking_reports_integer(r,  self, -1, 1111030801, quantity, true);
    break;
  default:
    break;
  }
}

void qa_q3_ranking_reports_pickup_health(qa_q3_ranking_reports *r, double self, double quantity) {
  if (qa_q3_ranking_reports_warmup_check(r)) return;
  
  qa_q3_ranking_reports_integer(r,  self, -1, 1111040000, 1, true);
  qa_q3_ranking_reports_integer(r,  self, -1, 1111040001, quantity, true);

  switch ((int)quantity)
  {
  case 5:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111040100, 1, true);
    break;
  case 25:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111040200, 1, true);
    break;
  case 50:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111040300, 1, true);
    break;
  case 100:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111040400, 1, true);
    break;
  default:
    break;
  }
}

void qa_q3_ranking_reports_pickup_armor(qa_q3_ranking_reports *r, double self, double quantity) {
  if (qa_q3_ranking_reports_warmup_check(r)) return;
  
  qa_q3_ranking_reports_integer(r,  self, -1, 1111050000, 1, true);
  qa_q3_ranking_reports_integer(r,  self, -1, 1111050001, quantity, true);

  switch ((int)quantity)
  {
  case 5:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111050100, 1, true);
    break;
  case 50:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111050200, 1, true);
    break;
  case 100:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111050300, 1, true);
    break;
  default:
    break;
  }
}

void qa_q3_ranking_reports_pickup_powerup(qa_q3_ranking_reports *r, double self, double powerup) {
  if (qa_q3_ranking_reports_warmup_check(r)) return;
  
  
  if( (powerup == 7) || (powerup == 8) )
  {
    qa_q3_ranking_reports_integer(r,  self, -1, 1111110000, 1, true);
    return;
  }

  qa_q3_ranking_reports_integer(r,  self, -1, 1111060000, 1, true);
  
  switch ((int)powerup)
  {
  case 1:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111060100, 1, true);
    break;
  case 2:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111060200, 1, true);
    break;
  case 3:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111060300, 1, true);
    break;
  case 4:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111060400, 1, true);
    break;
  case 5:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111060500, 1, true);
    break;
  case 6:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111060600, 1, true);
    break;
  default:
    break;
  }
}

void qa_q3_ranking_reports_pickup_holdable(qa_q3_ranking_reports *r, double self, double holdable) {
  if (qa_q3_ranking_reports_warmup_check(r)) return;
  
  switch ((int)holdable)
  {
  case 2:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111070000, 1, true);
    break;
  case 1:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111070100, 1, true);
    break;
  default:
    break;
  }
}

void qa_q3_ranking_reports_use_holdable(qa_q3_ranking_reports *r, double self, double holdable) {
  if (qa_q3_ranking_reports_warmup_check(r)) return;
  
  switch ((int)holdable)
  {
  case 2:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111070001, 1, true);
    break;
  case 1:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111070101, 1, true);
    break;
  default:
    break;
  }
}

void qa_q3_ranking_reports_reward(qa_q3_ranking_reports *r, double self, double award) {
  if (qa_q3_ranking_reports_warmup_check(r)) return;
  
  switch ((int)award)
  {
  case 0x8000:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111090000, 1, true);
    break;
  case 0x8:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111090100, 1, true);
    break;
  default:
    break;
  }
}

void qa_q3_ranking_reports_capture(qa_q3_ranking_reports *r, double self) {
  if (qa_q3_ranking_reports_warmup_check(r)) return;
  
  qa_q3_ranking_reports_integer(r,  self, -1, 1111110001, 1, true);
}

void qa_q3_ranking_reports_damage(qa_q3_ranking_reports *r, double self, double attacker, double damage, double means_of_death, double frame, bool attacker_is_client, bool same_team) {
  
  
  
  
  

  
  double splash = 0;
  double key_hit = 0;
  double key_damage = 0;
  double key_splash = 0;

  if (qa_q3_ranking_reports_warmup_check(r)) return;
  
  bool new_hit = !(r->last_hit_frame==frame && r->last_hit_self==self && r->last_hit_attacker==attacker && r->last_hit_mod==means_of_death && r->has_last_hit); r->has_last_hit=true; r->last_hit_frame=frame; r->last_hit_self=self; r->last_hit_attacker=attacker; r->last_hit_mod=means_of_death;

  
  if( (attacker != 1022) && (attacker != self) && 
    (means_of_death == 2)  && 
    (attacker_is_client) )
  {
    qa_q3_ranking_reports_integer(r,  attacker, -1, 1111020102, 1, true);
  }

  
  switch ((int)means_of_death)
  {
  case 14:
  case 15:
  case 16:
  case 17:
  case 18:
  case 19:
  case 20:
  case 22:
    return;
  default:
    break;
  }

  
  switch ((int)means_of_death)
  {
  case 5:
  case 7:
  case 9:
  case 13:
    splash = damage;
    break;
  default:
    splash = 0;
    key_splash = -1;
    break;
  }
  
  
  switch ((int)means_of_death)
  {
  case 2:
    key_hit = 1111020104;
    key_damage = 1111020106;
    break;
  case 3:
    key_hit = 1111020204;
    key_damage = 1111020206;
    break;
  case 1:
    key_hit = 1111020304;
    key_damage = 1111020306;
    break;
  case 4:
  case 5:
    key_hit = 1111020404;
    key_damage = 1111020406;
    key_splash = 1111020408;
    break;
  case 6:
  case 7:
    key_hit = 1111020504;
    key_damage = 1111020506;
    key_splash = 1111020508;
    break;
  case 8:
  case 9:
    key_hit = 1111020604;
    key_damage = 1111020606;
    key_splash = 1111020608;
    break;
  case 10:
    key_hit = 1111020704;
    key_damage = 1111020706;
    break;
  case 11:
    key_hit = 1111020804;
    key_damage = 1111020806;
    break;
  case 12:
  case 13:
    key_hit = 1111020904;
    key_damage = 1111020906;
    key_splash = 1111020908;
    break;
  case 23:
    key_hit = 1111021004;
    key_damage = 1111021006;
    break;
  default:
    key_hit = 1111021104;
    key_damage = 1111021106;
    break;
  }

  
  if( new_hit )
  {
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020004, 1, true);
    qa_q3_ranking_reports_integer(r,  self, -1, key_hit, 1, true);
  }
  
  
  qa_q3_ranking_reports_integer(r,  self, -1, 1111020006, damage, true);
  qa_q3_ranking_reports_integer(r,  self, -1, key_damage, damage, true);

  
  if( splash != 0 )
  {
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020008, splash, true);
    qa_q3_ranking_reports_integer(r,  self, -1, key_splash, splash, true);
  }

  
  if( (attacker != 1022) && (attacker != self) )
  {
    switch ((int)means_of_death)
    {
    case 2:
      key_hit = 1111020103;
      key_damage = 1111020105;
      break;
    case 3:
      key_hit = 1111020203;
      key_damage = 1111020205;
      break;
    case 1:
      key_hit = 1111020303;
      key_damage = 1111020305;
      break;
    case 4:
    case 5:
      key_hit = 1111020403;
      key_damage = 1111020405;
      key_splash = 1111020407;
      break;
    case 6:
    case 7:
      key_hit = 1111020503;
      key_damage = 1111020505;
      key_splash = 1111020507;
      break;
    case 8:
    case 9:
      key_hit = 1111020603;
      key_damage = 1111020605;
      key_splash = 1111020607;
      break;
    case 10:
      key_hit = 1111020703;
      key_damage = 1111020705;
      break;
    case 11:
      key_hit = 1111020803;
      key_damage = 1111020805;
      break;
    case 12:
    case 13:
      key_hit = 1111020903;
      key_damage = 1111020905;
      key_splash = 1111020907;
      break;
    case 23:
      key_hit = 1111021003;
      key_damage = 1111021005;
      break;
    default:
      key_hit = 1111021103;
      key_damage = 1111021105;
      break;
    }
    
    
    
    
    
    if (attacker_is_client) {
      if( new_hit )
      {
        qa_q3_ranking_reports_integer(r,  attacker, -1, 1111020003, 1, true);
        qa_q3_ranking_reports_integer(r,  attacker, -1, key_hit, 1, true);
      }
      
      
      qa_q3_ranking_reports_integer(r,  attacker, -1, 1111020005, damage, true);
      qa_q3_ranking_reports_integer(r,  attacker, -1, key_damage, damage, true);

      
      if( splash != 0 )
      {
        qa_q3_ranking_reports_integer(r,  attacker, -1, 1111020007, splash, true);
        qa_q3_ranking_reports_integer(r,  attacker, -1, key_splash, splash, true);
      }
    }
  }

  
  if( (attacker != self) && 
    same_team &&
    (attacker_is_client) )
  {
    
    if( new_hit )
    {
      qa_q3_ranking_reports_integer(r,  self, -1, 1111100002, 1, true);
      qa_q3_ranking_reports_integer(r,  attacker, -1, 1111100001, 1, true);
    }

    
    qa_q3_ranking_reports_integer(r,  self, -1, 1111100004, damage, true);
    qa_q3_ranking_reports_integer(r,  attacker, -1, 1111100003, 
      damage, true);
      
    
    if( splash != 0 )
    {
      qa_q3_ranking_reports_integer(r,  self, -1, 1111100006, 
        splash, true);
      qa_q3_ranking_reports_integer(r,  attacker, -1, 1111100005, 
        splash, true);
    }
  }
}

void qa_q3_ranking_reports_player_die(qa_q3_ranking_reports *r, double self, double attacker, double means_of_death) {
  double p1 = 0;
  double p2 = 0;

  if (qa_q3_ranking_reports_warmup_check(r)) return;
  
  if( attacker == 1022 )
  {
    p1 = self;
    p2 = -1;
    
    qa_q3_ranking_reports_integer(r,  p1, p2, 1111080000, 1, true);

    switch ((int)means_of_death)
    {
    case 14:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111080100, 1, true);
      break;
    case 15:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111080200, 1, true);
      break;
    case 16:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111080300, 1, true);
      break;
    case 17:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111080400, 1, true);
      break;
    case 18:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111080500, 1, true);
      break;
    case 19:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111080600, 1, true);
      break;
    case 20:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111080700, 1, true);
      break;
    case 22:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111080800, 1, true);
      break;
    default:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111080900, 1, true);
      break;
    }
  }
  else if( attacker == self )
  {
    p1 = self;
    p2 = -1;
    
    qa_q3_ranking_reports_integer(r,  p1, p2, 1111020001, 1, true);
    
    switch ((int)means_of_death)
    {
    case 2:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111020101, 1, true);
      break;
    case 3:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111020201, 1, true);
      break;
    case 1:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111020301, 1, true);
      break;
    case 4:
    case 5:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111020401, 1, true);
      break;
    case 6:
    case 7:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111020501, 1, true);
      break;
    case 8:
    case 9:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111020601, 1, true);
      break;
    case 10:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111020701, 1, true);
      break;
    case 11:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111020801, 1, true);
      break;
    case 12:
    case 13:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111020901, 1, true);
      break;
    case 23:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111021001, 1, true);
      break;
    default:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1111021101, 1, true);
      break;
    }
  }
  else
  {
    p1 = attacker;
    p2 = self;

    qa_q3_ranking_reports_integer(r,  p1, p2, 1211020000, 1, true);
    
    switch ((int)means_of_death)
    {
    case 2:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1211020100, 1, true);
      break;
    case 3:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1211020200, 1, true);
      break;
    case 1:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1211020300, 1, true);
      break;
    case 4:
    case 5:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1211020400, 1, true);
      break;
    case 6:
    case 7:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1211020500, 1, true);
      break;
    case 8:
    case 9:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1211020600, 1, true);
      break;
    case 10:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1211020700, 1, true);
      break;
    case 11:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1211020800, 1, true);
      break;
    case 12:
    case 13:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1211020900, 1, true);
      break;
    case 23:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1211021000, 1, true);
      break;
    default:
      qa_q3_ranking_reports_integer(r,  p1, p2, 1211021100, 1, true);
      break;
    }
  }
}

void qa_q3_ranking_reports_weapon_time(qa_q3_ranking_reports *r, double self, double weapon, double time) {
    if (time <= 0 || qa_q3_ranking_reports_warmup_check(r)) return;
    qa_q3_ranking_reports_integer(r, self, -1, 1111020010, time, true);
  switch ((int)weapon)
  {
  case 1:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020110, time, true);
    break;
  case 2:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020210, time, true);
    break;
  case 3:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020310, time, true);
    break;
  case 4:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020410, time, true);
    break;
  case 5:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020510, time, true);
    break;
  case 6:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020810, time, true);
    break;
  case 7:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020710, time, true);
    break;
  case 8:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020610, time, true);
    break;
  case 9:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111020910, time, true);
    break;
  case 10:
    qa_q3_ranking_reports_integer(r,  self, -1, 1111021010, time, true);
    break;
  default:
    break;
  }
}

void qa_q3_ranking_reports_team_name(qa_q3_ranking_reports *r, double self, const char *name) { qa_q3_ranking_reports_string(r, self, -1, 1100100007, name); }


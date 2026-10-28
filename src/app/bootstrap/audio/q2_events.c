/* Port of src/app/bootstrap/audio/q2-events.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/audio/q2_events.h"
#include <stdio.h>
#include <string.h>
static qa_app_q2_event_sound mk(const char *path, double channel, double attenuation,
                                double volume, double delay) {
  qa_app_q2_event_sound s;
  memset(&s, 0, sizeof s);
  snprintf(s.path, sizeof s.path, "%s", path);
  s.channel = channel; s.attenuation = attenuation; s.volume = volume; s.delay_seconds = delay;
  return s;
}
bool qa_app_q2_entity_sound(double event, qa_app_q2_random_fn random, void *random_ctx,
                            qa_app_q2_event_sound *out) {
  char buf[64];
  if (event == 1) { *out = mk("items/respawn1.wav", 1, 2, 1, 0); return true; }
  if (event == 2) {
    snprintf(buf, sizeof buf, "player/step%lld.wav", (long long)((random(random_ctx) & 3) + 1));
    *out = mk(buf, 4, 1, 1, 0); return true;
  }
  if (event == 3) { *out = mk("player/land1.wav", 0, 1, 1, 0); return true; }
  if (event == 4) { *out = mk("*fall2.wav", 0, 1, 1, 0); return true; }
  if (event == 5) { *out = mk("*fall1.wav", 0, 1, 1, 0); return true; }
  if (event == 6) { *out = mk("misc/tele1.wav", 1, 2, 1, 0); return true; }
  return false;
}
size_t qa_app_q2_muzzle_sounds(double flash, bool silenced, qa_app_q2_random_fn random,
                               void *random_ctx, bool rerelease, qa_app_q2_event_sound out[3]) {
  const double volume = silenced ? 0.2 : 1;
  char buf[64];
  /* TS machinegun closure, inlined per call site below. */
  long long key = (long long)flash & ~128LL;
  switch (key) {
    case 0: case 34: out[0] = mk("weapons/blastf1a.wav", 1, 1, volume, 0); return 1;
    case 1: case 3:
      snprintf(buf, sizeof buf, "weapons/machgf%lldb.wav", (long long)(random(random_ctx) % 5 + 1));
      out[0] = mk(buf, 1, 1, volume, 0); return 1;
    case 2:
      out[0] = mk("weapons/shotgf1b.wav", 1, 1, volume, 0);
      out[1] = mk("weapons/shotgr1b.wav", 0, 1, volume, 0.1); return 2;
    case 4: {
      size_t n = 0;
      snprintf(buf, sizeof buf, "weapons/machgf%lldb.wav", (long long)(random(random_ctx) % 5 + 1));
      out[n++] = mk(buf, 1, 1, volume, 0);
      snprintf(buf, sizeof buf, "weapons/machgf%lldb.wav", (long long)(random(random_ctx) % 5 + 1));
      out[n++] = mk(buf, 1, 1, volume, 0.05); return n;
    }
    case 5: {
      size_t n = 0;
      snprintf(buf, sizeof buf, "weapons/machgf%lldb.wav", (long long)(random(random_ctx) % 5 + 1));
      out[n++] = mk(buf, 1, 1, volume, 0);
      snprintf(buf, sizeof buf, "weapons/machgf%lldb.wav", (long long)(random(random_ctx) % 5 + 1));
      out[n++] = mk(buf, 1, 1, volume, 0.033);
      snprintf(buf, sizeof buf, "weapons/machgf%lldb.wav", (long long)(random(random_ctx) % 5 + 1));
      out[n++] = mk(buf, 1, 1, volume, 0.066); return n;
    }
    case 6:
      out[0] = mk("weapons/railgf1a.wav", 1, 1, volume, 0);
      if (rerelease) { out[1] = mk("weapons/railgr1b.wav", 7, 1, volume, 0.4); return 2; }
      return 1;
    case 7:
      out[0] = mk("weapons/rocklf1a.wav", 1, 1, volume, 0);
      out[1] = mk("weapons/rocklr1b.wav", 0, 1, volume, 0.1); return 2;
    case 8:
      out[0] = mk("weapons/grenlf1a.wav", 1, 1, volume, 0);
      out[1] = mk("weapons/grenlr1b.wav", 0, 1, volume, 0.1); return 2;
    case 9: case 10: case 11: out[0] = mk("weapons/grenlf1a.wav", 1, 1, 1, 0); return 1;
    case 12: out[0] = mk("weapons/bfg__f1y.wav", 1, 1, volume, 0); return 1;
    case 13: out[0] = mk("weapons/sshotf1b.wav", 1, 1, volume, 0); return 1;
    case 14: case 17: out[0] = mk("weapons/hyprbf1a.wav", 1, 1, volume, 0); return 1;
    case 16: out[0] = mk("weapons/rippfire.wav", 1, 1, volume, 0); return 1;
    case 18: out[0] = mk("weapons/plasshot.wav", 1, 1, volume, 0); return 1;
    case 30: out[0] = mk("weapons/nail1.wav", 1, 1, volume, 0); return 1;
    case 32: out[0] = mk("weapons/shotg2.wav", 1, 1, volume, 0); return 1;
    case 35: out[0] = mk("weapons/disint2.wav", 1, 1, volume, 0); return 1;
    default: return 0;
  }
}
static int one(qa_app_q2_event_sound *out, const char *path, double attenuation) {
  *out = mk(path, 1, attenuation, 1, 0);
  return 1;
}
int qa_app_q2_monster_muzzle_sounds(double flash, qa_app_q2_random_fn random, void *random_ctx,
                                    bool rerelease, qa_app_q2_event_sound *out) {
  long long f = (long long)flash;
  char buf[64];
  if (rerelease) {
    switch (f) {
      case 232: case 233: case 234: case 235: case 236: case 237: case 238: case 239: case 260:
        return one(out, "infantry/infatck1.wav", 1);
      case 251: return one(out, "soldier/solatck2.wav", 1);
      case 252: return one(out, "soldier/solatck1.wav", 1);
      case 253: return one(out, "soldier/solatck3.wav", 1);
      case 256: case 257: case 258: case 259: return one(out, "gunner/gunatck3.wav", 1);
      case 263: return one(out, "hover/hovatck1.wav", 1);
      default: break;
    }
  }
  switch (f) {
    case 26: case 27: case 28: case 29: case 30: case 31: case 32: case 33: case 34: case 35:
    case 36: case 37: case 38: return one(out, "infantry/infatck1.wav", 1);
    case 43: case 44: case 85: case 88: case 91: case 94: case 97: case 100:
      return one(out, "soldier/solatck3.wav", 1);
    case 45: case 46: case 47: case 48: case 49: case 50: case 51: case 52:
      return one(out, "gunner/gunatck2.wav", 1);
    case 63: case 64: case 65: case 66: case 67: case 68: case 69: case 141:
      return one(out, "infantry/infatck1.wav", 1);
    case 73: case 74: case 75: case 76: case 77: case 138: case 152:
      return one(out, rerelease && f == 74 ? "flyer/flyatck3.wav" : "infantry/infatck1.wav", 0);
    case 39: case 40: case 83: case 86: case 89: case 92: case 95: case 98: case 143:
      return one(out, "soldier/solatck2.wav", 1);
    case 58: case 59: return one(out, "flyer/flyatck3.wav", 1);
    case 60: return one(out, "medic/medatck1.wav", 1);
    case 62: return one(out, "hover/hovatck1.wav", 1);
    case 82: return one(out, "floater/fltatck1.wav", 1);
    case 41: case 42: case 84: case 87: case 90: case 93: case 96: case 99:
      return one(out, "soldier/solatck1.wav", 1);
    case 1: case 2: case 3: return one(out, "tank/tnkatck3.wav", 1);
    case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12: case 13:
    case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 21: case 22:
      snprintf(buf, sizeof buf, "tank/tnkatk2%c.wav", (char)(97 + (int)(random(random_ctx) % 5)));
      return one(out, buf, 1);
    case 57: case 142: return one(out, "chick/chkatck2.wav", 1);
    case 23: case 24: case 25: return one(out, "tank/tnkatck1.wav", 1);
    case 70: case 71: case 72: case 78: case 79: case 80: case 81: case 191:
      return one(out, "tank/rocket.wav", 1);
    case 53: case 54: case 55: case 56: return one(out, "gunner/gunatck3.wav", 1);
    case 61: case 147: case 150: return 0;
    case 101: return 0;
    case 102: case 103: case 104: case 105: case 106: case 107: case 108: case 109: case 110:
    case 111: case 112: case 113: case 114: case 115: case 116: case 117: case 118:
      return one(out, "makron/blaster.wav", 1);
    case 120: case 121: case 122: case 123: case 124: case 125:
      return one(out, "boss3/xfire.wav", 1);
    case 126: case 127: case 128: case 129: case 130: case 131: return 0;
    case 132: return 0;
    case 133: case 134: case 135: case 136: case 137: case 139: case 153:
      return rerelease && f == 134 ? one(out, "flyer/flyatck3.wav", 0) : 0;
    case 144: case 145: case 146: case 149: case 156: case 157: case 158: case 159: case 160:
    case 161: case 162: case 163: case 164: case 165: case 166: case 167: case 168: case 169:
    case 170: case 171: case 172: case 173: case 174: case 175: case 176: case 177: case 178:
    case 179: case 180: case 181: case 182: case 183: case 184: case 185: case 186: case 187:
    case 188: case 189: case 190: return one(out, "tank/tnkatck3.wav", 1);
    case 148: return one(out, "weapons/disint2.wav", 1);
    case 151: case 195: case 196: case 197: case 198: case 199: case 200: case 201: case 202:
    case 203: case 204: case 205: case 206: case 207: case 208: case 209: case 210: return 0;
    default: return -1;
  }
}

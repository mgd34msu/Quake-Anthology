// Prints the bun-derived checks pasted into tests/foundation/test_js_semantics.c:
//   bun tools/js_semantics_reference.js
// Emits C check lines; each expected value printed by bun from the JS expression.
const c = v => { if (Number.isNaN(v)) return "NAN"; if (v === Infinity) return "INFINITY"; if (v === -Infinity) return "-INFINITY"; if (Object.is(v,-0)) return "-0.0"; let s = v.toString(); if (!/[.e]/.test(s)) s += ".0"; return s; };
const cs = s => JSON.stringify(s);
const out = [];
const nums = [NaN, 0, -0, 1, -1, Infinity, -Infinity, 0.5, -0.5, 2.5, -2.5, 1.5, -1.5, 0.49999999999999994, -0.49999999999999994, 4503599627370495.5, 4503599627370497, -4503599627370497, 2147483647.5, 2147483648, -2147483648.5, -2147483649, 4294967295, 4294967296, 4294967297.9, -4294967297.9, 9007199254740993, 18446744073709552000, 1e21, -1e300, 3.7, -3.7, 254.5, 255.5, 253.5, 65535.5, 65536, 32768, -32769, 128, -129, 5e-324];
for (const v of nums) {
  out.push(`  SAME(qa_js_round(${c(v)}), ${c(Math.round(v))});`);
  out.push(`  SAME(qa_js_sign(${c(v)}), ${c(Math.sign(v))});`);
  out.push(`  CHECK(qa_js_to_int32(${c(v)}) == ${(v|0)}LL);`);
  out.push(`  CHECK(qa_js_to_uint32(${c(v)}) == ${(v>>>0)}ULL);`);
  out.push(`  CHECK(qa_js_to_int16(${c(v)}) == ${new Int16Array([v])[0]});`);
  out.push(`  CHECK(qa_js_to_uint16(${c(v)}) == ${new Uint16Array([v])[0]});`);
  out.push(`  CHECK(qa_js_to_int8(${c(v)}) == ${new Int8Array([v])[0]});`);
  out.push(`  CHECK(qa_js_to_uint8(${c(v)}) == ${new Uint8Array([v])[0]});`);
  out.push(`  CHECK(qa_js_to_uint8_clamp(${c(v)}) == ${new Uint8ClampedArray([v])[0]});`);
}
const mm = [NaN, -0, 0, 1, -1, Infinity, -Infinity];
for (const a of mm) for (const b of mm) { out.push(`  SAME(qa_js_min(${c(a)}, ${c(b)}), ${c(Math.min(a,b))});`); out.push(`  SAME(qa_js_max(${c(a)}, ${c(b)}), ${c(Math.max(a,b))});`); }
const pw = [[1.1,10],[37.946147918701172,16],[3324.8348236083984,19],[0.7,-0.5],[0.7,0.5],[-0,0.5],[-0,-0.5],[-Infinity,0.5],[-Infinity,-0.5],[2,1000],[1.0000001,1001],[1,Infinity],[-1,-Infinity],[NaN,0],[0,NaN],[-8,1/3],[-2,3],[-2,-3],[10,-3],[46.995790004730225,2],[-226.26833915710449,0.5],[1.5,2147483648]];
for (const [a,b] of pw) out.push(`  SAME(qa_js_pow(${c(a)}, ${c(b)}), ${c(Math.pow(a,b))});`);
const hy = [[3,4],[0.035515404846519237,2.0397114753723145],[407.27384090423584,2.463982105255127],[0.035515404846519237,2.0397114753723145,0.01065462145],[571.30641937255859,-8.3411407470703125,171.39192581176758],[37.946147918701172,6.8120884895324707,11.383844375610352],[0.1,0.2,0.3],[1,2,3,4],[0.3672189712524414,-2.4291e-7,-0.08186203241348267,-3.7568e-7],[NaN,Infinity],[Infinity,NaN,1],[NaN,1,2],[-0,-0],[-0,0,-0],[],[-5],[NaN],[1e200,1e200,1e200],[1e-200,1e-200,1e-200,1e-200],[NaN,1,2,Infinity]];
for (const v of hy) {
  const r = c(Math.hypot(...v));
  if (v.length === 2) out.push(`  SAME(qa_js_hypot2(${v.map(c).join(", ")}), ${r});`);
  if (v.length === 3) out.push(`  SAME(qa_js_hypot3(${v.map(c).join(", ")}), ${r});`);
  out.push(`  { const double values[] = {${v.length ? v.map(c).join(", ") : "0"}}; SAME(qa_js_hypot(${v.length}, values), ${r}); }`);
}
const ps = ["", "  12abc", "0x1F", "-0x1F", "  -0x1F", "+7", "-", "1e3", "Infinity", "-Infinity", "  .5", "-.5e2x", "0b101", "0o17", "\t\n 42 ", "inf", "nan", "INF", "1_000", "12345678901234567890", "9007199254740993", "-0", "08", "0.0000001", "1e400", "-1e400", "+Infinity", "Infinityx", "e5", "5e", "5e+", "5.", ".", "+.e1", " 42　", "﻿7", "0X1g", "0x", "0x1p3", "1.5e-3z", "fffffffffffffffffff", "zz", "  +0x10", "0x1fffffffffffff1", "1e-400", "-.0", "123456789012345678901234567890"];
for (const p of ps) {
  out.push(`  SAME(qa_js_parse_int(${cs(p)}, 0), ${c(parseInt(p))});`);
  for (const r of [2, 10, 16, 36, 1, 37]) out.push(`  SAME(qa_js_parse_int(${cs(p)}, ${r}), ${c(parseInt(p, r))});`);
  out.push(`  SAME(qa_js_parse_float(${cs(p)}), ${c(parseFloat(p))});`);
  out.push(`  SAME(qa_js_string_to_number(${cs(p)}), ${c(Number(p))});`);
}
const fx = [[0.5,0],[1.5,0],[2.5,0],[-2.5,0],[-0.5,0],[1.25,1],[1.005,2],[-0,2],[-0.001,2],[NaN,2],[Infinity,1],[-Infinity,0],[1e21,2],[-1.5e300,3],[123.456,2],[0,0],[1.45,1],[8.345,2],[0.000001,7],[-1.5,0],[99.995,2],[1e20,2],[5e-7,6],[999.9999,3],[0.1,20],[1.7976931348623157e308,1],[5e-324,100],[123456789012345680000,0],[9.5,0],[-9.5,0],[0.05,1],[2.345,2]];
for (const [v,d] of fx) out.push(`  FIXED(${c(v)}, ${d}, ${cs(v.toFixed(d))});`);
const bw = [NaN, 0, -0, 1, -1, 16, 0x10000, 2147483647, 2147483648, -2147483648, 4294967295, 4294967296 + 16, 2 ** 32 + 0x20000, -2.5, 3.7, Infinity, 1e21, 0x80000000 + 0x10];
for (const a of bw) {
  out.push(`  SAME(qa_js_bit_not(${c(a)}), ${c(~a)});`);
  for (const b of [16, ~16, 0x10000, 0x80000000, -1, 3.9, NaN, 31, 32, 33, -1]) {
    out.push(`  SAME(qa_js_bit_and(${c(a)}, ${c(b)}), ${c(a & b)});`);
    out.push(`  SAME(qa_js_bit_or(${c(a)}, ${c(b)}), ${c(a | b)});`);
    out.push(`  SAME(qa_js_bit_xor(${c(a)}, ${c(b)}), ${c(a ^ b)});`);
    out.push(`  SAME(qa_js_shl(${c(a)}, ${c(b)}), ${c(a << b)});`);
    out.push(`  SAME(qa_js_shr(${c(a)}, ${c(b)}), ${c(a >> b)});`);
    out.push(`  SAME(qa_js_ushr(${c(a)}, ${c(b)}), ${c(a >>> b)});`);
  }
}
console.log(out.join("\n"));

// Prints the bun-derived checks pasted into tests/foundation/test_js_case.c:
//   bun tools/js_case_reference.js
const cs = s => JSON.stringify(s).replace(/\\u00([0-7][0-9a-f])/g, '\\x$1""').replace(/[\u0080-￿]/g, ch => { const b = Buffer.from(ch, "utf8"); return [...b].map(x => "\\x" + x.toString(16)).join("") + '""'; });
const out = [];
const texts = ["", "ABC def", "ÀÉÎÕÜ ÿ ß", "İstanbul", "ΑΣ", "Σ", "ΑΣ Α", "ΑΣ.", "ΑΣΑ", "Σ.Α", "a'Σ", "ﬃ ŉ ǰ ΐ", "ПРИВЕТ мир", "ǅǄǆ", "KÅ", "ẞ", "ῼ", "maps/Q2DM1.BSP", "ΌΣΟΣ ΆΣ.", "x́Σ"];
for (const t of texts) {
  out.push(`  LOWER(${cs(t)}, ${cs(t.toLowerCase())});`);
  out.push(`  UPPER(${cs(t)}, ${cs(t.toUpperCase())});`);
}
const pairs = [["PAK0.PK3", "pak0.pk3"], ["İ", "i̇"], ["K", "k"], ["É", "é"], ["ß", "SS"], ["abc", "abd"]];
for (const [a, b] of pairs) out.push(`  CHECK(qa_js_equals_ignore_case(${cs(a)}, ${cs(b)}) == ${a.toLowerCase() === b.toLowerCase()});`);
for (const [a, b] of [["maps/x.K", ".k"], ["FOO.PK3", ".pk3"], ["a", "ab"]]) out.push(`  CHECK(qa_js_ends_with_ignore_case(${cs(a)}, ${cs(b)}) == ${a.toLowerCase().endsWith(b.toLowerCase())});`);
for (const [a, b] of [["HTTP://x", "http://"], ["İx", "i"], ["ab", "abc"]]) out.push(`  CHECK(qa_js_starts_with_ignore_case(${cs(a)}, ${cs(b)}) == ${a.toLowerCase().startsWith(b.toLowerCase())});`);
// Latin-1 byte strings (fixedByteString): bytes are code points 0..255.
const bytes = s => '"' + [...s].map(ch => "\\x" + ch.charCodeAt(0).toString(16).padStart(2, "0")).join("") + '"';
const l1 = ["ABCÀ×Þßàÿ\u0080\u009f", "Q2DM1"];
for (const t of l1) out.push(`  LATIN1(${bytes(t)}, ${bytes(t.toLowerCase())});`);
out.push(`  CHECK(qa_js_latin1_equals_ignore_case("\\xc0X", "\\xe0x") == ${"ÀX".toLowerCase() === "àx".toLowerCase()});`);
out.push(`  CHECK(qa_js_latin1_equals_ignore_case("\\xd7", "\\xf7") == ${"×".toLowerCase() === "÷".toLowerCase()});`);
// localeCompare: fixed cases and a seeded random sweep over an ASCII alphabet.
const alphabet = "aAbBzZ09_- .,!~$#\x01\t" ;
let seed = 1234; const rnd = n => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed % n; };
const words = ["", "a", "A", "b", "B", "ab", "aB", "Ab", "a-b", "a_b", "a b", "a1", "a10", "a9", "1.10", "1.9", "1.2", "01.2", "1.02", "1.2a", "v2.0.10", "v2.0.9", "Zed", "zed", "q2dm1", "Q2DM1", "a\x01", "\x01a", "\t", " ", "~", "$", "é", "e", "f", "007", "7"];
for (let i = 0; i < 300; i++) { let w = ""; const n = rnd(6); for (let k = 0; k < n; k++) w += alphabet[rnd(alphabet.length)]; words.push(w); }
const sign = v => v < 0 ? -1 : v > 0 ? 1 : 0;
for (let i = 0; i + 1 < words.length; i++) {
  const a = words[i], b = words[(i * 7 + 3) % words.length];
  const nonAscii = /[^\x00-\x7f]/.test(a + b);
  if (nonAscii) continue; // documented approximation outside ASCII
  out.push(`  LC(${cs(a)}, ${cs(b)}, ${sign(a.localeCompare(b))}, ${sign(a.localeCompare(b, undefined, { sensitivity: "base" }))}, ${sign(a.localeCompare(b, "en", { numeric: true }))});`);
}
console.log(out.join("\n"));

// The deadline planning block of SharedSimulation.step (runtime.ts, "const fixed = ..."
// through "const boundaries = ..."), copied verbatim over a plain state object.
type Clock = { kind: string; frameMilliseconds?: number; serverFrameMilliseconds?: number };
function plan(s: any, input: { elapsedMilliseconds: number }, paused: boolean) {
  const settlement = s.settlement, profile: Clock = s.profile;
  const fixed = profile.kind === "q2-classic" ? 100 : profile.kind === "q2-rerelease" ? profile.frameMilliseconds! : profile.kind === "q3" ? profile.serverFrameMilliseconds! : null;
  const nowMilliseconds = Math.round(s.timeSeconds * 1000);
  const mapRun = settlement.kind === "active" || !paused && input.elapsedMilliseconds > 0 && (fixed === null || s.sourceSchedulingMilliseconds >= nowMilliseconds + (profile.kind === "q3" ? profile.serverFrameMilliseconds! : 0));
  const elapsed = settlement.kind === "active" ? 0.1 : fixed === null ? Math.min(0.1, Math.max(0.001, input.elapsedMilliseconds / 1000)) : fixed / 1000;
  const selectedQ2 = s.selectedQ2;
  const mapStartMilliseconds = selectedQ2?.mapMilliseconds ?? nowMilliseconds;
  const mapDeadlines = new Set<number>();
  if (settlement.kind === "active") mapDeadlines.add(mapStartMilliseconds);
  else if (mapRun && profile.kind === "q3") {
    for (let due = nowMilliseconds + profile.serverFrameMilliseconds!; due <= s.sourceSchedulingMilliseconds; due += profile.serverFrameMilliseconds!) mapDeadlines.add(due);
  } else if (mapRun && fixed !== null) {
    const maximumQ2CatchupMilliseconds = 200;
    for (let due = mapStartMilliseconds; due <= s.sourceSchedulingMilliseconds && due - mapStartMilliseconds < maximumQ2CatchupMilliseconds; due += fixed) mapDeadlines.add(due + fixed);
  } else if (mapRun) mapDeadlines.add(mapStartMilliseconds + elapsed * 1000);
  const mapEndMilliseconds = mapDeadlines.size === 0 ? mapStartMilliseconds : Math.max(...mapDeadlines);
  const deadlines = new Set<number>(mapDeadlines.size === 0 ? [mapEndMilliseconds] : mapDeadlines);
  const selectedQ3Clock = s.selectedQ3Clock;
  const q3Deadlines = new Set<number>();
  if (!paused && selectedQ3Clock !== null) for (let due = s.selectedQ3Next; due <= s.selectedMilliseconds; due += selectedQ3Clock.serverFrameMilliseconds) { deadlines.add(due); q3Deadlines.add(due); }
  const weaponDeadlines = new Set<number>();
  if (mapRun) {
    if (selectedQ2 !== null) for (let due = selectedQ2.nextMilliseconds; due <= mapEndMilliseconds; due += selectedQ2.intervalMilliseconds) { deadlines.add(due); weaponDeadlines.add(due); }
    if (s.foreignWorld !== null) {
      const interval = s.foreignWorld.edition === "classic" ? 100 : 25;
      for (let due = Math.round(s.foreignWorld.timeSeconds * 1000) + interval; due <= mapEndMilliseconds; due += interval) deadlines.add(due);
    }
  }
  const boundaries = [...deadlines].sort((a, b) => a - b).map(milliseconds => ({ map: mapDeadlines.has(milliseconds), q2: weaponDeadlines.has(milliseconds), q3: q3Deadlines.has(milliseconds), milliseconds }));
  return { fixed, mapRun, elapsed, mapStartMilliseconds, mapEndMilliseconds, boundaries };
}
const none = { kind: "none" };
const cases = [
  { name: "q1 elapsed clamp", s: { settlement: none, profile: { kind: "q1-netquake" }, timeSeconds: 1, sourceSchedulingMilliseconds: 1250, selectedQ2: null, selectedQ3Clock: null, foreignWorld: null }, input: { elapsedMilliseconds: 250 }, paused: false },
  { name: "q2 classic catch-up", s: { settlement: none, profile: { kind: "q2-classic" }, timeSeconds: 0.3, sourceSchedulingMilliseconds: 900, selectedQ2: null, selectedQ3Clock: null, foreignWorld: null }, input: { elapsedMilliseconds: 600 }, paused: false },
  { name: "q2 rerelease due", s: { settlement: none, profile: { kind: "q2-rerelease", frameMilliseconds: 25 }, timeSeconds: 0.05, sourceSchedulingMilliseconds: 110, selectedQ2: null, selectedQ3Clock: null, foreignWorld: { edition: "classic", timeSeconds: 0 } }, input: { elapsedMilliseconds: 60 }, paused: false },
  { name: "q3 server frames + selected q3", s: { settlement: none, profile: { kind: "q3", serverFrameMilliseconds: 50 }, timeSeconds: 0.1, sourceSchedulingMilliseconds: 260, selectedQ2: null, selectedQ3Clock: { serverFrameMilliseconds: 50 }, selectedQ3Next: 125, selectedMilliseconds: 260, foreignWorld: null }, input: { elapsedMilliseconds: 160 }, paused: false },
  { name: "q1 selected q2 weapons", s: { settlement: none, profile: { kind: "q1-netquake" }, timeSeconds: 2, sourceSchedulingMilliseconds: 2100, selectedQ2: { mapMilliseconds: 2000, nextMilliseconds: 2000, intervalMilliseconds: 100 }, selectedQ3Clock: null, foreignWorld: null }, input: { elapsedMilliseconds: 100 }, paused: false },
  { name: "paused", s: { settlement: none, profile: { kind: "q2-classic" }, timeSeconds: 1, sourceSchedulingMilliseconds: 1000, selectedQ2: null, selectedQ3Clock: { serverFrameMilliseconds: 50 }, selectedQ3Next: 900, selectedMilliseconds: 1000, foreignWorld: null }, input: { elapsedMilliseconds: 100 }, paused: true },
  { name: "settlement active", s: { settlement: { kind: "active" }, profile: { kind: "q3", serverFrameMilliseconds: 50 }, timeSeconds: 3, sourceSchedulingMilliseconds: 3000, selectedQ2: null, selectedQ3Clock: null, foreignWorld: null }, input: { elapsedMilliseconds: 100 }, paused: false },
];
for (const c of cases) console.log(JSON.stringify({ name: c.name, ...plan(c.s, c.input, c.paused) }));

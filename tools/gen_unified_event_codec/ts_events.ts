import { createIdentityOwner } from "/home/buzzkill/Projects/quake-anthology-ts/src/contracts/identity.ts";
import { createContentId } from "/home/buzzkill/Projects/quake-anthology-ts/src/contracts/content.ts";
import { EntityState } from "/home/buzzkill/Projects/quake-anthology-ts/src/network/q3/state/entity.ts";
import { encodeUnifiedPresentationEvents, decodeUnifiedPresentationEvents, writeUnifiedSimulationEvent } from "/home/buzzkill/Projects/quake-anthology-ts/src/app/bootstrap/network/unified-event-codec.ts";
import { readSourceQvmPlayerState } from "/home/buzzkill/Projects/quake-anthology-ts/src/compat/qvm/player-record.ts";
const server = createIdentityOwner('event-server');
const actor = server.actor(3, 7);
const content = createContentId({ family: 'q3', edition: 'classic', package: 'baseq3', revision: 'test' });
const common = { sequence: 17, content, seconds: 1.5, sourceEntity: null };
const state = new EntityState(); state.event = 23; state.origin = { x: 1, y: 2, z: 3 }; state.pos = { ...state.pos, delta: { x: 4, y: 5, z: 6 } };
const events: any[] = [
  { ...common, recipient: actor, kind: 'q1', event: { kind: 'message', player: actor, text: 'score', center: true, args: ['name', 17], parts: [{ text: 'part', args: [2] }] } },
  { ...common, kind: 'q2', event: { kind: 'poi', origin: { x: 1, y: 2, z: 3 }, message: 'target', fields: new Map([['image', 'i_help']]) } },
  { ...common, kind: 'q2-player', event: { kind: 'inventory', actor, entries: [{ item: 'q2:cells', count: -2, capacity: 100, countPolicy: { kind: 'source-counter', arithmetic: 'int32' } }], selected: null, visible: false } },
  { ...common, kind: 'q2-rerelease', event: { kind: 'healthbar', actor, slot: 1, target: actor, name: 'boss', fraction: 0.5, visible: true } },
  { ...common, kind: 'q3-character', event: { actor, sequence: 2, timeMilliseconds: 1500, event: 7, parameter: 9 } },
  { ...common, kind: 'q3-source', event: { kind: 'entity-event', actor, state, origin: state.origin, time: 1500 } },
  { ...common, recipient: actor, kind: 'music', event: { kind: 'pause', paused: true } },
];
const a = new TextDecoder().decode(encodeUnifiedPresentationEvents(events));
const view = new DataView(new ArrayBuffer(444)); view.setInt32(128, 66 | 768, true); view.setInt32(140, 12, true); view.setInt32(312 + 8 * 4, 2718, true);
const ps = readSourceQvmPlayerState(view, "q3-1.16n-base");
const pe: any = { kind: "q3-source", content: "q3:classic:component:test", sequence: 4, seconds: 1.5, event: { kind: "player-event", actor, source: { module: { id: "mod:events", artifactPath: "vm/qagame.qvm", revision: "original", digest: "sha256:9751bad99a2d138f96a9b0436d2ea2d965b86214175dc33e4cea95e059419337" }, abiProfile: "q3-1.16n-base" }, playerState: ps, event: 834, parameter: 123, sequence: { kind: "external", time: -1 }, origin: { x: 4, y: 5, z: 6 }, time: 1500 } };
const b = new TextDecoder().decode(encodeUnifiedPresentationEvents([pe]));
const before = { regular: { kind: 'q1', points: 75, absorption: 0.6, item: 'q1:armor/yellow' }, powered: { kind: 'screen', cells: 20 } };
const after = { regular: { ...before.regular }, powered: { kind: 'screen', cells: 15 } };
const damage: any = { sequence: 4, time: { kind: 'seconds', value: 2 }, audience: { kind: 'world' }, payload: { kind: 'damage', outcome: { kind: 'committed', survived: true, decision: { reaction: 'none', appliedDamage: 0, mutations: [{ kind: 'armor', before, after }], request: { target: actor, amount: 5, knockback: 0, direction: state.origin, point: state.origin, normal: state.origin, delivery: 'direct', attack: { sequence: 1, time: { kind: 'seconds', value: 2 }, attacker: actor, inflictor: actor, weapon: 'q3:machinegun', weaponProvider: 'q3:weapons', combatProvider: 'q1:combat', inventoryProvider: 'q2:inventory', movementProvider: 'q1:movement', cause: { kind: 'q3', meansOfDeath: 3, damageFlags: 0 } } } } } } };
const c = JSON.stringify(writeUnifiedSimulationEvent(damage));
const sound = JSON.stringify(writeUnifiedSimulationEvent({ sequence: 3, time: { kind: 'milliseconds', value: 1500 }, audience: { kind: 'client', client: server.client(1, 2) }, payload: { kind: 'sound', resource: `resource:unified:${'a'.repeat(64)}`, actor, origin: state.origin, channel: 1, volume: 0.7, attenuation: 1 } } as any));
const owner = createIdentityOwner('x');
const pass: any = { session: owner.session, actor: (s:number,g:number)=>owner.actor(s,g), client:(s:number,g:number)=>owner.client(s,g), seat:(i:number)=>owner.seat(i), resourceId:(r:any)=>r };
const player_decoded = new TextDecoder().decode(encodeUnifiedPresentationEvents(decodeUnifiedPresentationEvents(new TextEncoder().encode(b), pass)));
console.log(JSON.stringify({ events: a, player: b, damage: c, sound, player_decoded }));

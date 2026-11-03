// Oracle for tests/world/test_gate_world2.c (gate-world2 read of src/world:
// session closeAll, mod-files paths, closed observers, original-pickup scopes).
// Run from this directory: bun gate_world2.ts
import { relative } from "node:path";
const TS = "../../../../quake-anthology-ts/src";
const { EngineSession } = await import(`${TS}/world/session/session.ts`);
const { ModUserFiles } = await import(`${TS}/world/session/mod-files.ts`);
const { ModClientOutputs } = await import(`${TS}/world/session/mod-client-outputs.ts`);
const { ModMatchState } = await import(`${TS}/world/session/mod-match.ts`);
const { SessionMods } = await import(`${TS}/world/session/mods.ts`);
const { GameplayAuthority } = await import(`${TS}/world/gameplay/authority.ts`);
const { SharedOriginalPickupAdmission } = await import(`${TS}/world/gameplay/original-pickups.ts`);
const { SessionActorRegistry } = await import(`${TS}/world/actors/registry.ts`);
const { ActorCallbackTable } = await import(`${TS}/world/actors/callbacks.ts`);
const { createIdentityOwner } = await import(`${TS}/contracts/identity.ts`);

const fail = (f: () => unknown): string => {
  try {
    f();
    return "none";
  } catch (error) {
    return `${(error as Error).name}|${(error as Error).message}`;
  }
};
const failAsync = async (f: () => Promise<unknown>): Promise<string> => {
  try {
    await f();
    return "none";
  } catch (error) {
    return `${(error as Error).name}|${(error as Error).message}`;
  }
};

// 1. session.ts closeAll sites, their order, and repeated closes.
{
  const owner = createIdentityOwner("gate-world2");
  const session = new EngineSession(owner, { kind: "local" });
  const log: string[] = [];
  const sim = (name: string, throws: boolean) => ({
    session: owner.session,
    step: () => ({ snapshot: { session: owner.session }, events: [] }),
    close: () => {
      log.push(name);
      if (throws) throw new Error(`${name} close`);
      return undefined;
    },
  });
  const thrower = (name: string) => () => {
    log.push(name);
    throw new Error(name);
  };
  const client = session.createClient(0);
  const seat = session.createSeat(0, client);
  session.attachWorld(sim("sim1", true));
  client.worldResources.defer(thrower("client-world"));
  const presentation = { state: { seat: seat.id, client: client.id }, receive: () => undefined, frame: () => ({}), render: () => undefined };
  seat.attachPresentation(presentation, thrower("presentation"));
  const replaced = session.replaceWorld(sim("sim2", true));
  console.log(`retired_close ${fail(() => replaced.retired.close())} ${log.join(",")}`);
  log.length = 0;
  console.log(`retired_close_again ${fail(() => replaced.retired.close())} [${log.join(",")}]`);
  client.connect("loopback");
  client.connection?.defer(thrower("connection"));
  client.worldResources.defer(thrower("client-world2"));
  console.log(`disconnect ${fail(() => client.disconnect())} ${log.join(",")}`);
  log.length = 0;
  client.worldResources.defer(thrower("client-world3"));
  console.log(`clear_world ${fail(() => client.clearWorld())} ${log.join(",")}`);
  log.length = 0;
  const detached = session.detachWorld();
  console.log(`detach_close ${fail(() => detached.close())} ${log.join(",")}`);
  log.length = 0;
  console.log(`detach_close_again ${fail(() => detached.close())} [${log.join(",")}]`);
  const client2 = session.createClient(1);
  client2.resources.defer(thrower("client2"));
  console.log(`close_client ${fail(() => session.closeClient(client2.id))} ${log.join(",")}`);
  log.length = 0;
  const client3 = session.createClient(2);
  client3.resources.defer(thrower("client3"));
  const local = session.publishLocalSeats([], { added: [], removed: [client3] }, { added: [], removed: [] });
  console.log(`local_close ${fail(() => local.close())} ${log.join(",")}`);
  log.length = 0;
  console.log(`local_close_again ${fail(() => local.close())} [${log.join(",")}]`);
  const client4 = session.createClient(3);
  client4.resources.defer(thrower("client4"));
  console.log(`session_close ${fail(() => session.close())} ${log.join(",")}`);
  log.length = 0;
  console.log(`session_close_again ${fail(() => session.close())} [${log.join(",")}]`);
}

// 2. mod-files: node path.resolve normalization of the store root.
{
  const selection = { product: "pkg", id: "a:b+c/d" };
  for (const root of ["/tmp/qa/../root/./x//", "/", "rel/../dir/.", ""]) {
    const path = new ModUserFiles(root).for(selection).root;
    console.log(`mod_files ${JSON.stringify(root)} ${root.startsWith("/") ? path : relative(process.cwd(), path)}`);
  }
}

const actors = new SessionActorRegistry(createIdentityOwner("gate-world2 actors"));
const player = actors.allocate("q1:player", "q1:player");
const item = actors.allocate("q1:items", "q1:item");
const authority = new GameplayAuthority(actors, new ActorCallbackTable(actors), {
  impulse: () => undefined,
  beforeReaction: () => undefined,
  confirmed: () => undefined,
});

// 3. A pickup protection observer kept past its operation.
{
  let escaped: { stored(change: object): undefined } | null = null;
  authority.withPickupProtection(player, "mod:x", (observer: { stored(change: object): undefined }) => {
    escaped = observer;
    return 1;
  });
  console.log(`pickup_observer_late ${fail(() => escaped?.stored({}))}`);
}

// 4. Rebinding a copied combat binding: copiedPrimaryArmor sees the new binding.
{
  const state = { health: 50, armor: { regular: { kind: "none" }, powered: { kind: "none" } }, mass: 200, canTakeDamage: true, invulnerable: false, team: null };
  authority.create(item, state);
  const before = authority.copiedPrimaryArmor(item) !== null;
  authority.rebind(item, { read: () => state, writeHealth: () => undefined, writeArmor: () => undefined });
  console.log(`rebind_copied ${before} ${authority.copiedPrimaryArmor(item) !== null}`);
}

// 5. A client output lease retired by close() and a new owner.
{
  const outputs = new ModClientOutputs(() => true);
  const first = outputs.claim("mod:a", ["stance"]);
  outputs.close();
  const second = outputs.claim("mod:b", ["stance"]);
  console.log(`lease_retired_publish ${fail(() => first.publish(player.id, [{ kind: "stance", value: true }]))}`);
  first.close();
  first.close();
  second.publish(player.id, [{ kind: "stance", value: true }]);
  console.log(`lease_new_owner ${JSON.stringify(outputs.read(player.id))}`);
}

// 6. Match objectives: a read that unbinds an earlier objective.
{
  const match = new ModMatchState(actors, () => null);
  const state = { stage: "s", complete: false, carrier: null, target: null };
  const unbindX = match.bindObjective({ owner: "o", id: "x", campaignGate: true, botGoal: false, read: () => state, change: () => undefined });
  match.bindObjective({
    owner: "o",
    id: "y",
    campaignGate: true,
    botGoal: false,
    read: () => {
      unbindX();
      return state;
    },
    change: () => undefined,
  });
  console.log(`match_objectives ${match.objectives().map((entry: { id: string }) => entry.id).join(",")}`);
  console.log(`match_unbind_again ${fail(() => unbindX())} ${match.objectives().map((entry: { id: string }) => entry.id).join(",")}`);
}

// 7. Mods: assertCurrent kept past close; a failed initialization.
{
  const description = (id: string) => ({
    selection: { product: "q3-one", id },
    source: { provider: "q3:official", content: "q3:classic:baseq3:test" },
    title: "stub",
    sourceTitle: "Quake III",
    purpose: "addition",
    requires: [],
    conflicts: [],
    availability: { kind: "available" },
  });
  let kept: (() => void) | null = null;
  const prepared = (id: string, fails: boolean) => {
    const d = description(id);
    return {
      description: d,
      identity: { selection: d.selection, source: d.source, modules: [], providers: [] },
      validateState: () => undefined,
      initialize: async (context: { assertCurrent(): void }) => {
        kept = context.assertCurrent;
        if (fails) throw new Error("init failed");
        return { close: () => undefined, register: () => undefined, checkpoint: async () => ({ guests: [], providers: [] }), restore: async () => undefined };
      },
    };
  };
  const operations = { actors: null, damage: null, inventory: null };
  const mods = await SessionMods.open({ prepared: [prepared("amount", false)], enabled: [{ product: "q3-one", id: "amount" }], operations, nextFrame: async () => undefined });
  console.log(`mods_assert_open ${fail(() => kept?.())}`);
  mods.close();
  console.log(`mods_assert_closed ${fail(() => kept?.())}`);
  console.log(
    `mods_init_failure ${await failAsync(() =>
      SessionMods.open({ prepared: [prepared("broken", true)], enabled: [{ product: "q3-one", id: "broken" }], operations, nextFrame: async () => undefined }),
    )}`,
  );
  console.log(`mods_assert_after_failure ${fail(() => kept?.())}`);
}

// 8. Original pickups: source-effect touches accept and close their scope.
{
  const admission = new SharedOriginalPickupAdmission(actors, authority, null);
  const outcomes: string[] = [];
  for (let i = 0; i < 3; i++)
    outcomes.push(
      admission.touch(
        { recipient: player.id, pickup: item.id, source: "q1:items", item: "q1:shells", count: { kind: "default" }, dropped: false, time: { kind: "seconds", value: 1 }, grant: "source-effect" },
        { original: () => true, complete: () => undefined },
      ),
    );
  console.log(`pickup_touch ${outcomes.join(",")}`);
}

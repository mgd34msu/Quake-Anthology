// Oracle for tests/app/test_remote_seat_source.c: RemoteSeatSource for the q1, qw,
// q2 and q3 families over a fake ApplicationTransport. Each family polls once
// (the connection request), answers the challenge the way a server does, polls
// again (the connect packet carrying userinfo) and closes. Prints every datagram
// the client sends (destination and hex bytes), the network phase after each
// step, and the construction errors the constructor raises.
// Run from this repository: bun tests/app/oracles/remote_seat_source.ts > tests/app/oracles/remote_seat_source.txt
import { RemoteSeatSource } from "../../../../quake-anthology-ts/src/app/bootstrap/remote-seat-source.ts";
import { ApplicationTransport } from "../../../../quake-anthology-ts/src/app/bootstrap/network/transport.ts";
import { PresentationTime } from "../../../../quake-anthology-ts/src/app/bootstrap/frame-clock.ts";
import { createIdentityOwner } from "../../../../quake-anthology-ts/src/contracts/identity.ts";
import { SessionClient, SessionSeat } from "../../../../quake-anthology-ts/src/world/session/session.ts";
import { CvarFlag, CvarRegistry } from "../../../../quake-anthology-ts/src/core/cvars/index.ts";
import { RemoteApplication } from "../../../../quake-anthology-ts/src/app/bootstrap/remote-application.ts";
import { ipv4Address, ipxAddress } from "../../../../quake-anthology-ts/src/network/common/endpoint.ts";

const hex = (bytes: Uint8Array): string => Array.from(bytes, b => b.toString(16).padStart(2, "0")).join("");
const text = (value: string): Uint8Array => {
  const out = new Uint8Array(4 + value.length);
  out.set([255, 255, 255, 255]);
  for (let i = 0; i < value.length; i++) out[4 + i] = value.charCodeAt(i);
  return out;
};
const fmt = (address: { kind: string; host?: readonly number[] | string; port: number }): string =>
  address.kind === "ipv4" ? `${(address.host as readonly number[]).join(".")}:${address.port}` : `${address.kind}:${address.port}`;

function makeSocket(local: ReturnType<typeof ipv4Address>, lines: string[]) {
  const inbox: { from: ReturnType<typeof ipv4Address>; payload: Uint8Array }[] = [];
  let closed = false;
  const socket = {
    address: local,
    get closed() {
      return closed;
    },
    maxDatagramBytes: 1400,
    send(to: ReturnType<typeof ipv4Address>, payload: Uint8Array): boolean {
      lines.push(`  send ${fmt(to)} ${hex(payload)}`);
      return true;
    },
    poll() {
      const next = inbox.shift();
      return next === undefined ? null : { kind: "packet" as const, from: next.from, payload: next.payload, truncated: false, receivedAt: 0 };
    },
    subscribeReadable: () => () => undefined,
    close() {
      closed = true;
    },
    connectSocks: async () => undefined,
  };
  return { socket, inbox };
}

function registry(dialect: "q1-netquake" | "q1-quakeworld" | "q2-classic" | "q3", identity: ReturnType<typeof createIdentityOwner>): CvarRegistry {
  const cvars = new CvarRegistry({ dialect, context: { session: identity.session, origin: { kind: "local-console" } }, print: () => undefined });
  cvars.register("name", "Tester", CvarFlag.UserInfo | CvarFlag.Archive);
  cvars.register("rate", "25000", CvarFlag.UserInfo | CvarFlag.Archive);
  cvars.register("color", "4", CvarFlag.Archive);
  return cvars;
}

async function run(family: "q1" | "qw" | "q2" | "q3", port: number, challenge: string | null): Promise<void> {
  const lines: string[] = [`family ${family}`];
  const identity = createIdentityOwner(`remote-seat-${family}`);
  const client = new SessionClient(identity.client(1, 1)),
    seat = new SessionSeat(identity.seat(1), client);
  const cvars = registry(family === "q2" ? "q2-classic" : family === "q3" ? "q3" : family === "qw" ? "q1-quakeworld" : "q1-netquake", identity);
  const local = ipv4Address([127, 0, 0, 1], 40000);
  const { socket, inbox } = makeSocket(local, lines);
  const transport = new ApplicationTransport({ kind: "udp", socket } as never);
  const server = ipv4Address([127, 0, 0, 1], port);
  const printed: string[] = [];
  const hooks = {
    options: () => ({}) as never,
    mounts: () => {
      throw new Error("mounts");
    },
    content: () => {
      throw new Error("content");
    },
    currentContent: () => null,
    selectContent: () => Promise.reject(new Error("select")),
    refreshContent: () => Promise.reject(new Error("refresh")),
    loadQ1World: () => Promise.reject(new Error("load")),
    loadQ2World: () => Promise.reject(new Error("load")),
    loadQ3World: () => Promise.reject(new Error("load")),
    initializeQ3: () => Promise.resolve(),
    shutdownQ3: () => Promise.resolve(),
    cinematic: { start: () => Promise.resolve(), stop: () => undefined },
    publish: () => undefined,
    disconnected: (reason: string) => lines.push(`  disconnected ${reason}`),
    print: (value: string) => printed.push(value),
    recording: () => false,
  };
  const common = {
    identity,
    session: { session: identity.session } as never,
    seat,
    cvars: () => cvars,
    clock: new PresentationTime(),
    transport,
    address: server,
    qport: 4242,
    downloadPermission: () => false,
    hooks,
  };
  const source =
    family === "q2"
      ? new RemoteSeatSource({ ...common, family, protocol: { kind: "q2-classic", version: 34 } })
      : family === "q3"
        ? new RemoteSeatSource({ ...common, family, authorization: { request: () => Promise.resolve() } })
        : new RemoteSeatSource({ ...common, family });
  lines.push(`  qport ${source.qport} phase ${source.network.phase}`);
  await source.network.poll(1000);
  lines.push(`  poll 1000 phase ${source.network.phase}`);
  if (challenge !== null) {
    inbox.push({ from: server, payload: family === "q1" ? Uint8Array.from(challenge.split(",").map(Number)) : text(challenge) });
    await source.network.poll(1100);
    lines.push(`  poll 1100 phase ${source.network.phase}`);
    await source.network.poll(1200);
    lines.push(`  poll 1200 phase ${source.network.phase}`);
  }
  await source.close();
  lines.push(`  closed phase ${source.network.phase} transport ${transport.closed}`);
  for (const value of printed) lines.push(`  print ${JSON.stringify(value)}`);
  console.log(lines.join("\n"));
}

// RemoteApplication getters over a promoted q2 peer (Object.create on the prototype with the fields the getters read).
async function promoted(): Promise<void> {
  const lines: string[] = ["promoted q2"];
  const identity = createIdentityOwner("remote-seat-promoted");
  const client = new SessionClient(identity.client(2, 1)),
    seat = new SessionSeat(identity.seat(2), client);
  const cvars = registry("q2-classic", identity);
  const { socket } = makeSocket(ipv4Address([127, 0, 0, 1], 40000), lines);
  const transport = new ApplicationTransport({ kind: "udp", socket } as never);
  const hooks = { print: () => undefined, disconnected: () => undefined, publish: () => undefined } as never;
  const source = new RemoteSeatSource({
    identity, session: { session: identity.session } as never, seat, cvars: () => cvars, clock: new PresentationTime(), transport,
    address: ipv4Address([127, 0, 0, 1], 27910), qport: 99, downloadPermission: () => false, hooks, family: "q2",
    protocol: { kind: "q2-classic", version: 34 },
  });
  const peer = { seat, source, closed: false, map: null as string | null, loadedContent: null, view: null, disconnected: null };
  const app = Object.create(RemoteApplication.prototype) as Record<string, unknown>;
  Object.assign(app, {
    primaryPeer: peer, promotedSource: null, initialRemote: null, initialSource: { kind: "recorded" }, remoteSeats: [peer],
    launchOptions: { map: "e1m1" }, ownership: { kind: "owned" }, closed: false, closing: false, controls: null, qwDownloads: null, q3Downloads: null, loadedContent: null, initialSeatId: identity.seat(0),
  });
  const get = (name: string): unknown => Reflect.get(RemoteApplication.prototype, name, app);
  lines.push(`  networkPhase ${get("networkPhase")}`);
  lines.push(`  seatId ${(get("seatId") as { index: number }).index}`);
  const address = get("networkAddress") as { host: number[]; port: number };
  lines.push(`  networkAddress ${address.host.join(".")}:${address.port}`);
  lines.push(`  localPlayers ${(get("localPlayers") as unknown[]).length}`);
  lines.push(`  downloadProgress ${(get("downloadProgress") as unknown[]).length}`);
  lines.push(`  sameSource ${get("source") === get("source")} network ${(get("network") as unknown) === source.network}`);
  try {
    (app.sourceForActor as (actor: unknown) => unknown).call(app, identity.actor(5, 1));
    lines.push("  sourceForActor none");
  } catch (error) {
    lines.push(`  sourceForActor ${(error as Error).message}`);
  }
  const current = (): boolean => (app.peerWorldCurrent as (peer: unknown) => boolean).call(app, peer);
  lines.push(`  peerWorldCurrent ${current()}`);
  peer.map = "e1m1";
  lines.push(`  peerWorldCurrent ${current()}`);
  try {
    (app.sendCommand as (text: string) => void).call(app, "say hi");
    lines.push("  sendCommand none");
  } catch (error) {
    lines.push(`  sendCommand ${(error as Error).message}`);
  }
  const owns = (): boolean => (app.ownsPublishedSource as () => boolean).call(app);
  lines.push(`  ownsPublishedSource ${owns()}`);
  app.closing = true;
  lines.push(`  ownsPublishedSource closing ${owns()}`);
  app.closing = false;
  peer.closed = true;
  lines.push(`  ownsPublishedSource peer-closed ${owns()}`);
  peer.closed = false;
  (app.cancelDownloads as (seat: unknown) => void).call(app, null);
  await (app.retryDownloads as (seat: unknown) => Promise<void>).call(app, null);
  await source.close();
  console.log(lines.join("\n"));
}

async function errors(): Promise<void> {
  const identity = createIdentityOwner("remote-seat-errors");
  const client = new SessionClient(identity.client(1, 1)),
    seat = new SessionSeat(identity.seat(1), client);
  const cvars = registry("q1-netquake", identity);
  const { socket } = makeSocket(ipv4Address([127, 0, 0, 1], 40000), []);
  const transport = new ApplicationTransport({ kind: "udp", socket } as never);
  const base = { identity, session: { session: identity.session } as never, seat, cvars: () => cvars, clock: new PresentationTime(), transport, qport: 7, hooks: {} as never };
  const tries: [string, () => unknown][] = [
    ["qw-ipx", () => new RemoteSeatSource({ ...base, address: ipxAddress(1, [1, 2, 3, 4, 5, 6], 27500), downloadPermission: null, family: "qw" })],
    ["q3-ipv6", () => new RemoteSeatSource({ ...base, address: { kind: "ipv6", host: "::1", port: 27960 } as never, downloadPermission: null, family: "q3", authorization: { request: () => Promise.resolve() } })],
    ["q2-null-permission", () => new RemoteSeatSource({ ...base, address: ipv4Address([127, 0, 0, 1], 27910), downloadPermission: null, family: "q2", protocol: { kind: "q2-classic", version: 34 } })],
  ];
  for (const [name, attempt] of tries) {
    try {
      attempt();
      console.log(`error ${name} none`);
    } catch (error) {
      console.log(`error ${name} ${(error as Error).message}`);
    }
  }
}

await errors();
await run("q1", 26000, "128,0,0,9,129,145,101,0,0");
await run("qw", 27500, "c1234567");
await run("q2", 27910, "challenge 1234567");
await run("q3", 27960, "challengeResponse 1234567");
await promoted();
process.exit(0);

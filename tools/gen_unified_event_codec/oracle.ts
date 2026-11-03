import { createIdentityOwner } from "/home/buzzkill/Projects/quake-anthology-ts/src/contracts/identity.ts";
import { encodeUnifiedPresentationEvents, decodeUnifiedPresentationEvents, writeUnifiedSimulationEvent, readUnifiedSimulationEvent } from "/home/buzzkill/Projects/quake-anthology-ts/src/app/bootstrap/network/unified-event-codec.ts";
const c = createIdentityOwner("client");
const identity = { session: c.session, actor: (s:number,g:number)=>c.actor(s+10,g+1), client:(s:number,g:number)=>c.client(s+20,g+2), seat:(i:number)=>c.seat(i+1), resourceId:()=>("resource:unified:"+"b".repeat(64)) as any };
const out: string[] = [];
const text = await Bun.file(process.argv[2]!).text();
for (const line of text.split("\n")) {
  if (!line) continue;
  const row = JSON.parse(line);
  try {
    if (row.root === "presentation") {
      const bytes = new TextEncoder().encode(JSON.stringify([row.event]));
      const d = decodeUnifiedPresentationEvents(bytes, identity as any);
      out.push("OK " + new TextDecoder().decode(encodeUnifiedPresentationEvents(d)));
    } else {
      const d = readUnifiedSimulationEvent(row.event, identity as any);
      out.push("OK " + JSON.stringify(writeUnifiedSimulationEvent(d)));
    }
  } catch (e:any) { out.push("ERR " + String(e.message)); }
}
await Bun.write(process.argv[3]!, out.join("\n")+"\n");

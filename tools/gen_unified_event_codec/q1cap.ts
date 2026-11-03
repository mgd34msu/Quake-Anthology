const R="/home/buzzkill/Projects/quake-anthology-ts/src/network/q1/";
const m = await import(R+"index.ts");
const { SizeBuf, NetQuakeDecoder, writeNetQuakeMessage, writeNetQuakeEntity, writeNetQuakeMove, decodeNetQuakeClient, encodeNetQuakeControl, decodeNetQuakeControl, answerNetQuakeControl, QuakeWorldChallenges, QuakeWorldConnectionlessServer, QuakeWorldConnectClient, NetQuakeConnectClient, QuakeWorldMasterHeartbeat, quakeWorldHeartbeat, quakeWorldShutdown, quakeWorldOutOfBand, readQuakeWorldOutOfBand, quakeWorldCommandArguments, quakeWorldInfo, writeQuakeWorldMove, decodeQuakeWorldClient, writeNetQuakeClientData } = m;
const hex=(b:Uint8Array)=>Buffer.from(b).toString('hex');
const v=(x:number,y:number,z:number)=>({x,y,z});
const protos:any[]=[{kind:'q1-netquake',version:15},{kind:'q1-fitzquake',version:666},{kind:'q1-rmq',version:999,flags:0},{kind:'q1-rmq',version:999,flags:2|16|64|128}];
const cd={viewHeight:22,idealPitch:-10,punchAngles:v(1,-2,3),velocity:v(320,-640,48),items:0x1234567,onGround:true,inWater:false,weaponFrame:300,armor:300,weaponModel:300,health:-20,ammo:300,shells:25,nails:30,rockets:5,cells:6,activeWeapon:32};
const st=(n:number)=>({number:n,origin:v(100.5,-32,8),angles:v(10,20,30),modelIndex:3,frame:4,colorMap:1,skin:2,effects:8,alpha:0,scale:16,lerpFinishSeconds:0});
const msgs:any[]=[
 ...['nop','disconnect','killed-monster','found-secret','intermission','sell-screen','bonus-flash','level-completed','back-to-level'].map(kind=>({kind})),
 ...['print','center-print','stufftext','finale','cutscene','skybox','achievement','botchat','raw-print','chat','server-var'].map(kind=>({kind,text:'hello "w" \\ '+kind})),
 {kind:'time',seconds:12.5},{kind:'version',version:15},{kind:'stat',index:3,value:-77},{kind:'set-view',entity:12},{kind:'set-angle',angles:v(11.25,-90,180)},
 {kind:'light-style',index:3,value:'abcz'},{kind:'name',slot:1,value:'player'},{kind:'social',slot:1,value:'s'},{kind:'player-info',slot:1,value:'pi'},
 {kind:'frags',slot:2,value:-5},{kind:'colors',slot:2,value:17},{kind:'ping',slot:2,value:50},
 {kind:'client-data',data:cd,weaponAlpha:0},
 {kind:'client-data',data:{...cd,viewHeight:0,idealPitch:0,punchAngles:v(0,0,0),velocity:v(0,0,0),weaponFrame:0,armor:0,weaponModel:0,ammo:0,onGround:false,inWater:true},weaponAlpha:0.5},
 {kind:'baseline',state:st(5)},{kind:'baseline',state:{...st(300),modelIndex:300,frame:300}},{kind:'static',state:st(6)},{kind:'static',state:{...st(7),modelIndex:300,alpha:128}},
 {kind:'sound',entity:7,channel:3,index:2,volume:255,attenuation:1,origin:v(1,2,3)},{kind:'sound',entity:7,channel:3,index:2,volume:128,attenuation:2,origin:v(1,2,3)},{kind:'sound',entity:9000,channel:9,index:300,volume:255,attenuation:1,origin:v(1,2,3)},
 {kind:'static-sound',entity:0,channel:0,index:2,volume:128,attenuation:1,origin:v(4,5,6)},{kind:'stop-sound',entity:7,channel:3},{kind:'local-sound',index:3},
 {kind:'damage',armor:3,blood:4,source:v(1,2,3)},{kind:'particle',origin:v(1,2,3),direction:v(0.5,-0.5,1),count:255,color:7},{kind:'particle',origin:v(1,2,3),direction:v(0.5,-0.5,1),count:30,color:7},
 ...[0,1,2,3,4,5,6,7,8,9,10,11,13].map((type,i)=>({kind:'temporary-entity',effect:(type===5||type===6||type===9||type===13)?{kind:'beam',type,entity:4,start:v(1,2,3),end:v(4,5,6)}:{kind:'point',type,origin:v(1+i,2,3)}})),
 {kind:'temporary-entity',effect:{kind:'explosion-colors',type:12,origin:v(1,2,3),colorStart:4,colorLength:5}},
 {kind:'pause',paused:true},{kind:'pause',paused:false},{kind:'signon',stage:2},{kind:'cd-track',track:4,loopTrack:5},
];
const rows:any[]=[];
for(const p of protos) for(const msg of msgs) for (const standard of [true,false]) for (const mode of ['known-retail','q1-rerelease-private']) {
  try{ const s=new SizeBuf(65535); writeNetQuakeMessage(s,p,msg,mode,standard); rows.push({p,standard,mode,kind:msg.kind,bytes:hex(s.bytes())}); }
  catch(e:any){ rows.push({p,standard,mode,err:String(e.message),kind:msg.kind}); }
}
// server-info rows
for(const p of protos){ const s=new SizeBuf(65535); writeNetQuakeMessage(s,p,{kind:'server-info',protocol:p,maxClients:8,gameType:1,level:'start level',models:['maps/start.bsp','progs/player.mdl'],sounds:['misc/a.wav']}); rows.push({p,standard:true,mode:'known-retail',kind:'server-info',bytes:hex(s.bytes())}); }
for (const r of rows) { if (r.err) continue;
  try { const d=new NetQuakeDecoder(r.p,r.mode,r.standard); const decoded=d.decode(Uint8Array.from(Buffer.from(r.bytes,'hex'))); const s=new SizeBuf(65535);
    for (const x of decoded) writeNetQuakeMessage(s,r.p,x,r.mode,r.standard); r.re=hex(s.bytes()); }
  catch(e:any){ r.decodeErr=String(e.message); } }
const moves:any[]=[];
for(const p of protos){ const s=new SizeBuf(65535); writeNetQuakeMove(s,{kind:'q1-netquake',acknowledgedServerTimeSeconds:3.5,viewAngles:v(11.25,-90,45),forwardMove:200,sideMove:-50,upMove:7,buttons:3,impulse:9},p); moves.push({p,bytes:hex(s.bytes())}); }
const ctrl:any[]=[{kind:'connect-request',game:'QUAKE',version:3},{kind:'server-info-request',game:'QUAKE',version:3},{kind:'player-info-request',player:2},{kind:'rule-info-request',previous:'sv_gravity'},{kind:'accept',port:26001},{kind:'reject',reason:'full\n'},{kind:'server-info',address:'1.2.3.4:26000',name:'srv',map:'e1m1',players:2,maxPlayers:8,version:3},{kind:'player-info',player:1,name:'bob',colors:17,frags:-3,seconds:99,address:'1.2.3.4'},{kind:'rule-info',rule:{name:'a',value:'b'}},{kind:'rule-info',rule:null}].map(x=>hex(encodeNetQuakeControl(x)));
const addr={host:'10.0.0.1',port:27001};
const seq=[100,200,300,400,0x12345,0x7fff];let si=0;
const c=new QuakeWorldChallenges(()=>seq[si++%seq.length]!);
const srvHost={password:'',spectatorPassword:'',rconPassword:'pw',highCharacters:true,blocked:()=>false,connect:(r:any)=>{conn.push({qport:r.qport,challenge:r.challenge,userinfo:r.userinfo,spectator:r.spectator,donorWide:r.donorWide});return {kind:'accepted'};},status:()=>'stat\n',log:(q:number)=>q<0?null:'L'+q,executeAdmin:(cmd:string,w:(t:string)=>void)=>{w('ran:'+cmd);}};
const conn:any[]=[];
const srv=new QuakeWorldConnectionlessServer(srvHost as any,c);
const srvReq=['ping\n','k','getchallenge\n','status\n','log 5\n','log\n','rcon wrong status\n','rcon pw status now\n','connect 27 1 2 "\\\\name\\\\a"\n','connect 28 99 1 "\\\\name\\\\a"\n','bogus\n'];
const out:any[]=[];
for(const t of srvReq){const rep=await srv.receive(quakeWorldOutOfBand(t),addr,1000);out.push({t,rep:rep.map((x:Uint8Array)=>hex(x))});}
const ch=c.issue(addr,1000);
const conReq=[`connect 28 2000 ${ch} "\\\\name\\\\a\\\\password\\\\p\\\\*wide\\\\1"\n`,`connect 28 70000 ${ch} "\\\\name\\\\a"\n`,`connect 28 x ${ch} "\\\\name\\\\a"\n`,`connect 28 2000 ${ch} "\\\\spectator\\\\sp\\\\name\\\\b"\n`];
for(const t of conReq){const rep=await srv.receive(quakeWorldOutOfBand(t),addr,1000);out.push({t,rep:rep.map((x:Uint8Array)=>hex(x))});}
const cl=new QuakeWorldConnectClient(1234,'\\name\\x');
const flow:string[]=[hex(cl.next(0)!)];cl.receive(quakeWorldOutOfBand('c'+ch));flow.push(hex(cl.next(1)!));flow.push(cl.next(2)===null?'null':'bad');
const hb=new QuakeWorldMasterHeartbeat();const beats=[hb.next(0,3),hb.next(1000,4),hb.next(300000,5),hb.next(300001,6,true)].map(x=>x===null?'null':hex(x));
const big=hex(quakeWorldHeartbeat(1234567,3));
const nq=new NetQuakeConnectClient();const nqf:string[]=[];for(const t of [0,100,2500,5000,7500,10000]){const b=nq.next(t);nqf.push(b===null?'null':hex(b));}
console.log(JSON.stringify({rows,moves,ctrl,out,conn,ch,flow,beats,big,nqf,nqstate:nq.state}));

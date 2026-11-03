const R="/home/buzzkill/Projects/quake-anthology-ts/src/network/q2/";
const m=await import(R+"index.ts");
const {Q2WireCodec,EntityStateT,PlayerStateT,encodeQ2Frame}=m;
const hex=(b:Uint8Array)=>Buffer.from(b).toString('hex');
const protocols:any[]=[{kind:'q2-classic',version:34},{kind:'q2-r1q2',version:35,revision:1905},{kind:'q2-q2pro',version:36,revision:1019},{kind:'q2-rerelease',version:1038},{kind:'q2-private-classic',version:4038},{kind:'q2-kex',version:2023},{kind:'q2-kex-demo',version:2022}];
const out:any[]=[];
const pf=(player:any,entities:any[],serverFrame=1)=>({serverFrame,deltaFrame:-1,suppressedCount:0,areaBits:new Uint8Array([7,0]),player,entities});
const richP=()=>{const p=new PlayerStateT();p.pmove.pm_type=2;p.pmove.origin.set([1600,-800,2400]);p.pmove.velocity.set([40,-50,60]);p.pmove.originF.set([200,-100,300]);p.pmove.velocityF.set([5,-6,7]);p.pmove.pm_flags=3;p.pmove.pm_time=9;p.pmove.gravity=800;p.pmove.delta_angles.set([100,-200,300]);p.pmove.viewheight=22;
 p.viewangles.set([10,20,30]);p.viewoffset.set([1,2,22]);p.kick_angles.set([1.5,-1.5,2]);p.gunangles.set([0.5,1,-1]);p.gunoffset.set([1,-1,0.5]);p.gunindex=7;p.gunskin=2;p.gunframe=12;p.gunrate=20;p.blend.set([0.5,0.25,0.125,0.75]);p.damage_blend.set([0.25,0.5,0.125,0.5]);p.fov=100;p.rdflags=4;p.team_id=1;p.stats[0]=5;p.stats[2]=100;p.stats[31]=-9;return p;};
const richE=(n:number)=>{const e=new EntityStateT();e.number=n;e.origin.set([320.5,-160.25,8]);e.angles.set([11,22,33]);e.old_origin.set([300,-150,4]);e.modelindex=5;e.modelindex2=6;e.modelindex3=7;e.modelindex4=8;e.frame=300;e.skinnum=2;e.effects=0x10000;e.renderfx=64;e.solid=31;e.sound=4;e.event=2;e.alpha=0.5;e.scale=1.5;return e;};
for(const protocol of protocols){
  const rows:any={protocol};
  try{
    const wire=new Q2WireCodec(protocol);
    const player=new PlayerStateT();player.pmove.origin.set([16,-8,24]);player.pmove.originF.set([2,-1,3]);player.fov=90;player.stats[2]=100;
    const entity=new EntityStateT();entity.number=42;entity.origin.set([32,-16,8]);entity.modelindex=5;entity.event=3;entity.solid=31;
    const first=pf(player,[entity]);rows.first=hex(encodeQ2Frame(wire,first,null,new Map(),4));
    const ne=new EntityStateT();ne.number=42;ne.origin.set(entity.origin);ne.modelindex=5;ne.solid=31;
    const next=pf(player,[ne],2);rows.next=hex(encodeQ2Frame(wire,next,first,new Map(),4));
    const empty=pf(player,[],3);rows.empty=hex(encodeQ2Frame(wire,empty,next,new Map(),4));
    const rp=richP();const r1=pf(rp,[richE(42),richE(300)],4);rows.rich=hex(encodeQ2Frame(wire,r1,null,new Map(),4));
    const rp2=richP();rp2.pmove.origin.set([1700,-800,2400]);rp2.pmove.originF.set([212.5,-100,300]);rp2.stats[2]=50;rp2.viewangles.set([11,20,30]);rp2.fov=90;rp2.gunframe=13;
    const e2=richE(42);e2.origin.set([321,-160.25,9]);e2.frame=301;e2.event=0;
    const r2=pf(rp2,[e2],5);rows.rich2=hex(encodeQ2Frame(wire,r2,r1,new Map(),4));
  }catch(e:any){rows.err=String(e.message);}
  out.push(rows);
}
console.log(JSON.stringify(out));

// Oracle for tests/render/test_gate_render2.c (gate-render2 full read of src/render).
// Run from this directory: bun gate_render2.ts
const TS = "../../../../quake-anthology-ts/src";
const { packSourceDrawSort } = await import(`${TS}/render/scene/source-sort.ts`);
const { floodSkin } = await import(`${TS}/render/scene/skin.ts`);
const { portalSurfaceOffscreen } = await import(`${TS}/render/scene/portal.ts`);
const { farClip, perspectiveProjection } = await import(`${TS}/render/scene/view.ts`);
const { geometryBounds } = await import(`${TS}/render/scene/geometry.ts`);
const { SourceDebugGraph } = await import(`${TS}/render/debug-graph.ts`);
const { imagePolicyFromControls } = await import(`${TS}/render/scene/image-policy.ts`);
const { SceneImageRegistry } = await import(`${TS}/render/scene/resources.ts`);
const { SceneTextureLoader } = await import(`${TS}/render/scene/textures.ts`);
const { shadowConeFov, packShadowAtlas } = await import(`${TS}/render/scene/shadows.ts`);

const out: string[] = [];
const emit = (label: string, value: unknown): void => {
  out.push(`${label} ${typeof value === "number" ? String(value) : JSON.stringify(value)}`);
};

// source-sort: int32 shifts wrap before >>> 0
emit("pack_wrap", packSourceDrawSort(20000, 1022, 31, 3));
emit("pack_negative", packSourceDrawSort(-1, 0, 0, 0));

// skin: a palette shorter than 768 bytes has no black entry past its end
{
  const palette = { colors: new Uint8Array([10, 10, 10, 20, 20, 20]) };
  const result = floodSkin(new Uint8Array([5, 5, 7, 5]), 2, 2, palette);
  emit("flood_short_palette", [...result]);
}

// portal: a NaN distance keeps the surface on screen (Math.min keeps NaN)
{
  const camera = {
    origin: { x: 0, y: 0, z: 0 },
    axis: [
      { x: 1, y: 0, z: 0 },
      { x: 0, y: 1, z: 0 },
      { x: 0, y: 0, z: 1 },
    ],
    projection: perspectiveProjection(90, 90, 4096),
    viewport: { x: 0, y: 0, width: 64, height: 64 },
    clip: { kind: "none" },
  };
  const vertex = (x: number, y: number, z: number) => ({
    position: { x, y, z },
    normal: { x: 1, y: 0, z: 0 },
    texCoord: { x: 0, y: 0 },
    lightmapCoord: { x: 0, y: 0 },
    color: { x: 255, y: 255, z: 255, w: 255 },
  });
  const mesh = { vertices: [vertex(NaN, 0, 0), vertex(50, 1, 0), vertex(50, 0, 1)], indices: [0, 1, 2] };
  emit("portal_nan", portalSurfaceOffscreen(mesh, camera, 10, false));
}

// view/geometry: Math.max/Math.min keep NaN
emit("far_clip_nan", farClip({ x: 0, y: 0, z: 0 }, { min: { x: NaN, y: 0, z: 0 }, max: { x: 1, y: 1, z: 1 } }));
{
  const b = geometryBounds([{ position: { x: 1, y: 2, z: 3 } }, { position: { x: NaN, y: 5, z: -1 } }]);
  emit("bounds_nan", [b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z].map(String));
}

// debug graph: Math.min(30, NaN) stores NaN
{
  const graph = new SourceDebugGraph();
  const settings = { debuggraph: 0, timegraph: 0, netgraph: 1, height: 32, scale: 1, shift: 0 };
  graph.addNetwork({ dropped: 0, suppressed: 0, pingMilliseconds: NaN }, settings);
  const bars = graph.bars({ x: 0, y: 0, width: 2, height: 40 }, settings);
  emit("netgraph_nan", bars.map(bar => [bar.rect.x, bar.rect.y, bar.rect.width, bar.rect.height, bar.color].map(String)));
}

// image policy: trim() strips form feed / vertical tab
emit("policy_ff_source", imagePolicyFromControls({ overrideLevel: 1, overrideMask: 4, formats: "\f source \v" }).formats === undefined);

// texture loader: an undecodable candidate is skipped (non-strict) or rethrown (strict)
{
  const tga = new Uint8Array([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 32, 8, 1, 2, 3, 255, 4, 5, 6, 255]);
  const reader = {
    async read(path: string) {
      if (path === "pics/x.jpg") return { bytes: new Uint8Array([1, 2, 3, 4]), source: { kind: "generated", name: path } };
      if (path === "pics/x.tga") return { bytes: tga, source: { kind: "generated", name: path } };
      return null;
    },
  };
  const owner = { session: { token: 1 }, identity: {}, generation: 0 };
  const loader = new SceneTextureLoader(new SceneImageRegistry(owner as never, { sample: () => 0 }), reader as never);
  const texture = await loader.load("pics/x.jpg", { mipmap: false });
  emit("skip_undecodable", texture === null ? null : [texture.name, texture.width, texture.height, texture.content.levels.length]);
  const strict = new SceneTextureLoader(new SceneImageRegistry(owner as never, { sample: () => 0 }), reader as never, null, { strict: true });
  let thrown = "none";
  try {
    await strict.load("pics/x.jpg", { mipmap: false });
  } catch (error) {
    thrown = (error as Error).name;
  }
  emit("strict_rethrows", thrown);
}

// shadows: Math.min/Math.max keep NaN
emit("cone_fov_nan", shadowConeFov(NaN));
emit("pack_nan_shelf", packShadowAtlas([{ width: 1500, height: NaN }, { width: 1000, height: 10 }]).map(r => (r === null ? null : [r.x, r.y, r.width, r.height].map(String))));

console.log(out.join("\n"));

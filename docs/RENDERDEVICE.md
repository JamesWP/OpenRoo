# RenderDevice: the contract, and porting it

`src/render/renderdevice.h` is the only way game code reaches the rendering
backend; `src/d3d/` is the only backend (Direct3D 9, fixed function). This file
says what the interface promises, so that a second backend -- OpenGL 3.3 core,
WebGL 2 -- can be written against it without touching game code, and where the
interface is deliberately still short of that.

## What the interface looks like

| Area | Calls | A shader backend turns it into |
|---|---|---|
| Pipeline state | `SetBlend` `SetDepth` `SetStencil` `SetRaster` `SetFog` (whole value types from `rendertypes.h`) | a pipeline / state object, keyed by the value |
| Sampling | `SetSampler(stage, SamplerState)`, `SetSamplerAddress`, `SetTexture` | sampler objects, texture bindings |
| Lighting | `SetAmbientLight` `SetMaterial` `SetDirectionalLight` `SetSpecular`, `DrawFlag::NoLight` | one uniform block; a lit and an unlit shader variant |
| Transforms | `SetWorld` `SetView` `SetProjection` | per-draw / per-frame uniforms |
| Geometry | `CreateVertexBuffer` `UpdateVertexBuffer` `DrawBuffer`; `Draw` for one-shot vertices | VBOs; a streaming ring for `Draw` |
| Frame | `Clear` `BeginFrame` `EndFrame` `Present` `PresentImage` | clear + swap |

Rules the game keeps, which a backend may rely on:

* **Nothing is read back from the device.** The getters (`blend()`, `view()`,
  ...) answer from `PipelineState`, the backend-independent shadow
  (`RenderDevice::state()`). State is write-only as far as the GPU is concerned.
* **State is sticky and whole.** Each setter replaces its entire value and it
  stays until set again. Parts of a value that cannot matter -- the blend
  factors while blending is off, the stencil ops while the test is off -- are
  not guaranteed to reach the device (the Direct3D backend skips them), so a
  backend must not depend on them.
* **Vertex layouts are a closed set** (`VertexFormat`): five, each a plain
  struct in `rendertypes.h` or a game type with the same layout. `Lit` carries
  an unused reserved word where Direct3D has a point size; the backend drops it.
* **Dynamic buffers are rewritten whole frames at a time.** `UpdateVertexBuffer`
  is only legal on `BufferUsage::Dynamic` buffers; static ones are written at
  creation.
* **Draws** use triangle lists, strips, fans, lines and points. Fans are
  deprecated in Vulkan/D3D12 and need converting to lists in a core-profile
  backend (every fan in the game is a 4-vertex quad).
* **The projection matrix is the game's original Direct3D 6 one**, with the clip
  volume's y axis at +-aspect; the backend rescales (see `adjust_projection`).
  Row-major, row vectors (`v * M`), left-handed: a GL backend transposes on
  upload and remaps depth to [-1, 1] if it needs to.
* **Theme files name blend factors and address modes by keyword**
  (`srcalpha`, `clamp`); the parser maps them to the enums once
  (`ui/theme.cpp`, `blendFromTheme`). The numbers in `rendertypes.h` are
  Direct3D's by accident of history and nothing depends on them. `BlendFactor::
  BothInvSrcAlpha` is the one factor that is really a pair; a GL backend sets
  source `ONE_MINUS_SRC_ALPHA` and destination `SRC_ALPHA`.

## Mapping notes for GL / WebGL 2

* **Fog** is per-pixel `FogState` (linear, exp, exp2, a colour): a fragment
  shader and four uniforms. It applies when `enable` is set.
* **Lighting** is one directional light, an ambient colour and one material;
  `NoLight` draws skip it, and only formats with normals are lit at all.
  Specular is a flag (`SetSpecular`) plus the vertex specular colour of `Lit`
  and `Screen` vertices.
* **Texture combine** is Direct3D 6's modulate: texture x vertex colour, alpha
  from the texture if its format has any, else from the vertex colour; with no
  texture bound, vertex colour only. That is the whole fixed-function texture
  pipeline the game uses.
* **Screen** vertices are pre-transformed (`XYZRHW`): the shader maps pixels to
  clip space from the back buffer size and uses `rhw` as 1/w.
* **Devices can be lost** in Direct3D 9 only; the backend's `BeginFrame`
  absorbs it. GL/WebGL context loss, if wanted, is the same: `BeginFrame`
  returns false until the resources are back.
* **Textures** come from `Image`s through `CreateTexture`; the format choice and
  conversion are the backend's.

## Not done yet

* **Window and display modes.** `Create(void *hWnd, adapter, modeIndex)` and
  `EnumerateDisplayModes` are shaped by exclusive full-screen Direct3D. A GL or
  browser backend wants a surface + size + vsync/fullscreen preference, with no
  mode list; do this together with the backend.
* **Render targets** are not in the interface. Nothing needs them today;
  post-processing and render-to-texture shadows would.
* **Instancing and per-draw uniforms beyond the world matrix.** See below.

## GPU particles: where the interface stands

Particles are simulated on the CPU (`particles.cpp`: generators emit into a
ring, environments age and move them) and the three classes are filled into a
`ParticleVertex` array that is uploaded to a dynamic buffer each frame
(`ParticleSystem::uploadVerts`). That upload is the point to replace.

What a GPU-side particle system needs from the device, in order of how far the
interface is from it:

1. **Dynamic and static vertex buffers** -- present.
2. **A draw with a per-draw uniform set** (emitter state, time) -- the lighting,
   fog and transform uniforms are there; particle uniforms are not. A
   `ParticleParams`-style struct set per draw is the smallest addition.
3. **Instancing**: one four-corner quad (or one point) drawn `N` times with the
   per-particle data in a second, per-instance stream. WebGL 2 has instanced
   arrays; D3D9 has hardware instancing via stream frequency, so the Direct3D
   backend can serve it too. Add `DrawBufferInstanced(prim, quadVb, instanceVb,
   count)` when the first consumer exists.
4. **A stateless simulation.** WebGL 2 has no compute and transform feedback is
   awkward to port; the portable shape is a vertex shader that evaluates a
   particle in closed form from `(spawn time, spawn position, velocity, seed)`
   and the current time. That works for the gravity / drag / fade
   environments the game's `.par` files use, but the *generators* are
   stateful (ring allocation, the Gaussian speed table), so emission stays on
   the CPU and only writes the small per-particle spawn record into the
   instance buffer. Retirement is then free: a particle past its lifetime
   collapses to zero size.

Doing (4) before a shader backend exists cannot be tested (Direct3D 9 fixed
function cannot run it), so it should come with the first shader backend; (1)-(3)
are the interface work this PR leaves ready for it.

## Checking a render-API change

Replays assert game state, not pixels, so a change to the rendering code has its
own check: `tools/drawtrace.py` (see `docs/TESTING.md`). It records, per draw,
a hash of the vertex bytes and of the state the Direct3D device itself reports,
and diffs two runs. Two builds that draw the same pictures leave identical
traces however differently they get there.

Two caveats from using it on this interface change. Run the "before" build twice:
`enemyfactory` differs by a handful of draws between two runs of one build, and
`bombstart-crash` came out differently the first time a fresh checkout ran it
than on every later run (36121 draws, the same ones each time), so a single
baseline can mislead. And build the "before" side in a clean worktree, because
`launch.sh` rebuilds whatever tree it is in.

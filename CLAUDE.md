# WAIFU_GODOT — engine fork for Project_WAIFU

Fork of Godot tracking `4.7.1-stable`. `origin` = `Th3D0c0/WAIFU_GODOT`,
`upstream` = `godotengine/godot`.

Exists to host custom C++ modules for **Project_WAIFU** (`D:\Godot\Projects\Project_WAIFU`),
a VR game where performance is the number one constraint. Target: PCVR now, standalone
Quest 3 / Android later — so modules must stay mobile-safe.

## Build

Toolchain on this machine: **MSVC (`cl`)**, Python 3.9.9, SCons 4.9.1, **32 cores**.
No ccache/sccache installed — full rebuilds are expensive, incremental builds are the norm.

```sh
# Editor build (what bin/godot.windows.editor.x86_64.exe currently is)
scons platform=windows target=editor -j32

# Module development: debug symbols + unit tests compiled in
scons platform=windows target=editor dev_build=yes tests=yes -j32

# Faster full rebuild (single compilation unit); slower incremental, use for clean builds
scons platform=windows target=editor scu_build=yes -j32

# Export templates for shipping
scons platform=windows target=template_release -j32
```

**Builds are long — always run them with `run_in_background` and keep working**, then read the
result. Never block on a compile.

### Build from PowerShell, never from Git Bash

Despite the `sh` above, run SCons from **PowerShell**. Git Bash sets `MSYSTEM=MINGW64`, and
`platform/windows/detect.py:207-220` reads that as "we are cross-compiling from MSYS2":

```python
deps_folder = os.getenv("LOCALAPPDATA")
if deps_folder and not os.getenv("MSYSTEM"):
    deps_folder = os.path.join(deps_folder, "Godot", "build_deps")
else:
    # Cross-compiling, the deps install script puts things in `bin`.
    ...
    deps_folder = os.path.join(caller_script_dir, "bin", "build_deps")
```

So the whole `%LOCALAPPDATA%\Godot\build_deps` tree — `accesskit`, `agility_sdk`, `mesa-*`,
`pix` — becomes invisible and it looks in `bin/build_deps`, which does not exist. SCons then
prints two ERROR lines about missing AccessKit and D3D12 dependencies, suggesting you run
`install_accesskit.py` / `install_d3d12_sdk_windows.py` — **do not**, they are already
installed and in the right place. It is looking in the wrong directory, not missing anything.

**The trap is that this exits 0.** It bails out during SConscript reading, before compiling
anything, so `$?` is 0, no object files are produced, and `bin/godot.windows.editor.x86_64.exe`
keeps its old timestamp. A background build "succeeds" in seconds and you test a stale binary.

Two habits that catch it regardless of shell:

- Check the binary's mtime against the clock after any build you intend to test.
- Grep the log for `ERROR:` rather than trusting the exit code.

Passing `accesskit_sdk_path=` and `agility_sdk_path=` explicitly also works, but only fixes the
two deps you happen to name — the mesa and pix paths are wrong in the same way.

## Pre-commit hooks (prek) — run these before pushing

CI runs Godot's full `prek` hook suite on every push. Formatting failures there are avoidable;
set the hooks up locally once:

```sh
pip install prek        # or: uv tool install prek
prek install            # install the git hook shims — hooks now run at commit time
prek run --all-files    # one-time full pass
prek run --files <paths>   # reproduce a CI failure for specific files
```

prek downloads its own pinned **clang-format v21.1.7** (`.pre-commit-config.yaml`). Do not
install clang-format separately — a different version formats differently than CI and will
fight the hooks.

Hooks that have bitten this fork before:

- **clang-format** — wants includes as one alphabetically sorted block, not multiple
  blank-line-separated groups. It rewrites files in place; just re-stage them.
- **validate-codeowners** — every new file under `modules/` must be covered by
  `.github/CODEOWNERS`, or CI fails with `<UNOWNED>`. See the fork-local block at EOF of that
  file; add new modules there. Verify locally with
  `python misc/scripts/validate_codeowners.py --unowned` (exit 0 = clean).

Run the module unit tests (requires a `tests=yes` build):

```sh
bin/godot.windows.editor.x86_64.exe --test --test-suite="*waifu*"
```

## Module conventions

`modules/waifu_test/` is the reference implementation — a minimal `RefCounted` class that
proves the whole loop (compiles → links → registers in ClassDB → passes its own tests).
Copy its shape for every new module:

```
modules/<name>/
  SCsub                    # env_modules.Clone(), add_source_files(module_obj, "*.cpp")
  config.py                # can_build(), configure(), get_doc_classes(), get_doc_path()
  register_types.{h,cpp}   # initialize_/uninitialize_<name>_module, guarded by init level
  <name>.{h,cpp}           # GDCLASS(...), _bind_methods()
  doc_classes/<Class>.xml  # regenerate with: bin/godot... --doctool --gdextension-docs
  tests/test_<name>.h      # doctest TEST_CASE("[Modules][<Name>] ...")
```

Fork modules so far: `waifu_test` (the reference), `projectiles` (`ProjectilePool`,
`ProjectileKind`), and `limboai` — behavior trees and hierarchical state machines.

`limboai` is **vendored third-party code**, not ours: a **git submodule** tracking
[limbonaut/limboai](https://github.com/limbonaut/limboai), pinned at tag `v1.8.1`
(commit `e45e60e`), MIT licensed. Clone the fork with `--recursive`, or run
`git submodule update --init` in an existing checkout, or the module directory is empty
and the build silently drops behavior trees. Re-pin it with a checkout inside the
submodule followed by a commit of the new gitlink in the parent:

```sh
git -C modules/limboai fetch --tags
git -C modules/limboai checkout v1.9.0
git add modules/limboai && git commit -m "Bump limboai to v1.9.0"
```

Pick its version from the compatibility table in its README, not from what is newest:
the `1.8.x` line is the one built against Godot 4.7, and its `deps.env` says
`GODOT_REF=4.7-stable`. Do not edit anything under it — a local change is a merge
conflict on every upgrade, exactly like a core patch. Its CODEOWNERS line exists only
to satisfy the CI hook; it does not mean we maintain it — and note that line is written
**without** a trailing slash (`/modules/limboai`), because a submodule is a single gitlink
entry in the tree rather than a directory, so the usual `/modules/<name>/` directory pattern
matches nothing and `validate_codeowners.py` reports `<UNOWNED>`.

Its `demo/` subtree tends to show as locally modified — those are Godot editor `.import`
artifacts from opening the demo project, not edits to module source. Harmless; leave them.

- **The directory name is an identifier, not a label.** `modules/modules_builders.py` writes
  `initialize_<dirname>_module` into `register_module_types.gen.cpp` and
  `MODULE_<DIRNAME_UPPER>_ENABLED` into `modules_enabled.gen.h`, both verbatim — capitals and
  underscores included. So renaming a module's folder means renaming those two functions in
  its `register_types.{h,cpp}` to match, or the build fails to link. Every upstream module is
  lowercase; stay lowercase and the question never comes up.
- Keep the Godot copyright header block on every new engine source file — the fork inherits
  upstream's licensing and file conventions.
- Guard registration on the correct `ModuleInitializationLevel`; registering at the wrong
  level is a silent failure or a startup crash.
- C++ in the engine means **crashes, not exceptions**. Bounds-check, null-check, and prefer
  `ERR_FAIL_*` macros over assumptions. Run `/code-review` on module diffs before they land.

## Box3D backend experiment (branch `feature/box3d-physics`)

An **experiment**, not a direction. Jolt remains the engine the game ships on; this
branch exists to find out what a Box3D backend would actually cost. `pre-box3d` tags
the last commit before it in both this repo and `Project_WAIFU`.

`modules/box3d_physics/` registers a second `PhysicsServer3D` alongside Jolt through
`PhysicsServer3DManager::register_server("Box3D", ...)`. It deliberately does **not**
call `set_default_server`, so nothing changes until a project picks `Box3D` in
`physics/3d/physics_engine`. That, plus the fact the whole thing is additive files,
is what makes the experiment reversible from both sides.

`thirdparty/box3d/` is Box3D v0.1.0 (`8441b4a`), MIT, C17, no dependencies. Only
`include/` and `src/` are vendored — `samples/`, `docs/`, `extern/` and `test/` are
not needed and `extern/sokol` is samples-only. SIMD needs no flags: `src/core.h`
selects `B3_SIMD_SSE2` or `B3_SIMD_NEON` from the target architecture itself.

Two build details worth not rediscovering:

- Box3D is **C17**, and the module environment is a C++ one. The library is compiled
  in its own `env_thirdparty` clone with `CFLAGS` (`/std:c17` on MSVC, `-std=gnu17`
  otherwise); putting it in the module env hands C++ flags to a `.c` compile.
- Include paths go in **`CPPPATH`**, not `CPPEXTPATH` — the latter does not exist in
  this tree, and setting it fails at compile time, not at SConscript time.

### The scaffold, and how to read it

`PhysicsServer3D` is 194 pure virtual methods, plus 48 on `PhysicsDirectBodyState3D`
and 7 on `PhysicsDirectSpaceState3D` — 249 in all. The backend overrides every one of
them from the start so it links immediately, which means an unimplemented method is
otherwise indistinguishable from one that legitimately does nothing.

`B3_TODO()` in `box3d_todo.h` is what tells them apart: each stub warns once, naming
itself, so running a scene prints exactly the surface that scene exercises rather than
silence or a per-frame flood. **Every remaining `B3_TODO()` is a piece of work still to
do.** When the backend is finished there should be none left and the header should go.

The stubs were generated by parsing `servers/physics_3d/physics_server_3d.h`, so if
the interface shifts under a rebase, regenerating beats hand-patching. Two things that
generator has to get right and a naive one will not: nested enum return types need
qualifying in out-of-class definitions (`PhysicsServer3D::ShapeType`, not `ShapeType`),
and default arguments belong on the header declaration only, never on the definition.

### Known gaps — what `main.tscn` needs and Box3D has not got

These are design gaps, not missing implementation. Box3D v0.1.0 has no equivalent and
each needs a decision before the corresponding stubs can be written:

| Godot | Box3D | Where it bites |
|---|---|---|
| `Generic6DOFJoint3D` | `b3MotorJoint` — spring/motor work, **per-axis limits do not** | `Player.tscn` has 5 |
| `PinJoint3D` | `b3SphericalJoint` with limits off | works |
| `ConeTwistJoint3D` | `b3SphericalJoint` cone + twist | works |
| `HingeJoint3D` / `SliderJoint3D` | revolute / prismatic | limits + motor only |
| `CylinderShape3D` | ~~none~~ `b3CreateCylinder` tessellates one, 16 sides | resolved, approximate |
| `SoftBody3D` | none at all | 40 of the 249 methods can only ever be stubs |
| live shape scale | scale is baked at hull/mesh creation | rescaling means rebaking |

The one genuine win on the other side: `b3MotorJointDef` has
`angularHertz`/`angularDampingRatio`/`maxSpringTorque` as first-class documented API —
exactly what the `jolt_physics` patch in the rebase checklist had to expose by hand.

### What actually runs today

**`Project_WAIFU`'s `main.tscn` runs end to end on Box3D and tracks Jolt closely.**
Shapes, bodies, spaces, stepping, transform sync, all five joint types, areas,
contact reporting, `body_test_motion` and the shape queries are implemented.

The A/B that says so is `Tools/box3d/probe.gd` in `Project_WAIFU`, which loads the
real scene under either backend and dumps the rig after three seconds. Every body
settles within 3 mm of its position under Jolt — head at 2.050 against 2.053, pelvis
1.016 against 1.019, hands within 3 mm on all axes — and rays onto the CSG climbing
wall and ramp report y 5.556 and y 0.744 on both. Switch backends with `override.cfg`,
which sets `physics/3d/physics_engine` without touching the tracked `project.godot`.

Isolated behavior, measured earlier: a box and a sphere dropped from 5 m rest at
y=0.499 against a 1 mm soft-solver penetration; a capsule, a cylinder and a convex
hull land on a `ConcavePolygonShape3D` floor; a pin joint holds its anchor distance to
1.503 against Jolt's 1.503; a cone twist limits swing to 29.9 degrees against a 30
degree span; a 6DOF linear spring absorbs a 6 N s impulse with a 0.104 m peak
excursion and returns to its equilibrium pose exactly.

57 `B3_TODO()` stubs remain, and they are not all equal:

- **40 are `soft_body_*`** and are permanent. Box3D has no soft bodies at all, and
  `Project_WAIFU` uses none.
- **`collide_shape` and `body_set_collision_priority` are reachable from this project**
  — one call site in the game, one in the vendored `godot-xr-tools` `snap_path.gd` —
  so they are the next real work.
- The rest are unused here: `world_boundary`, `separation_ray`, `heightmap` and custom
  shape creation, the `space_*_contacts` debug hooks, `body_*_user_flags`,
  `body_set_force_integration_callback` and `shape_*_custom_solver_bias`.

### First performance numbers

`Tools/box3d/bench.gd` samples 900 physics ticks of `main.tscn` after a 2 s settle,
five runs per backend, on a plain `target=editor` build:

| | Box3D | Jolt | Box3D advantage |
|---|---|---|---|
| wall clock per tick | 1.702 ms | 1.903 ms | 0.201 ms (10.6%) |
| `TIME_PHYSICS_PROCESS` | 1.455 ms | 1.874 ms | 0.419 ms (22.3%) |

The two backends' five-run ranges do not overlap on either metric — Box3D's worst run
beats Jolt's best on both — so the ordering is real rather than noise. Against the
11.1 ms frame budget it is worth roughly 2-4% of a frame.

Four things have to be said about that number before anyone leans on it:

- **Run it with `--fixed-fps 90`.** Headless still paces the main loop to real time
  otherwise, so wall time comes back as 11.1 ms per tick on both backends — that is the
  tick rate being measured, not the physics.
- **Never measure on a `dev_build`.** The same scene reports 6.6 ms against 7.0 ms
  there, three times the real cost, because of asserts and lost optimization.
- **Most of the tick is GDScript**, not physics — LimboAI trees, navigation and NPC
  state machines. That cost is identical under both backends, so it cancels in the
  difference but makes any ratio taken from the absolute numbers far too flattering.
- **The scene is close to idle.** The rig has settled and two NPCs are walking; nothing
  here is a stress test, and Box3D is still missing `collide_shape` and per-area gravity
  overrides, both of which cost time once implemented.

Four Box3D behaviors that are not guessable from the headers and cost a debug cycle
each if rediscovered:

- **`b3CreateCylinder` is base-anchored**, spanning `y` in `[yOffset, yOffset+height]`
  (`hull.c:1789-1790`). Godot's `CylinderShape3D` is centered, so `yOffset` must be
  `-height/2`. Passing 0 sinks the shape by half its height and it rests at y≈0.
- **`b3CreateMeshShape` does not clone its mesh** — "must remain valid for the lifetime
  of this shape" — unlike hulls, which are cloned. The `b3MeshData*` is therefore owned
  per shape instance and destroyed strictly *after* the shape. Mesh collision also only
  generates contacts against static bodies, which is why concave shapes are skipped on
  non-static ones rather than silently doing nothing.
- **Vertex welding is skipped unless `weldTolerance > 0`** (`mesh.c:1579`), so setting
  `weldVertices = true` alone does nothing and the internal-edge handling that depends
  on it never engages.
- **Box3D winds its triangles opposite to Godot, and both engines cull back faces on a
  mesh raycast.** Passing Godot's winding through unchanged makes every concave surface
  solid from the wrong side: a ray dropped onto a floor misses it and hits whatever is
  below, while a ray from underneath reports a hit. `Box3DShape3D::instantiate` swaps
  the last two indices of each triangle to correct it. This is invisible in resting
  contact — a body still lands on a mesh floor either way, which is why it survived the
  first round of testing — and shows up only in queries. The A/B that pins it down is
  `Tools/box3d/csg_probe.gd` in `Project_WAIFU`: a quad and its mirror, ray-tested under
  both backends, where every case is exactly inverted between them.

### Joints, and what the 6DOF mapping can and cannot do

`Generic6DOFJoint3D` maps onto `b3MotorJointDef`, which is the one place Box3D is
better suited to this game than Jolt was: `linearHertz`/`angularHertz` with
`maxSpringForce`/`maxSpringTorque` are exactly the frequency-based drive with a real
torque ceiling that the `jolt_physics` patch had to expose by hand.

Three things about that mapping are worth knowing before tuning a hand against it:

- **A 6DOF spring holds the pose the joint was built at.** Godot derives the local
  frames from the joint node's transform relative to each body, so at construction the
  frames are already coincident and the spring separation is zero. The drive returns
  the body to *that* pose after a disturbance; it does not pull bodies together. Move
  the equilibrium with `G6DOF_JOINT_*_SPRING_EQUILIBRIUM_POINT`, which rebuilds the
  joint with a displaced frame A.
- **Per-axis limits are not supported and warn once.** Box3D's motor joint constrains
  all three axes with one scalar and has no limit range at all. An axis is a spring, a
  velocity motor, or rigid — never "free between -30 and +30 degrees". Where a limit is
  the point, use `ConeTwistJoint3D` (cone + twist), `HingeJoint3D` or `SliderJoint3D`,
  all of which map onto Box3D joints that do have limits.
- **Godot's spring parameter is a stiffness, Box3D's is a frequency.** The conversion
  `f = sqrt(k/m) / 2*pi` needs a mass, so it happens at build time from the lighter
  dynamic endpoint. For the angular case the true relation needs the inertia tensor
  about the joint axis, so mass stands in for it and the angular frequency is only
  approximate - the main reason a tuned Jolt hand will not feel identical here.
- **Godot's spring damping is a coefficient, Box3D's is a damping ratio**, related by
  `c = 2*zeta*sqrt(k*m)`. Handing the coefficient over as if it were the ratio is not a
  small error: the rig authors critical damping as values in the thousands, which read
  as a ratio is a wildly overdamped spring that creeps to its target over seconds. It
  presents as a spring that is too *weak*, not one that is too damped, which is what
  makes it worth naming. `_damping_to_ratio` divides it back out against the same
  stiffness and mass the frequency came from.

Two axis conventions differ and are converted rather than assumed:

- **Cone twist**: Godot twists about the frame's **X** axis
  (`godot_cone_twist_joint_3d.cpp:117` takes basis column 0), Box3D about **Z**
  (`types.h:881,887`). `cone_twist_frame()` relabels the frame. Without it a ragdoll
  limb bends around the wrong axis.
- **Hinge and slider** happen to agree already — revolute is Z on both, prismatic and
  Godot's slider are both X — so those need no conversion.

### Queries, and why CharacterBody3D needs the mover

Box3D's overlap and cast entry points take a `b3ShapeProxy` — a point cloud plus an
external radius — so every convex Godot shape maps to one and `Box3DShape3D::build_proxy`
builds it. Concave shapes have no proxy: Box3D can cast *against* a mesh but not *with*
one, which matches Godot, where concave shapes are static geometry.

The margin is signed, and the sign matters. It **grows** the proxy for an overlap test,
where it is a tolerance, and **shrinks** it for a sweep, where it is the skin that keeps
resting contact from registering as an initial overlap.

That still is not enough for `move_and_slide`, and the reason is worth keeping:

- **Box3D reports an already-interpenetrating shape at fraction 0 with a degenerate
  zero-length normal.** It is saying "already touching", not "you hit a wall here".
  Godot can do nothing with it — the surface classifies as neither floor nor wall, so
  `is_on_floor()` is false and slide has no plane to slide along.
- Treating that as a blocking hit **freezes** the character: it settles a fraction into
  the ground on its first landing and never moves again.
- Ignoring it lets the character **tunnel** straight through the floor and the wall.

Neither is a bug in the cast. The missing piece is depenetration, and
`b3World_CollideMover` + `b3SolvePlanes` is exactly the primitive Box3D provides for it:
the planes carry the push needed to separate and the solver returns a delta that is both
depenetrated and clipped. `body_test_motion` therefore routes a single-capsule body —
what `CharacterBody3D` almost always is — through the mover, and everything else through
the plain sweep. **A mover is a free capsule, not a shape on a body, so it collides with
the character's own shapes unless they are excluded**; forgetting that yields a
degenerate self-plane and no movement at all.

Verified against Jolt on the same scene: a capsule character walking into a wall stops
at x=4.105 (Jolt 4.100) resting at y=0.895 (Jolt 0.901) with `is_on_floor()` true, and
`cast_motion` returns 0.400 on both backends.

### Contacts, forces and locks

`PhysicsDirectBodyState3D` is complete. Contacts come from `b3Body_GetContactData` on
demand rather than being accumulated every step, because most bodies never have their
contacts asked for.

Three orientation and lifetime details that are easy to get backwards:

- **Manifold anchors are relative to body A's center of mass, in world space** — not
  local to either body, and not relative to the body origin. Box3D does not order the
  pair, so which side is "us" has to be established before an anchor means anything.
- **The manifold normal points from shape A toward shape B.** Godot wants it pointing
  from the collider back toward this body — a box resting on the floor reports
  `(0, 1, 0)` — so it is negated when this body is A. Getting this backwards is
  invisible until something reads the normal.
- **Godot configures axis locks before the node enters the tree**, so by the time the
  b3 body exists the locks are already recorded. Applying them only in `set_axis_lock()`
  silently drops every lock authored in the editor; `_build()` has to replay them.

Constant forces have no Box3D counterpart — it clears its force accumulator each step
like any impulse-based solver — so they are held on the body and re-applied from the
space immediately before every step.

Collision exceptions are a *pair* rule, which layer and mask bits cannot express, so
they go through `b3World_SetCustomFilterCallback`. Box3D only consults that callback
when one of the two shapes set `enableCustomFiltering` (`types.h:68`), so the flag is
set per body and a scene with no exceptions never reaches the callback at all.

### Areas are sensors, and userData is shared

`Area3D` maps onto a kinematic Box3D body whose shapes carry `isSensor`. Overlaps
arrive as begin/end touch events after the step and are diffed into Godot's two
five-argument callbacks. Three things about that:

- **`enableSensorEvents` is false by default even for sensors, and applies to both
  sides** (`types.h:485-489`). `Box3DBody3D` therefore sets it on every shape it
  creates. Without that every area in the scene silently detects nothing.
- **Sensors are not excluded from each other** — `sensor.c:118-133` checks
  `enableSensorEvents`, same-body and the collision filter, but never `isSensor` — so
  area-to-area monitoring works with no extra machinery. Godot's `monitorable` flag has
  no Box3D counterpart and is enforced when the overlap is reported.
- **A body's and an area's `userData` occupy the same slot**, and an area is a
  *kinematic body*, so it emits move events too. Casting a move event's `userData`
  straight to `Box3DBody3D *` reads a `Callable` out of the middle of a `Box3DArea3D`.
  That is why both derive from `Box3DCollisionObject3D`, whose only job is to carry a
  type tag that makes the pointer identifiable before it is cast. Any new consumer of
  a Box3D `userData` must check `is_area()` first.

Per-area gravity and damping overrides for bodies *inside* an area are not implemented;
those areas report overlaps correctly but do not alter what is inside them. The space's
default area is the exception, because Godot routes the world's gravity through it —
which is why `space_create()` makes one and `area_set_param` accepts the space's own
RID as an alias for it.

Finally, **creating a joint does not wake its bodies.** A spring or motor attached to a
sleeping body does nothing at all until something unrelated wakes it, which reads
exactly like the joint being ignored. `_build()` ends with `b3Joint_WakeBodies()`.

## Additive modules over core patches — important

Every edit to a file under `core/`, `scene/`, `servers/`, or `drivers/` becomes a merge
conflict on **every** upstream pull. This fork tracks a live upstream branch, so the
maintenance cost of core diffs compounds forever.

Therefore:

- **Default: add a module.** Modules are new files; they rebase cleanly.
- Patching engine core requires an explicit reason stated up front and the user's agreement.
  If a core patch seems necessary, say so and justify it — don't do it quietly.
- Keep any unavoidable core patches small, self-contained, and listed here so rebases have
  a checklist.

### Current upstream-file diffs

Keep this list accurate — it is the rebase checklist.

| File | Change | Why |
|---|---|---|
| `.github/CODEOWNERS` | Fork-local block appended at EOF, one line per fork module (`/modules/waifu_test/`, `/modules/projectiles/`, both `@Th3D0c0`) | Required by the `validate-codeowners` CI hook; every file under `modules/` must have an owner. Placed at EOF (last-match-wins) so it also claims each module's `SCsub`/`config.py`, and because appending conflicts less than inserting into the Modules section. Add a line here with every new module. |

| `modules/jolt_physics/jolt_physics_server_3d.{h,cpp}` | `_bind_methods()` given a body: binds the four `generic_6dof_joint_*_jolt_{param,flag}` methods and their enum constants, and `init()`/`finish()` register/unregister an `Engine` singleton named `JoltPhysicsServer3D` | Jolt's 6DOF extensions exist in C++ but are unreachable from GDScript: upstream leaves `_bind_methods` empty, and the `PhysicsServer3D` scripting singleton is registered before the configured backend is constructed, so it captures the placeholder. The VR hand drives need `G6DOF_JOINT_ANGULAR_SPRING_FREQUENCY` (inertia-independent damping ratio) and `G6DOF_JOINT_ANGULAR_SPRING_MAX_TORQUE` (a real torque ceiling instead of clamping the commanded angle). Rationale is in the comments at the top of the `.cpp`. |

No diffs under `core/`, `scene/`, `servers/`, or `drivers/`. Keep it that way. The
`jolt_physics` row above is a module, not core, but it is still an upstream file and will
conflict on rebases — treat it with the same suspicion.

## Rebasing on upstream

```sh
git fetch upstream
git rebase upstream/4.7.1-stable      # or the newer stable branch when moving up
```

Custom modules under `modules/` should survive untouched. Anything that conflicts is, by
definition, a core patch that should be reconsidered.

## Performance context

The consumer of this engine has an **~11.1 ms budget per frame for both eyes at 90 Hz**.
Modules built here are usually the answer to a *measured* hot loop, not a speculative one.
Ask for the profile before writing the optimization.

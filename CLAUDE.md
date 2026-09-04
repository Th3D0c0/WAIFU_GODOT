# WAIFU_GODOT — engine fork for Project_WAIFU

Fork of Godot tracking `4.7.1-stable`. `origin` = `Th3D0c0/WAIFU_GODOT`,
`upstream` = `godotengine/godot`.

Exists to host custom C++ modules for **Project_WAIFU** (`D:\Godot\Projects\Project_WAIFU`),
a VR game where performance is the number one constraint. Target: PCVR now, standalone
Quest 3 / Android later — so modules must stay mobile-safe.

## Build

Toolchain on this machine: **MSVC (`cl`)**, Python 3.9.9, SCons 4.9.1, **32 cores**.
No ccache/sccache installed — full rebuilds are expensive, incremental builds are the norm.
`gh` is installed and authenticated as `Th3D0c0`, so CI failures can be read directly:
`gh run list`, then `gh run view --job <id> --log`. Worth doing before theorising about a
red job — the anonymous REST API serves run and job metadata but returns 403 for logs, and
check-run annotations only capture what a problem matcher recognized, which silently misses
linker errors.

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
| `.github/actions/godot-build/action.yml` | The template-build guard deletes only editor translation units (`find editor -type f \( -name '*.cpp' … \) -delete`) instead of `rm -rf editor` | `modules/limboai`'s `compat/compat_window_wrapper.h` includes `editor/gui/window_wrapper.h` under `LIMBOAI_MODULE` with no `TOOLS_ENABLED` guard, so every template build needs that header to resolve. The TU is empty in a module build — its whole body is `#ifdef LIMBOAI_GDEXTENSION` — so keeping headers costs nothing, and deleting the sources still fails the link if editor code ever genuinely leaks into a template. Revert to `rm -rf editor` once limboai guards the include. |
| `modules/mono/build_scripts/build_assemblies.py` | `NoWarn` extended from `1591` to `1591%3B0108` | CI builds the generated C# glue with `--werror`, and the glue is generated from ClassDB, so a third-party module that collides with a base member becomes a CS0108 error. `modules/limboai` does it twice (`BTTask.Status` over `BT.Status`, `BBParam.GetType()` over `object.GetType()`); the generator cannot emit `new` and the submodule is not ours to patch. `%3B` is a literal `;` — MSBuild splits an unescaped one in a `/p:` value into a second property. |
| `modules/SCsub` | A `vendored_modules` list (currently `["limboai"]`); those modules' `tests/*.h` are not collected into `modules_tests.gen.h` | limboai's `tests/test_for_each.h` and `tests/test_set_var.h` each `memnew` a `Node` they never free, and doctest re-enters a `TEST_CASE` body once per leaf `SUBCASE` — 4 and 18 instances, the 22 `ObjectDB` leaks the suite reports. LeakSanitizer comes along with `use_asan=yes` on Linux and fails `--test` on them. The tests are limboai's and we cannot patch them from here. Runtime code is unaffected; only the vendored test suite is skipped. |
| `.github/workflows/linux_builds.yml` | `module_limboai_enabled=no` added to the "Editor with doubles and GCC sanitizers" job only | That job is the largest link in the matrix — ASan + UBSan + doubles at `-O0` — and upstream already sat just under the 2 GB reach of `R_X86_64_PC32`. limboai pushed it ~25 MiB past, and mold failed with 120 `relocation out of range` errors against `libstdc++.a`'s `.gcc_except_table`. Purely a size ceiling, not a code defect: the module still builds under GCC on the Mono editor job, under clang on the other two sanitizer jobs, and on every template and non-Linux job. If a future addition overflows it again, the next lever is `optimize=debug` on that job. |

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

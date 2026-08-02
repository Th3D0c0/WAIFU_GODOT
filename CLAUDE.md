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

**Current core patches: none.** (Keep this line accurate.)

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

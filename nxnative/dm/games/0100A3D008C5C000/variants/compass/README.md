# Compass opt-in reference

This directory is not loaded by the default Scarlet profile. It records the user-confirmed final UI and camera patch, and the explicit compatibility input needed when regenerating `main`. No GUI/aspect-importer changes are required for this direct rewriter route.

## Rewrite main

From the repository root, with the usual rewriter Python dependencies installed:

```sh
variant=nxnative/dm/games/0100A3D008C5C000/variants/compass
DM_TARGET=android DM_PATCHES_EXTRA="$variant/aspect_cuts.py" \
  python nxnative/dm/tools/patch_nso.py \
  "$RAW_MAIN" "$FRESH_OUTPUT" main --compat "$variant/compat-spec.tsv"
```

Set `RAW_MAIN` to the user's unmodified matching NSO and `FRESH_OUTPUT` to a fresh directory under `.gamedump/<App>/`. The manifest verifies the full main build identity and seven displaced instructions. Registered Scarlet cuts supply 153 writes; the opt-in file adds 21. Do not layer another aspect patch over these cuts.

Keep the generated `main.patched.bin`, `main.gates.tsv`, `main.fixups.tsv`, `main.compat.tsv` and shared `compat.tsv` together. Preserve the matching already-rewritten `sdk` and `subsdk9` binaries and their tables: auxiliary operation regeneration is deliberately unsupported. `reference.json` records their runtime hashes. The compatibility manifest's shorter auxiliary IDs are runtime prefixes, not full raw-module identity checks; verify the paired file hashes separately.

## Runtime and packaging

Use the normal packaging/signing and runtime-manifest generation machinery with the coherent files above. Regenerate the manifest file hashes and its bundle hash; never reuse the historical bundle hash after changing files. Keep package/version identity when updating an existing install and use the user's existing signing key. No key or copyrighted runtime payload is distributed here.

Set external `env.txt` to `DM_PRESENT_ASPECT=43:18`. The confirmed artifact also had that embedded default via a local build-source substitution; this reference does not add that substitution to generic core or claim a byte-reproducible host/APK. The installed host included temporary geometry/resource diagnostics and pre-existing presentation/menu changes, deliberately excluded from this commit scope. A host without the corresponding aspect-presentation support is not equivalent merely because the guest cuts match.

`reference.json` records observed portable options, not defaults to impose. In particular, retain the user's current Fill/Fit and all other settings. Device-specific paths and control placement are omitted. Configure DLC content through the normal data-pack setup; no device AOC path is recorded.

## User-supplied content

Supply the matching Compass auxiliary modules, script override and complete active RomFS overlay (including its archive index and model files), plus the base game/update/DLC, shaders and any selected Vulkan driver. These are not included. Overlay location is shown as `usermods/<user-selected-mod>/romfs`, not a personal installation identifier. Mod release version/source were not established; hashes identify only observed files, not a complete mod distribution.

The user confirmed the final centered UI, scale and camera. The earlier branding regression was not independently diagnosed; this reference does not attribute a branding fix to these cuts. Historical artifact hashes identify the verified installation, not a guarantee of identical APK output from this limited scope.

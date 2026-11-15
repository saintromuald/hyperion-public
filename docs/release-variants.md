# Hyperion Zero Private and Hyperion Shield Zero releases

Hyperion has two package variants. They use the same engine build and
`engine/tools/package.py`; the difference is what may be distributed with it.

| Variant | Audience | Game data | Publishing |
| --- | --- | --- | --- |
| **Hyperion Zero Private** | Private testing | A complete, maintainer-supplied StarCraft: Remastered install under `Game/` | Never upload publicly. Build and share only through an authorized private channel. |
| **Hyperion Shield Zero** | Public | None. The player supplies Remastered or classic Brood War files. | The manual `Hyperion Shield Zero release` workflow creates a prerelease or final release. |

Hyperion Shield Zero is the engine package HyperShield bundles and launches; Zero identifies
where the engine runs. Hyperion Zero Private is the separate maintainer-only test bundle.

Both packages contain `release.json` and `EDITION.txt`, recording the product,
version, `prerelease` or `release` channel, visibility, platform, and whether game
files are bundled. Shield Zero has no `Game/` directory and no private launcher. The private
launcher passes `--install-folder <the extracted Game directory>` to Hyperion and forwards
any additional arguments.

## Build Shield Zero locally

On Windows:

```bat
cd engine
python tools/build_engine_x64.py --no-install
python tools/package.py --edition shield --channel prerelease --out testrun/share/variants
```

On Linux:

```sh
cd engine
python tools/build_engine_linux.py
python tools/package.py --linux x64 --edition shield --channel prerelease --out testrun/share/variants
```

On an Apple-silicon Mac:

```sh
cd engine
python3 tools/build_engine_linux.py
python3 tools/package.py --mac --edition shield --channel prerelease --out testrun/share/variants
```

## Build Hyperion Zero Private

Zero Private **requires** a legally supplied Remastered install. The packager refuses
to make it without one, and refuses to let Shield Zero accidentally include one.

```sh
python tools/package.py --linux x64 \
  --edition zero --channel prerelease \
  --game-files '/path/to/StarCraft' \
  --out testrun/share/variants
```

Use the platform's launcher after extraction:

- Windows: `Play Hyperion Zero Private.cmd`
- Linux: `./play-hyperion-zero-private.sh`
- Mac: `"./Play Hyperion Zero Private.command"`

For a non-GUI check on Linux:

```sh
./play-hyperion-zero-private.sh --mod-check bw
```

Do not attach Hyperion Zero Private to a GitHub release, an issue, a public artifact, or a shared
scratch directory. There is deliberately no Hyperion Zero Private upload workflow.

## Prerelease, then release

Run **Hyperion Shield Zero release** manually with the source ref, a unique tag, and the
`prerelease` channel. It packages Windows, Linux, and macOS, then creates a public prerelease.
Test those exact packages before promoting them. The Mac build-input archive is only a workflow
artifact; it is not attached to the release.

The normal release track uses `release-v0.7.3`; the experimental track uses `pre-release` and a
distinct tag ending in `-pre`. To promote a tested release without replacing its assets:

```sh
gh release edit hyperion-shield-zero-vX.Y.Z-beta --prerelease=false
```

For a final release built directly from another ref, run the workflow with the `release` channel.
The workflow has no scheduled or push-triggered publishing.

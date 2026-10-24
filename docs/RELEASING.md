# Releasing

VWaveEditor uses two GitHub Actions workflows:

- `.github/workflows/ci.yml` builds and tests Linux and Windows on branch pushes
  and pull requests.
- `.github/workflows/release.yml` builds, tests, packages, and publishes tagged
  releases.

## Versioning

Versions come from git tags in `vMAJOR.MINOR.PATCH` format. CMake runs
`git describe --tags --abbrev=0` and stamps `Version.h`, the CLI, MCP server
metadata, and the window title. Un-tagged source trees fall back to the current
development version.

## Release artifacts

### Linux x86_64

`VWaveEditor-<tag>-linux-x64.tar.gz` contains:

- `vwave` CLI
- `vwave_editor` SDL2 editor
- README, LICENSE, NOTICES
- documentation

The binaries use the system SDL2, SDL2_ttf, and libsndfile libraries. Install the
distribution packages listed in the README before running them.

### Windows x86_64

`VWaveEditor-<tag>-windows-x64.zip` contains:

- `vwave.exe`
- `vwave_editor.exe`
- MinGW runtime and dependency DLLs discovered with `ldd`
- SDL2, SDL2_ttf, libsndfile, and codec DLLs
- README, LICENSE, NOTICES
- documentation

The Windows build uses MSYS2 MINGW64.

## Cutting a release

1. Ensure `master` is green on Linux and Windows.
2. Update the fallback version in `CMakeLists.txt` when appropriate.
3. Commit the release changes.
4. Create an annotated version tag:

   ```sh
   git tag -a v0.0.3 -m "VWaveEditor v0.0.3"
   git push --atomic origin master v0.0.3
   ```

5. The `release` workflow builds both platforms, runs the test suite, uploads
   artifacts, and creates a GitHub release with generated notes.

Package caching is enabled: Linux apt archives are cached with
`actions/cache`, and MSYS2 package downloads are cached by
`msys2/setup-msys2`.

The workflow can also be run manually with **Actions → release → Run workflow**.
Manual runs upload the platform artifacts but do not create a GitHub release
unless the ref is a `v*` tag.

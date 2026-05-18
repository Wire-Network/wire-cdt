# GitHub Actions vcpkg Binary Cache and NuGet Package Plan

## Goal

Refactor the GitHub Actions build workflow so vcpkg dependency builds are restored from and published to a NuGet-backed vcpkg binary cache, preferably GitHub Packages, instead of rebuilding large dependencies on every cache miss. Keep the existing vcpkg downloads cache as a secondary optimization for source archives and tools.

## Current State

- The main CI entry point is `.github/workflows/build.yaml`.
- The build runs in a discovered Ubuntu 24 container image and currently targets `ubuntu24`.
- The workflow already has `packages: write`, which is required for publishing NuGet packages to GitHub Packages.
- vcpkg is vendored in `vcpkg/` and bootstrapped inside the build job.
- vcpkg manifest mode is used through the root `vcpkg.json`.
- The selected target and host triplet are both `x64-linux-release`.
- The custom triplet lives at `.github/vcpkg-triplets/x64-linux-release.cmake`.
- The workflow currently caches:
  - `.ccache`
  - `vcpkg/downloads`
  - `~/.cache/vcpkg/archives`
- There is no current NuGet config, no vcpkg binary source configuration, and no explicit NuGet authentication setup.

## Target Design

Use vcpkg binary caching with a NuGet source backed by GitHub Packages:

```bash
VCPKG_BINARY_SOURCES="clear;nuget,https://nuget.pkg.github.com/${GITHUB_REPOSITORY_OWNER}/index.json,readwrite"
```

Expected behavior:

- Pull requests and branch builds restore matching binary packages when available.
- Trusted writes, such as pushes to `master`, release branches, and manual runs, publish new binary packages.
- Forked pull requests should use read-only or local-only caching because their `GITHUB_TOKEN` cannot reliably write packages and should not be given package-write secrets.
- `actions/cache` remains for vcpkg downloads and project-level ccache, but compiled vcpkg package reuse moves to the NuGet binary cache.

## Implementation Plan

### 1. Split the Build Step

Refactor the current monolithic `Build & Test` shell block into smaller steps:

- `Setup compiler environment`
- `Bootstrap vcpkg`
- `Configure NuGet authentication`
- `Configure CMake`
- `Build`
- `Test`

This makes cache setup failures easier to diagnose and allows the workflow to switch binary-cache modes based on event trust level.

### 2. Add NuGet Authentication

After `./vcpkg/bootstrap-vcpkg.sh`, fetch the NuGet client that vcpkg expects:

```bash
NUGET_EXE="$(./vcpkg/vcpkg fetch nuget | tail -n 1)"
```

Then add the GitHub Packages source:

```bash
mono "$NUGET_EXE" sources add \
  -Name "github" \
  -Source "https://nuget.pkg.github.com/${GITHUB_REPOSITORY_OWNER}/index.json" \
  -UserName "${GITHUB_REPOSITORY_OWNER}" \
  -Password "${GITHUB_TOKEN}" \
  -StorePasswordInClearText

mono "$NUGET_EXE" setapikey \
  "${GITHUB_TOKEN}" \
  -Source "https://nuget.pkg.github.com/${GITHUB_REPOSITORY_OWNER}/index.json"
```

The build container must include `mono` or another compatible way to run the NuGet executable used by vcpkg. If `mono` is not present in the current Ubuntu 24 builder image, add it to `.cicd/platforms/ubuntu24.Dockerfile`.

### 3. Configure Binary Sources by Trust Level

Use read-write binary caching only for trusted contexts:

- `push`
- `workflow_dispatch`
- repository-owned pull requests, if desired

Use read-only or no remote binary cache for untrusted fork pull requests.

Recommended environment expression:

```yaml
env:
  VCPKG_NUGET_FEED: https://nuget.pkg.github.com/${{ github.repository_owner }}/index.json
  VCPKG_BINARY_SOURCES_RW: clear;nuget,https://nuget.pkg.github.com/${{ github.repository_owner }}/index.json,readwrite
  VCPKG_BINARY_SOURCES_RO: clear;nuget,https://nuget.pkg.github.com/${{ github.repository_owner }}/index.json,read
```

Then choose the source in shell:

```bash
if [[ "${{ github.event_name }}" == "pull_request" && "${{ github.event.pull_request.head.repo.full_name }}" != "${{ github.repository }}" ]]; then
  export VCPKG_BINARY_SOURCES="$VCPKG_BINARY_SOURCES_RO"
else
  export VCPKG_BINARY_SOURCES="$VCPKG_BINARY_SOURCES_RW"
fi
```

If GitHub Packages read access is unavailable for forked pull requests, fall back to:

```bash
export VCPKG_BINARY_SOURCES="clear;default,readwrite"
```

### 4. Pass Binary Cache Settings to CMake/vcpkg

Export these before CMake configure:

```bash
export VCPKG_FEATURE_FLAGS="manifests,binarycaching"
export VCPKG_TARGET_TRIPLET=x64-linux-release
export VCPKG_HOST_TRIPLET=x64-linux-release
export VCPKG_OVERLAY_TRIPLETS="$PWD/.github/vcpkg-triplets"
```

The existing CMake configure can remain mostly unchanged, because vcpkg will read `VCPKG_BINARY_SOURCES` from the environment.

### 5. Keep the Existing Download Cache

Retain the current `Cache vcpkg downloads` step for:

- source tarballs
- vcpkg-acquired tools
- fallback behavior when a binary package is not yet available

Rename it to clarify its role:

```yaml
- name: Cache vcpkg downloads and tool archives
```

Do not rely on `~/.cache/vcpkg/archives` as the primary compiled package cache once the NuGet source is active. The NuGet cache should be the durable cross-run cache.

### 6. Stabilize Cache Inputs

The binary package ABI will already account for compiler, triplet, port versions, and relevant build settings. Still, keep workflow inputs stable:

- continue pinning vcpkg through `vcpkg-configuration.json`
- continue using `.github/vcpkg-triplets/x64-linux-release.cmake`
- avoid mutating compiler variables between configure runs
- keep `CC`, `CXX`, `VCPKG_TARGET_TRIPLET`, `VCPKG_HOST_TRIPLET`, and `VCPKG_OVERLAY_TRIPLETS` in one shared setup step
- avoid configuring ccache as a vcpkg port compiler launcher once NuGet binary cache restores are validated

### 7. Package Feed Naming and Retention

GitHub Packages will receive many vcpkg-generated NuGet packages. Establish cleanup rules before broad rollout:

- document that packages are generated by vcpkg binary caching
- decide whether old package versions are retained indefinitely or cleaned up periodically
- consider a scheduled cleanup workflow after the cache has proven stable
- avoid manual deletion of active packages unless a rebuild from source is acceptable

### 8. Rollout Sequence

1. Add NuGet/Mono support to the builder image if missing.
2. Add NuGet authentication and `VCPKG_BINARY_SOURCES` setup to `build.yaml`.
3. Run `workflow_dispatch` on a test branch and confirm vcpkg uploads packages.
4. Re-run the same commit and confirm vcpkg restores packages instead of rebuilding them.
5. Open a pull request from an internal branch and confirm read/write behavior.
6. Test a fork-style pull request path or force read-only mode to confirm it does not fail on package writes.
7. Compare total build time before and after the second cached run.
8. After validation, clean up any redundant local archive caching only if it no longer adds value.

## Proposed Workflow Shape

The `build` job should eventually look roughly like this:

```yaml
env:
  VCPKG_NUGET_FEED: https://nuget.pkg.github.com/${{ github.repository_owner }}/index.json

steps:
  - uses: actions/checkout@v5
    with:
      submodules: recursive

  - name: Cache ccache
    uses: actions/cache@v5
    with:
      path: .ccache
      key: ${{ runner.os }}-${{ matrix.platform }}-ccache-${{ github.sha }}
      restore-keys: |
        ${{ runner.os }}-${{ matrix.platform }}-ccache-

  - name: Cache vcpkg downloads and tool archives
    uses: actions/cache@v5
    with:
      path: |
        vcpkg/downloads
        ~/.cache/vcpkg/archives
      key: ${{ runner.os }}-${{ matrix.platform }}-vcpkg-downloads-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', '.github/vcpkg-triplets/**') }}
      restore-keys: |
        ${{ runner.os }}-${{ matrix.platform }}-vcpkg-downloads-

  - name: Setup build environment
    run: |
      echo "CC=/usr/bin/clang-18" >> "$GITHUB_ENV"
      echo "CXX=/usr/bin/clang++-18" >> "$GITHUB_ENV"
      echo "CMAKE_MAKE_PROGRAM=/usr/bin/ninja" >> "$GITHUB_ENV"
      echo "VCPKG_TARGET_TRIPLET=x64-linux-release" >> "$GITHUB_ENV"
      echo "VCPKG_HOST_TRIPLET=x64-linux-release" >> "$GITHUB_ENV"
      echo "VCPKG_OVERLAY_TRIPLETS=$PWD/.github/vcpkg-triplets" >> "$GITHUB_ENV"
      echo "CCACHE_DIR=$PWD/.ccache" >> "$GITHUB_ENV"
      echo "CCACHE_MAXSIZE=5G" >> "$GITHUB_ENV"

  - name: Bootstrap vcpkg
    run: ./vcpkg/bootstrap-vcpkg.sh

  - name: Configure NuGet authentication
    env:
      GITHUB_TOKEN: ${{ github.token }}
    run: |
      NUGET_EXE="$(./vcpkg/vcpkg fetch nuget | tail -n 1)"
      mono "$NUGET_EXE" sources add \
        -Name "github" \
        -Source "$VCPKG_NUGET_FEED" \
        -UserName "${{ github.repository_owner }}" \
        -Password "$GITHUB_TOKEN" \
        -StorePasswordInClearText
      mono "$NUGET_EXE" setapikey \
        "$GITHUB_TOKEN" \
        -Source "$VCPKG_NUGET_FEED"

  - name: Configure vcpkg binary cache
    run: |
      if [[ "${{ github.event_name }}" == "pull_request" && "${{ github.event.pull_request.head.repo.full_name }}" != "${{ github.repository }}" ]]; then
        echo "VCPKG_BINARY_SOURCES=clear;default,readwrite" >> "$GITHUB_ENV"
      else
        echo "VCPKG_BINARY_SOURCES=clear;nuget,$VCPKG_NUGET_FEED,readwrite" >> "$GITHUB_ENV"
      fi
      echo "VCPKG_FEATURE_FLAGS=manifests,binarycaching" >> "$GITHUB_ENV"

  - name: Configure CMake
    run: |
      cmake -B build -S . -G Ninja \
        -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_MAKE_PROGRAM="$CMAKE_MAKE_PROGRAM" \
        -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DVCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET" \
        -DVCPKG_HOST_TRIPLET="$VCPKG_HOST_TRIPLET" \
        -DVCPKG_OVERLAY_TRIPLETS="$VCPKG_OVERLAY_TRIPLETS"

  - name: Build
    run: cmake --build build -- -j "$(nproc)"

  - name: Test
    run: ctest --test-dir build/tests -j "$(nproc)" --output-on-failure
```

The example persists shared values through `$GITHUB_ENV`; GitHub Actions does not carry normal shell exports across steps.

## Acceptance Criteria

- CI still builds and tests on `ubuntu24`.
- First trusted run creates NuGet packages in GitHub Packages.
- Second trusted run shows vcpkg binary cache hits for `protobuf`, `llvm`, `zpp-bits`, and `magic-enum` where ABI-compatible.
- Fork or untrusted pull requests do not fail because of package write attempts.
- Existing `ccache` behavior is preserved.
- Existing vcpkg source download caching is preserved or intentionally removed after measurements.
- Workflow logs clearly show the selected vcpkg binary cache mode.

## Risks and Mitigations

- **Missing Mono/NuGet runtime in the container:** add `mono-complete` or the minimal compatible package to `.cicd/platforms/ubuntu24.Dockerfile`.
- **Forked PR package access:** default forked PRs to read-only or no remote binary cache.
- **Package feed growth:** add cleanup policy after observing package churn.
- **Poisoned or stale binary packages:** rely on vcpkg ABI keys and pinned registry baseline; force source rebuild by changing triplet, dependency versions, or clearing package versions if necessary.
- **Environment leakage across steps:** persist required variables through `$GITHUB_ENV`, not transient shell `export`.

## Files Expected to Change During Implementation

- `.github/workflows/build.yaml`
- `.cicd/platforms/ubuntu24.Dockerfile`, only if the container lacks Mono/NuGet support
- possibly `BUILD.md`, if local developers should know how to opt into the same binary cache

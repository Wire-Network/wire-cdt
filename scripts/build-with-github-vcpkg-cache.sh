#!/usr/bin/env bash

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
JOBS="${JOBS:-$(nproc)}"
RUN_TESTS=1
NEEDS_BOOTSTRAP_CHOWN=0
BUILD_MODE="${WIRE_CDT_BUILD_MODE:-developer}"
VCPKG_BINARY_SOURCES=""
VCPKG_NUGET_FEED="${VCPKG_NUGET_FEED:-https://nuget.pkg.github.com/Wire-Network/index.json}"
CI_WORKFLOW_FILE="${CI_WORKFLOW_FILE:-$ROOT_DIR/.github/workflows/build.yaml}"
CI_PLATFORM="${CI_PLATFORM:-}"

usage() {
  cat <<USAGE
Usage: $0 [options]

Build Wire CDT with the GitHub Packages vcpkg NuGet binary cache.

Options:
  --build-dir DIR       CMake build directory. Default: $BUILD_DIR
  --jobs N             Parallel build/test jobs. Default: $JOBS
  --skip-tests         Configure and build only.
  --mode MODE          Build mode: developer, trusted-ci, or forked-pr-ci.
                       Default: $BUILD_MODE
  -h, --help           Show this help.

Environment:
  VCPKG_NUGET_FEED     NuGet feed URL. Default: $VCPKG_NUGET_FEED
  CI_WORKFLOW_FILE     CI workflow to mirror. Default: $CI_WORKFLOW_FILE
  CI_PLATFORM          Platform key from the workflow matrix. Default: parsed from workflow.
  GITHUB_TOKEN         GitHub token. Required for trusted-ci mode.
  GITHUB_USER          GitHub username or owner. Required for trusted-ci mode.
USAGE
}

fail() {
  local message="$1"
  local correction="$2"

  printf '\nERROR: %s\n\nCorrection:\n%b\n\n' "$message" "$correction" >&2
  exit 1
}

info() {
  printf '==> %s\n' "$*"
}

require_command() {
  local command_name="$1"
  local package_hint="$2"

  if ! command -v "$command_name" >/dev/null 2>&1; then
    fail "'$command_name' is not installed." "Install it with:\n  sudo apt-get install -y $package_hint"
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      [[ $# -ge 2 ]] || fail "--build-dir requires a value." "Run '$0 --build-dir /path/to/build'."
      BUILD_DIR="$2"
      shift 2
      ;;
    --jobs)
      [[ $# -ge 2 ]] || fail "--jobs requires a value." "Run '$0 --jobs $(nproc)'."
      JOBS="$2"
      shift 2
      ;;
    --skip-tests)
      RUN_TESTS=0
      shift
      ;;
    --mode)
      [[ $# -ge 2 ]] || fail "--mode requires a value." "Use '--mode developer', '--mode trusted-ci', or '--mode forked-pr-ci'."
      BUILD_MODE="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      fail "Unknown option '$1'." "Run '$0 --help' to see supported options."
      ;;
  esac
done

if [[ "$BUILD_MODE" != "developer" && "$BUILD_MODE" != "trusted-ci" && "$BUILD_MODE" != "forked-pr-ci" ]]; then
  fail "Unsupported build mode '$BUILD_MODE'." "Use '--mode developer' for local builds, '--mode trusted-ci' for trusted GitHub Actions runs, or '--mode forked-pr-ci' for fork pull requests."
fi

if [[ "$BUILD_MODE" == "trusted-ci" || "$BUILD_MODE" == "forked-pr-ci" ]]; then
  NEEDS_BOOTSTRAP_CHOWN=1
fi

if [[ ! -f "$ROOT_DIR/CMakeLists.txt" || ! -d "$ROOT_DIR/vcpkg" ]]; then
  fail "The script could not locate the Wire CDT repository root." "Run this script from a complete Wire CDT checkout with submodules initialized:\n  git submodule update --init --recursive"
fi

if [[ ! -f "$CI_WORKFLOW_FILE" ]]; then
  fail "The CI workflow was not found at '$CI_WORKFLOW_FILE'." "Set CI_WORKFLOW_FILE to the build workflow used by CI, then rerun this script."
fi

PLATFORM_FILE="$(sed -nE 's/^[[:space:]]*platform-file:[[:space:]]*([^[:space:]]+).*/\1/p' "$CI_WORKFLOW_FILE" | head -n 1)"
if [[ -z "$PLATFORM_FILE" ]]; then
  fail "Could not parse the platform-file setting from '$CI_WORKFLOW_FILE'." "Make sure the workflow discover step contains a 'platform-file:' entry."
fi

if [[ "$PLATFORM_FILE" != /* ]]; then
  PLATFORM_FILE="$ROOT_DIR/$PLATFORM_FILE"
fi

if [[ ! -f "$PLATFORM_FILE" ]]; then
  fail "The workflow platform file was not found at '$PLATFORM_FILE'." "Check the 'platform-file:' entry in $CI_WORKFLOW_FILE."
fi

if [[ -z "$CI_PLATFORM" ]]; then
  CI_PLATFORM="$(sed -nE 's/^[[:space:]]*platform:[[:space:]]*\[([^]]+)\].*/\1/p' "$CI_WORKFLOW_FILE" | head -n 1 | cut -d, -f1 | tr -d '[:space:]')"
fi

if [[ -z "$CI_PLATFORM" ]]; then
  fail "Could not parse the build matrix platform from '$CI_WORKFLOW_FILE'." "Set CI_PLATFORM explicitly, for example:\n  CI_PLATFORM=ubuntu24 $0"
fi

require_command python3 python3

CI_DOCKERFILE_REL="$(python3 - "$PLATFORM_FILE" "$CI_PLATFORM" <<'PY'
import json
import sys

platform_file, platform = sys.argv[1], sys.argv[2]
with open(platform_file, encoding="utf-8") as f:
    platforms = json.load(f)

try:
    dockerfile = platforms[platform]["dockerfile"]
except KeyError:
    sys.exit(1)

print(dockerfile)
PY
)" || fail "Could not resolve Dockerfile for platform '$CI_PLATFORM' from '$PLATFORM_FILE'." "Make sure the workflow matrix platform exists in the workflow platform file, or set CI_PLATFORM to a valid platform key."

if [[ "$CI_DOCKERFILE_REL" == /* ]]; then
  CI_DOCKERFILE="$CI_DOCKERFILE_REL"
else
  CI_DOCKERFILE="$ROOT_DIR/$CI_DOCKERFILE_REL"
fi

if [[ ! -f "$CI_DOCKERFILE" ]]; then
  fail "The CI Dockerfile resolved from the workflow was not found at '$CI_DOCKERFILE'." "Check platform '$CI_PLATFORM' in $PLATFORM_FILE."
fi

EXPECTED_UBUNTU_CODENAME="$(sed -nE 's/^FROM[[:space:]]+ubuntu:([^[:space:]]+).*/\1/p' "$CI_DOCKERFILE" | head -n 1)"
EXPECTED_LLVM_APT_REPO="$(sed -nE 's/.*(deb[[:space:]]+http:\/\/apt\.llvm\.org\/[^"]+).*/\1/p' "$CI_DOCKERFILE" | head -n 1)"
LLVM_MAJOR="$(sed -nE 's/.*llvm-toolchain-[[:alnum:]_.-]+-([0-9]+).*/\1/p' "$CI_DOCKERFILE" | head -n 1)"

if [[ -z "$EXPECTED_UBUNTU_CODENAME" || -z "$EXPECTED_LLVM_APT_REPO" || -z "$LLVM_MAJOR" ]]; then
  fail "Could not parse Ubuntu or LLVM settings from '$CI_DOCKERFILE'." "Make sure the Dockerfile contains a 'FROM ubuntu:<codename>' line and an apt.llvm.org 'llvm-toolchain-<codename>-<major>' repository line."
fi

CLANG_BIN="/usr/bin/clang-$LLVM_MAJOR"
CLANGXX_BIN="/usr/bin/clang++-$LLVM_MAJOR"

if [[ -f /etc/os-release ]]; then
  # shellcheck disable=SC1091
  source /etc/os-release
else
  fail "/etc/os-release is missing." "Build on the Ubuntu '$EXPECTED_UBUNTU_CODENAME' environment defined by $CI_DOCKERFILE to match the CI vcpkg ABI."
fi

if [[ "${ID:-}" != "ubuntu" || "${VERSION_CODENAME:-}" != "$EXPECTED_UBUNTU_CODENAME" ]]; then
  fail "This host is '${PRETTY_NAME:-unknown}', but the CI cache is built from 'ubuntu:$EXPECTED_UBUNTU_CODENAME'." "Use an Ubuntu '$EXPECTED_UBUNTU_CODENAME' host, or build inside the CI builder image defined by $CI_DOCKERFILE, before expecting GitHub NuGet cache hits."
fi

if [[ "$(uname -m)" != "x86_64" ]]; then
  fail "This host architecture is '$(uname -m)', but the CI cache is x86_64." "Use an x86_64 Ubuntu '$EXPECTED_UBUNTU_CODENAME' host to reuse the current vcpkg binary cache."
fi

require_command cmake cmake
require_command ninja ninja-build
require_command git git
require_command mono mono-complete
if [[ ! -x "$CLANG_BIN" || ! -x "$CLANGXX_BIN" ]]; then
  fail "Clang $LLVM_MAJOR binaries are missing." "Install the compiler package used by CI:\n  sudo apt-get install -y clang-$LLVM_MAJOR"
fi

# In CI modes, GITHUB_TOKEN is injected directly; the GitHub CLI is only needed
# for developer mode so the script can reuse the local authenticated token.
if [[ "$BUILD_MODE" == "developer" ]]; then
  require_command gh gh
fi

if [[ ! -x "$ROOT_DIR/vcpkg/vcpkg" ]]; then
  info "Bootstrapping vcpkg"
  "$ROOT_DIR/vcpkg/bootstrap-vcpkg.sh"
  if [[ "$NEEDS_BOOTSTRAP_CHOWN" -eq 1 ]]; then
    chown -R "$(id -u):$(id -g)" "$ROOT_DIR"
  fi
fi

CLANG_VERSION="$("$CLANG_BIN" --version | head -n 1)"
if [[ "$CLANG_VERSION" != *"$LLVM_MAJOR."* ]]; then
  fail "$CLANG_BIN does not report LLVM major version $LLVM_MAJOR. Found: $CLANG_VERSION" "Install Clang $LLVM_MAJOR from the LLVM repository used by CI:\n  sudo wget -qO /etc/apt/trusted.gpg.d/apt.llvm.org.asc https://apt.llvm.org/llvm-snapshot.gpg.key\n  echo '$EXPECTED_LLVM_APT_REPO' | sudo tee /etc/apt/sources.list.d/llvm-toolchain-${EXPECTED_UBUNTU_CODENAME}-${LLVM_MAJOR}.list\n  sudo apt-get update\n  sudo apt-get install -y clang-$LLVM_MAJOR clang-tools-$LLVM_MAJOR lld-$LLVM_MAJOR llvm-$LLVM_MAJOR llvm-$LLVM_MAJOR-dev llvm-$LLVM_MAJOR-tools"
fi

CLANG_PACKAGE_VERSION="$(dpkg-query -W -f='${Version}' "clang-$LLVM_MAJOR" 2>/dev/null || true)"
if [[ -z "$CLANG_PACKAGE_VERSION" || "$CLANG_PACKAGE_VERSION" != *"~++"* ]]; then
  fail "clang-$LLVM_MAJOR does not look like the apt.llvm.org package used by CI. Installed package version: ${CLANG_PACKAGE_VERSION:-unknown}" "Reinstall Clang $LLVM_MAJOR from the repository parsed from $CI_DOCKERFILE:\n  echo '$EXPECTED_LLVM_APT_REPO' | sudo tee /etc/apt/sources.list.d/llvm-toolchain-${EXPECTED_UBUNTU_CODENAME}-${LLVM_MAJOR}.list\n  sudo apt-get update\n  sudo apt-get install -y clang-$LLVM_MAJOR clang-tools-$LLVM_MAJOR lld-$LLVM_MAJOR llvm-$LLVM_MAJOR llvm-$LLVM_MAJOR-dev llvm-$LLVM_MAJOR-tools"
fi

if [[ "$BUILD_MODE" == "forked-pr-ci" ]]; then
  export VCPKG_BINARY_SOURCES="clear;default,readwrite"
else
  if [[ "$BUILD_MODE" == "trusted-ci" ]]; then
    GITHUB_TOKEN="${GITHUB_TOKEN:-}"
    GITHUB_USER="${GITHUB_USER:-}"

    if [[ -z "$GITHUB_TOKEN" || -z "$GITHUB_USER" ]]; then
      fail "trusted-ci mode requires GITHUB_TOKEN and GITHUB_USER." "In the workflow step, pass:\n  GITHUB_TOKEN: \${{ github.token }}\n  GITHUB_USER: \${{ github.repository_owner }}\nFor fork PRs, use '--mode forked-pr-ci' instead."
    fi
  else
    if ! gh auth status -h github.com >/dev/null 2>&1; then
      fail "GitHub CLI is not authenticated." "Authenticate and request package-read scope:\n  gh auth login -h github.com\n  gh auth refresh -h github.com -s read:packages"
    fi

    GH_STATUS="$(gh auth status -h github.com 2>&1 || true)"
    if [[ "$GH_STATUS" != *"read:packages"* ]]; then
      fail "GitHub CLI token is missing the 'read:packages' scope." "Refresh the token scope, then rerun this script:\n  gh auth refresh -h github.com -s read:packages\n  gh auth status -h github.com"
    fi

    GITHUB_TOKEN="${GITHUB_TOKEN:-$(gh auth token)}"
    GITHUB_USER="${GITHUB_USER:-$(gh api user --jq .login)}"

    if [[ -z "$GITHUB_TOKEN" || -z "$GITHUB_USER" ]]; then
      fail "Could not resolve GitHub token or username." "Set GITHUB_TOKEN and GITHUB_USER explicitly, or fix GitHub CLI authentication with 'gh auth login'."
    fi
  fi

  NUGET_EXE="$("$ROOT_DIR/vcpkg/vcpkg" fetch nuget | tail -n 1)"
  if [[ ! -f "$NUGET_EXE" ]]; then
    fail "vcpkg did not return a usable nuget.exe path." "Run '$ROOT_DIR/vcpkg/vcpkg fetch nuget' and fix any reported vcpkg download errors."
  fi

  info "Configuring GitHub Packages NuGet source"
  mono "$NUGET_EXE" sources remove -Name "github" >/dev/null 2>&1 || true
  mono "$NUGET_EXE" sources add \
    -Name "github" \
    -Source "$VCPKG_NUGET_FEED" \
    -UserName "$GITHUB_USER" \
    -Password "$GITHUB_TOKEN" \
    -StorePasswordInClearText >/dev/null
  mono "$NUGET_EXE" setapikey "$GITHUB_TOKEN" -Source "$VCPKG_NUGET_FEED" >/dev/null

  if [[ "$BUILD_MODE" == "developer" ]]; then
    export VCPKG_BINARY_SOURCES="clear;nuget,$VCPKG_NUGET_FEED,read"
  else
    export VCPKG_BINARY_SOURCES="clear;nuget,$VCPKG_NUGET_FEED,readwrite"
  fi
fi

export CC="$CLANG_BIN"
export CXX="$CLANGXX_BIN"
export CMAKE_MAKE_PROGRAM=/usr/bin/ninja
export VCPKG_TARGET_TRIPLET=x64-linux-release
export VCPKG_HOST_TRIPLET=x64-linux-release
export VCPKG_OVERLAY_TRIPLETS="$ROOT_DIR/.github/vcpkg-triplets"
export VCPKG_FEATURE_FLAGS=manifests,binarycaching
export CCACHE_DIR="${CCACHE_DIR:-$ROOT_DIR/.ccache}"
export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-5G}"

CMAKE_LAUNCHER_ARGS=()
if [[ "${CCACHE_DISABLE:-0}" == "1" ]]; then
  info "Compiler cache: disabled by CCACHE_DISABLE=1"
elif command -v ccache >/dev/null 2>&1; then
  CMAKE_LAUNCHER_ARGS=(
    -DCMAKE_C_COMPILER_LAUNCHER=ccache
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
  )
  info "Compiler cache: ccache"
else
  info "Compiler cache: unavailable"
fi

info "Build directory: $BUILD_DIR"
info "NuGet feed: $VCPKG_NUGET_FEED"
info "Build mode: $BUILD_MODE"
info "vcpkg binary sources: $VCPKG_BINARY_SOURCES"
info "Compiler: $CLANG_VERSION"

CONFIGURE_LOG="$BUILD_DIR/vcpkg-nuget-configure.log"
mkdir -p "$BUILD_DIR"

info "Configuring CMake"
set +e
cmake -B "$BUILD_DIR" -S "$ROOT_DIR" -G Ninja \
  -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_MAKE_PROGRAM="$CMAKE_MAKE_PROGRAM" \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DVCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET" \
  -DVCPKG_HOST_TRIPLET="$VCPKG_HOST_TRIPLET" \
  -DVCPKG_OVERLAY_TRIPLETS="$VCPKG_OVERLAY_TRIPLETS" \
  "${CMAKE_LAUNCHER_ARGS[@]}" 2>&1 | tee "$CONFIGURE_LOG"
configure_status=${PIPESTATUS[0]}
set -e

if [[ "$configure_status" -ne 0 ]]; then
  fail "CMake configure failed." "Review $CONFIGURE_LOG. Common fixes:\n  sudo apt-get install -y mono-complete ninja-build cmake\n  gh auth refresh -h github.com -s read:packages\n  rm -rf '$BUILD_DIR' and rerun this script after changing compilers or triplets."
fi

if grep -q "Restored 0 package(s) from NuGet" "$CONFIGURE_LOG"; then
  info "Warning: vcpkg reported zero NuGet restores. This can mean the ABI does not match CI, or that the packages were already installed in '$BUILD_DIR'."
fi

# These messages are informational because vcpkg's restore wording can change
# between versions; the build itself remains the authoritative success check.
if grep -q "Restored [1-9][0-9]* package(s) from NuGet" "$CONFIGURE_LOG"; then
  info "Confirmed vcpkg restored packages from the GitHub NuGet cache"
else
  info "No NuGet restore line was printed. This usually means vcpkg packages were already installed in '$BUILD_DIR'."
fi

info "Building"
cmake --build "$BUILD_DIR" -- -j "$JOBS"

if [[ "$RUN_TESTS" -eq 1 ]]; then
  info "Running tests"
  ctest --test-dir "$BUILD_DIR/tests" -j "$JOBS" --output-on-failure
fi

info "Done"

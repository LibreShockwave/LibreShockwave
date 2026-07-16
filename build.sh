#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BUILD_TYPE="Debug"
BUILD_DIR=""
GENERATOR=""
INSTALL_DEPS=0
PRINT_DEPS=0
RUN_TESTS=1
CLEAN=0
WASM=0
JOBS=""
TARGETS=()
EXTRA_CMAKE_ARGS=()

usage() {
    cat <<'EOF'
Usage: ./build.sh [options] [-- extra-cmake-args...]

Build LibreShockwave's C++ targets with CMake.

Options:
  --deps                 Print dependency install commands for this Linux distro.
  --install-deps         Install native build dependencies for this Linux distro.
  --release              Use Release build type.
  --debug                Use Debug build type. This is the default.
  --wasm                 Build the browser/WASM player with Emscripten.
  --build-dir DIR        Use a custom CMake build directory.
  --generator NAME       Pass a CMake generator, for example "Ninja".
  --jobs N               Pass a parallel job count to cmake --build.
  --target NAME          Build one target. Can be used more than once.
  --no-tests             Do not run ctest after a native build.
  --clean                Remove the build directory before configuring.
  -h, --help             Show this help.

Examples:
  ./build.sh
  ./build.sh --wasm --release
  ./build.sh --release --generator Ninja
  ./build.sh --deps
  ./build.sh --install-deps
EOF
}

sudo_cmd() {
    if [[ "${EUID}" -eq 0 ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

source_os_release() {
    if [[ -r /etc/os-release ]]; then
        # shellcheck disable=SC1091
        . /etc/os-release
    else
        ID="unknown"
        ID_LIKE=""
        PRETTY_NAME="Unknown Linux"
    fi
}

dependency_commands() {
    source_os_release
    local distro="${ID:-unknown}"
    local like="${ID_LIKE:-}"

    case " ${distro} ${like} " in
        *" debian "*|*" ubuntu "*)
            cat <<'EOF'
sudo apt update
sudo apt install build-essential cmake ninja-build zlib1g-dev qt6-base-dev
EOF
            ;;
        *" fedora "*)
            cat <<'EOF'
sudo dnf install cmake gcc-c++ ninja-build zlib-ng-compat-devel qt6-qtbase-devel
# If zlib-ng-compat-devel is unavailable on your Fedora release:
sudo dnf install cmake gcc-c++ ninja-build zlib-devel qt6-qtbase-devel
EOF
            ;;
        *" rhel "*|*" centos "*|*" rocky "*|*" almalinux "*)
            cat <<'EOF'
sudo dnf install cmake gcc-c++ ninja-build zlib-devel qt6-qtbase-devel
EOF
            ;;
        *" arch "*)
            cat <<'EOF'
sudo pacman -S --needed base-devel cmake ninja zlib qt6-base
EOF
            ;;
        *" opensuse "*|*" suse "*)
            cat <<'EOF'
sudo zypper install cmake gcc-c++ ninja zlib-devel qt6-base-devel
EOF
            ;;
        *" alpine "*)
            cat <<'EOF'
sudo apk add build-base cmake ninja zlib-dev linux-headers qt6-qtbase-dev
EOF
            ;;
        *)
            cat <<'EOF'
# Unknown distribution. Install:
# - CMake 3.20 or newer
# - A C++20 compiler
# - zlib development headers
# - Optional: Ninja
# - Optional: Qt6 Widgets development headers for the editor
EOF
            ;;
    esac
}

install_deps() {
    source_os_release
    local distro="${ID:-unknown}"
    local like="${ID_LIKE:-}"

    case " ${distro} ${like} " in
        *" debian "*|*" ubuntu "*)
            sudo_cmd apt update
            sudo_cmd apt install -y build-essential cmake ninja-build zlib1g-dev qt6-base-dev
            ;;
        *" fedora "*)
            if ! sudo_cmd dnf install -y cmake gcc-c++ ninja-build zlib-ng-compat-devel qt6-qtbase-devel; then
                sudo_cmd dnf install -y cmake gcc-c++ ninja-build zlib-devel qt6-qtbase-devel
            fi
            ;;
        *" rhel "*|*" centos "*|*" rocky "*|*" almalinux "*)
            sudo_cmd dnf install -y cmake gcc-c++ ninja-build zlib-devel qt6-qtbase-devel
            ;;
        *" arch "*)
            sudo_cmd pacman -S --needed --noconfirm base-devel cmake ninja zlib qt6-base
            ;;
        *" opensuse "*|*" suse "*)
            sudo_cmd zypper install -y cmake gcc-c++ ninja zlib-devel qt6-base-devel
            ;;
        *" alpine "*)
            sudo_cmd apk add build-base cmake ninja zlib-dev linux-headers qt6-qtbase-dev
            ;;
        *)
            echo "Unsupported distribution for automatic dependency installation." >&2
            echo "Install these packages manually:" >&2
            dependency_commands >&2
            exit 1
            ;;
    esac
}

require_command() {
    local name="$1"
    local hint="$2"
    if ! command -v "${name}" >/dev/null 2>&1; then
        echo "Missing required command: ${name}" >&2
        echo "${hint}" >&2
        echo >&2
        echo "Dependency commands for this distro:" >&2
        dependency_commands >&2
        exit 1
    fi
}

lower_build_type() {
    printf '%s' "${BUILD_TYPE}" | tr '[:upper:]' '[:lower:]'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --deps)
            PRINT_DEPS=1
            shift
            ;;
        --install-deps)
            INSTALL_DEPS=1
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --wasm)
            WASM=1
            RUN_TESTS=0
            shift
            ;;
        --build-dir)
            BUILD_DIR="${2:?--build-dir requires a value}"
            shift 2
            ;;
        --build-dir=*)
            BUILD_DIR="${1#*=}"
            shift
            ;;
        --generator|-G)
            GENERATOR="${2:?--generator requires a value}"
            shift 2
            ;;
        --generator=*)
            GENERATOR="${1#*=}"
            shift
            ;;
        --jobs|-j)
            JOBS="${2:?--jobs requires a value}"
            shift 2
            ;;
        --jobs=*)
            JOBS="${1#*=}"
            shift
            ;;
        --target)
            TARGETS+=("${2:?--target requires a value}")
            shift 2
            ;;
        --target=*)
            TARGETS+=("${1#*=}")
            shift
            ;;
        --no-tests)
            RUN_TESTS=0
            shift
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            EXTRA_CMAKE_ARGS+=("$@")
            break
            ;;
        *)
            echo "Unknown option: $1" >&2
            echo >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ "${PRINT_DEPS}" -eq 1 ]]; then
    dependency_commands
    exit 0
fi

if [[ "${INSTALL_DEPS}" -eq 1 ]]; then
    install_deps
fi

require_command cmake "Install CMake 3.20 or newer."
if [[ "${WASM}" -eq 1 ]]; then
    require_command emcmake "Install and activate Emscripten, then retry."
    require_command emcc "Install and activate Emscripten, then retry."
fi

if [[ "${WASM}" -eq 1 ]]; then
    BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/cmake-build-wasm}"
else
    BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/cmake-build-$(lower_build_type)}"
fi
if [[ "${#TARGETS[@]}" -eq 0 ]]; then
    if [[ "${WASM}" -eq 1 ]]; then
        TARGETS=(libreshockwave_cpp_wasm_dist)
    else
        TARGETS=(
            libreshockwave_tests
            libreshockwave_editor_model_tests
            libreshockwave_probe
            libreshockwave_render_probe
            libreshockwave_wasm_bridge_probe
            libreshockwave_export_ts
        )
    fi
fi

if [[ "${CLEAN}" -eq 1 ]]; then
    rm -rf "${BUILD_DIR}"
fi

if [[ "${WASM}" -eq 1 ]]; then
    configure_cmd=(emcmake cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
else
    configure_cmd=(cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
fi
if [[ -n "${GENERATOR}" ]]; then
    configure_cmd+=(-G "${GENERATOR}")
fi
configure_cmd+=("${EXTRA_CMAKE_ARGS[@]}")

build_cmd=(cmake --build "${BUILD_DIR}")
for target in "${TARGETS[@]}"; do
    build_cmd+=(--target "${target}")
done
if [[ -n "${JOBS}" ]]; then
    build_cmd+=(--parallel "${JOBS}")
else
    build_cmd+=(--parallel)
fi

echo "+ ${configure_cmd[*]}"
"${configure_cmd[@]}"

echo "+ ${build_cmd[*]}"
"${build_cmd[@]}"

if [[ "${RUN_TESTS}" -eq 1 ]]; then
    echo "+ ctest --test-dir ${BUILD_DIR} --output-on-failure"
    ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi

#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Build the contest ESP32-S3 board with a temporary, reversible
# esp-hal-3rdparty lock-initializer backport.
#
# Fresh workspaces may not contain esp-hal-3rdparty yet. NuttX prepares it
# during the normal build context phase, so this wrapper bootstraps that
# phase first when needed, then applies the local backport and performs the
# real build.

set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly OPENVELA_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
readonly CONFIG_PATH="vendor/openvela/boards/contest2026_295_board/configs/nsh"
readonly PATCH_TOOL="${SCRIPT_DIR}/esp_hal_lock_backport.sh"
readonly MYENV_DIR="${OPENVELA_ROOT}/myenv"
readonly BUILD_SH="${OPENVELA_ROOT}/build.sh"
readonly NUTTX_DIR="${OPENVELA_ROOT}/nuttx"
readonly HAL_DIR="${NUTTX_DIR}/arch/xtensa/src/esp32s3/esp-hal-3rdparty"

cd "${OPENVELA_ROOT}"

if test ! -x "${MYENV_DIR}/bin/python"; then
  echo "Creating Python virtual environment: ${MYENV_DIR}"
  python3 -m venv "${MYENV_DIR}"
fi

# shellcheck disable=SC1091
source "${MYENV_DIR}/bin/activate"

# Keep the board-local esptool wrapper ahead of the system PATH.
export PATH="${SCRIPT_DIR}:${PATH}"

hal_checkout_ready()
{
  git -C "${HAL_DIR}" rev-parse --is-inside-work-tree >/dev/null 2>&1
}

reset_generated_build_state()
{
  rm -f "${NUTTX_DIR}/.config" \
        "${NUTTX_DIR}/Make.defs" \
        "${NUTTX_DIR}/staging/libarch.a" \
        "${NUTTX_DIR}/staging/libboard.a"
}

bootstrap_hal_checkout()
{
  if hal_checkout_ready; then
    return 0
  fi

  echo "HAL checkout is missing:"
  echo "  ${HAL_DIR}"
  echo
  echo "Bootstrapping the normal NuttX build context first..."

  reset_generated_build_state

  # Follow openvela's normal build path so NuttX creates/clones the ESP HAL
  # checkout using the revision expected by this NuttX tree.
  if ! "${BUILD_SH}" "${CONFIG_PATH}" context; then
    echo "HAL bootstrap via build.sh context failed." >&2
    echo "Expected HAL checkout:" >&2
    echo "  ${HAL_DIR}" >&2
    exit 1
  fi

  if ! hal_checkout_ready; then
    echo "build.sh context completed, but the HAL checkout was not created." >&2
    echo "Expected:" >&2
    echo "  ${HAL_DIR}" >&2
    exit 1
  fi

  echo "HAL checkout bootstrapped successfully."
  echo "HAL HEAD: $(git -C "${HAL_DIR}" rev-parse HEAD)"
}

rollback=0

restore_hal()
{
  local rc=$?

  trap - EXIT INT TERM

  if test "${rollback}" = 1; then
    if ! "${PATCH_TOOL}" reverse; then
      echo "failed to restore the HAL backport" >&2
      if test "${rc}" -eq 0; then
        rc=1
      fi
    fi
  fi

  exit "${rc}"
}

# The original wrapper called the patch helper before build.sh. On a fresh
# checkout that fails because esp-hal-3rdparty has not been prepared yet.
bootstrap_hal_checkout

patch_state="$("${PATCH_TOOL}" apply)"
echo "HAL backport: ${patch_state}"

if test "${patch_state}" = "APPLIED"; then
  rollback=1
fi

trap restore_hal EXIT INT TERM

# Reconfigure and perform the actual requested build, e.g. -j8.
reset_generated_build_state

"${BUILD_SH}" "${CONFIG_PATH}" "$@"

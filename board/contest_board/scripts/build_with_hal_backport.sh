#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Build the contest board with a temporary, reversible ESP HAL backport.

set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly OPENVELA_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
readonly CONFIG_PATH="vendor/openvela/boards/contest2026_295_board/configs/nsh"
readonly PATCH_TOOL="${SCRIPT_DIR}/esp_hal_lock_backport.sh"
readonly MYENV_DIR="${OPENVELA_ROOT}/myenv"

cd "${OPENVELA_ROOT}"
if test ! -x "${MYENV_DIR}/bin/python"; then
  python3 -m venv "${MYENV_DIR}"
fi
source "${MYENV_DIR}/bin/activate"

rollback=0
restore_hal()
{
  local rc=$?

  if test "${rollback}" = 1; then
    if ! "${PATCH_TOOL}" reverse; then
      echo "failed to restore the HAL backport" >&2
      test "${rc}" -eq 0 && rc=1
    fi
  fi

  exit "${rc}"
}

patch_state="$("${PATCH_TOOL}" apply)"
echo "HAL backport: ${patch_state}"
if test "${patch_state}" = "APPLIED"; then
  rollback=1
fi
trap restore_hal EXIT INT TERM

export PATH="${SCRIPT_DIR}:${PATH}"

rm -f "${OPENVELA_ROOT}/nuttx/.config" \
      "${OPENVELA_ROOT}/nuttx/Make.defs" \
      "${OPENVELA_ROOT}/nuttx/staging/libarch.a" \
      "${OPENVELA_ROOT}/nuttx/staging/libboard.a"

"${OPENVELA_ROOT}/build.sh" "${CONFIG_PATH}" "$@"

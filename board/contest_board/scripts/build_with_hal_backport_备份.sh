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
readonly HAL_PATCH_TOOL="${SCRIPT_DIR}/esp_hal_lock_backport.sh"
readonly NUTTX_BREAK_PATCH_TOOL="${SCRIPT_DIR}/nuttx_xtensa_break_backport.sh"
readonly NUTTX_RAW_EXC_PATCH_TOOL="${SCRIPT_DIR}/nuttx_xtensa_raw_exception_backport.sh"
readonly NUTTX_RAW_EXC="${NUTTX_RAW_EXC:-0}"
readonly AI_AGENT_PATCH_TOOL="${SCRIPT_DIR}/ai_agent_esp32s3_backport.sh"
readonly MYENV_DIR="${OPENVELA_ROOT}/myenv"
readonly BUILD_SH="${OPENVELA_ROOT}/build.sh"
readonly NUTTX_DIR="${OPENVELA_ROOT}/nuttx"
readonly HAL_DIR="${NUTTX_DIR}/arch/xtensa/src/esp32s3/esp-hal-3rdparty"

readonly VOICE_FIX_REQUIRED="${VOICE_FIX_REQUIRED:-1}"
readonly VOICE_TLS_SRC="${OPENVELA_ROOT}/packages/ai_agent/src/infra/vela_tls.c"
readonly VOICE_MIMO_SRC="${OPENVELA_ROOT}/packages/ai_agent/src/voice/mimo_voice.c"

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

refresh_demo_kconfig()
{
  (
    cd "${OPENVELA_ROOT}/packages/demos"
    "${OPENVELA_ROOT}/apps/tools/mkkconfig.sh" -m Demos -o Kconfig
  )
}

prepare_build_context()
{
  reset_generated_build_state

  # The ESP32-S3 context target resets and repatches the HAL mbedTLS
  # submodule.  Run it before applying the temporary ai_agent compatibility
  # patch so the patch survives the actual compilation.
  "${BUILD_SH}" "${CONFIG_PATH}" context
}

run_configured_make()
{
  local extra_flags="-Wno-cpp -Wno-deprecated-declarations"

  # Match build.sh's configuration environment without invoking context a
  # second time after the ai_agent patch has been applied.
  # shellcheck disable=SC1091
  set +e
  set +u
  source "${OPENVELA_ROOT}/build/envsetup.sh"
  set -e
  set -u
  export PATH="${OPENVELA_ROOT}/prebuilts/kconfig-frontends/bin:${PATH}"

  # Important: build only.  Do NOT run savedefconfig here and do NOT copy the
  # generated defconfig back into the board source tree.  A diagnostic build
  # must not silently mutate the persistent NSH board configuration.
  make -C "${NUTTX_DIR}" EXTRAFLAGS="${extra_flags}" "$@"
}


verify_voice_fix_sources()
{
  if test "${VOICE_FIX_REQUIRED}" != 1; then
    echo "Voice fix source verification: SKIPPED (VOICE_FIX_REQUIRED=${VOICE_FIX_REQUIRED})"
    return 0
  fi

  test -f "${VOICE_TLS_SRC}" || {
    echo "Missing voice TLS source: ${VOICE_TLS_SRC}" >&2
    exit 1
  }

  test -f "${VOICE_MIMO_SRC}" || {
    echo "Missing MiMo voice source: ${VOICE_MIMO_SRC}" >&2
    exit 1
  }

  local missing=0

  for marker in \
    'DNS start:' \
    'TCP connect start:' \
    'upload progress:'; do
    if ! grep -Fq "${marker}" "${VOICE_TLS_SRC}"; then
      echo "Voice fix marker missing from vela_tls.c: ${marker}" >&2
      missing=1
    fi
  done

  # The build wrapper must verify capability, not dictate runtime key policy.
  # Accept either lookup order as long as both the generic fallback and the
  # MiMo-specific key path still exist in get_api_key().
  local key_block
  key_block="$(
    sed -n '/static int get_api_key/,/^}/p' "${VOICE_MIMO_SRC}"
  )"

  if ! grep -Fq 'AGENT_CFG_KEY_MIMO_API_KEY' <<<"${key_block}"; then
    echo "MiMo voice key support is missing in mimo_voice.c" >&2
    missing=1
  fi

  if ! grep -Fq 'AGENT_CFG_KEY_API_KEY' <<<"${key_block}"; then
    echo "Generic API key fallback is missing in mimo_voice.c" >&2
    missing=1
  fi

  local first_key
  first_key="$(
    grep -Eo 'AGENT_CFG_KEY_(MIMO_API_KEY|API_KEY)' <<<"${key_block}" |
      head -n 1 || true
  )"

  if test "${first_key}" != "AGENT_CFG_KEY_MIMO_API_KEY"; then
    echo "Voice fix source verification: NOTE - get_api_key() checks ${first_key:-no-key} first; build allowed."
  fi

  # Ensure the current TTS response-capacity fix is present before spending a
  # full build.  These are source markers only; post-build verification below
  # checks that they actually reached the ELF.
  for marker in \
    'MIMO_TTS_HTTP_RESP_BYTES' \
    'TTS response bytes=' \
    'TTS audio base64 bytes=' \
    'TTS WAV bytes='; do
    if ! grep -Fq "${marker}" "${VOICE_MIMO_SRC}"; then
      echo "TTS v4 marker missing from mimo_voice.c: ${marker}" >&2
      missing=1
    fi
  done

  if test "${missing}" != 0; then
    echo "Refusing to build: expected voice-chain fixes are not present." >&2
    exit 1
  fi

  echo "Voice fix source verification: OK"
}

force_clean_rebuild()
{
  # The ai_agent sources live outside nuttx/, and old externally-built object
  # files can survive a context refresh.  Remove only the known stale MiMo
  # objects and application archives; do not distclean and do not touch the
  # board defconfig.
  echo "Removing stale MiMo/app archive build artifacts..."

  find "${OPENVELA_ROOT}/packages/ai_agent/src/voice" \
    -maxdepth 1 -type f \
    -name 'mimo_voice.c.*.o' \
    -print -delete 2>/dev/null || true

  rm -f \
    "${OPENVELA_ROOT}/apps/libapps.a" \
    "${NUTTX_DIR}/staging/libapps.a"

  echo "Forcing clean rebuild so current ai_agent sources are compiled..."
  make -C "${NUTTX_DIR}" clean
}


verify_built_voice_image()
{
  local elf="${NUTTX_DIR}/nuttx"

  # Skip artifact verification for maintenance targets.
  if printf '%s\n' "$@" | grep -Eq '(^|[[:space:]])(clean|distclean)([[:space:]]|$)'; then
    return 0
  fi

  if test ! -f "${elf}"; then
    echo "Post-build verification failed: missing ${elf}" >&2
    exit 1
  fi

  local missing=0
  for marker in \
    'TTS response bytes=' \
    'TTS audio base64 bytes=' \
    'TTS WAV bytes='; do
    if ! strings "${elf}" | grep -Fq "${marker}"; then
      echo "Post-build verification failed: ELF missing marker: ${marker}" >&2
      missing=1
    fi
  done

  if test "${missing}" != 0; then
    echo "The firmware was built, but the current mimo_voice.c was not linked into nuttx." >&2
    exit 1
  fi

  echo "Post-build voice verification: OK"
}

hal_rollback=0
nuttx_break_rollback=0
nuttx_raw_exc_rollback=0
ai_agent_rollback=0

restore_backports()
{
  local rc=$?

  trap - EXIT INT TERM

  if test "${ai_agent_rollback}" = 1; then
    if ! "${AI_AGENT_PATCH_TOOL}" restore; then
      echo "failed to restore the ai_agent ESP32-S3 backport" >&2
      if test "${rc}" -eq 0; then
        rc=1
      fi
    fi
  fi

  if test "${nuttx_break_rollback}" = 1; then
    if ! "${NUTTX_BREAK_PATCH_TOOL}" reverse; then
      echo "failed to restore the NuttX Xtensa BREAK backport" >&2
      if test "${rc}" -eq 0; then
        rc=1
      fi
    fi
  fi

  if test "${nuttx_raw_exc_rollback}" = 1; then
    if ! "${NUTTX_RAW_EXC_PATCH_TOOL}" reverse; then
      echo "failed to restore the raw Xtensa exception diagnostic" >&2
      if test "${rc}" -eq 0; then
        rc=1
      fi
    fi
  fi

  if test "${hal_rollback}" = 1; then
    if ! "${HAL_PATCH_TOOL}" reverse; then
      echo "failed to restore the HAL backport" >&2
      if test "${rc}" -eq 0; then
        rc=1
      fi
    fi
  fi

  exit "${rc}"
}

# The original wrapper called the patch helpers before build.sh.  The normal
# ESP32-S3 context target resets the mbedTLS submodule, so prepare it before
# applying the ai_agent compatibility patch.
bootstrap_hal_checkout

refresh_demo_kconfig

trap restore_backports EXIT INT TERM

prepare_build_context

patch_state="$("${HAL_PATCH_TOOL}" apply)"
echo "HAL backport: ${patch_state}"

if test "${patch_state}" = "APPLIED"; then
  hal_rollback=1
fi

patch_state="$("${NUTTX_BREAK_PATCH_TOOL}" apply)"
echo "NuttX Xtensa BREAK backport: ${patch_state}"

if test "${patch_state}" = "APPLIED"; then
  nuttx_break_rollback=1
fi

if test "${NUTTX_RAW_EXC}" = 1; then
  patch_state="$(${NUTTX_RAW_EXC_PATCH_TOOL} apply)"
  echo "NuttX raw Xtensa exception diagnostic: ${patch_state}"

  if test "${patch_state}" = "APPLIED"; then
    nuttx_raw_exc_rollback=1
  fi
fi

patch_state="$("${AI_AGENT_PATCH_TOOL}" apply)"
echo "ai_agent ESP32-S3 backport: ${patch_state}"

if test "${patch_state}" = "APPLIED"; then
  ai_agent_rollback=1
fi

verify_voice_fix_sources
force_clean_rebuild
run_configured_make "$@"
verify_built_voice_image "$@"

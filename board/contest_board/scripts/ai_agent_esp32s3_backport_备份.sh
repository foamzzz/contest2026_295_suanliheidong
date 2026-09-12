#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Temporary ESP32-S3 compatibility changes needed by ai_agent's mbedTLS build.

set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
readonly AGENT_DIR="${ROOT_DIR}/packages/ai_agent"
readonly HAL_DIR="${ROOT_DIR}/nuttx/arch/xtensa/src/esp32s3/esp-hal-3rdparty"
readonly MAKEDEFS="${ROOT_DIR}/apps/crypto/mbedtls/Make.defs"
readonly MBEDTLS_CFG="${HAL_DIR}/components/mbedtls/mbedtls/include/mbedtls/mbedtls_config.h"
readonly AGENT_PATCH="${SCRIPT_DIR}/../patches/0003-ai-agent-startup-and-request-guards.patch"
readonly AI_AGENT_B3_PATCH="${SCRIPT_DIR}/../patches/0005-ai-agent-b3-functional-restoration.patch"
readonly AI_AGENT_B31_PATCH="${SCRIPT_DIR}/../patches/0006-ai-agent-b3.1-message-bus-gate.patch"
readonly AI_AGENT_B32_PATCH="${SCRIPT_DIR}/../patches/0007-ai-agent-b3.2-monotonic-wlan-init.patch"
readonly AI_AGENT_VARIANT_PATCH="${AI_AGENT_VARIANT_PATCH:-}"
readonly STATE_DIR="${TMPDIR:-/tmp}/openvela-contest-ai-agent-backport"
readonly MARKER="${STATE_DIR}/root"

die()
{
  echo "ai_agent backport: $*" >&2
  exit 1
}

b3_enabled()
{
  test "${AI_AGENT_B3:-0}" = 1 || b31_enabled
}

b31_enabled()
{
  test "${AI_AGENT_B31:-0}" = 1 || b32_enabled
}

b32_enabled()
{
  test "${AI_AGENT_B32:-0}" = 1
}

variant_enabled()
{
  test -n "${AI_AGENT_VARIANT_PATCH}"
}

file_hash()
{
  sha256sum "$1" | awk '{print $1}'
}

require_targets()
{
  local file

  for file in "${MAKEDEFS}" "${MBEDTLS_CFG}"; do
    test -f "${file}" || die "missing target: ${file}"
  done

  if grep -Fq 'CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/include' \
      "${MAKEDEFS}" || \
     grep -Fq 'CFLAGS += -isystem $(APPDIR)/crypto/mbedtls/include' \
      "${MAKEDEFS}"; then
    :
  else
    die "unexpected mbedTLS CFLAGS context"
  fi

  if grep -Fq 'CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/include' \
      "${MAKEDEFS}" || \
     grep -Fq 'CXXFLAGS += -isystem $(APPDIR)/crypto/mbedtls/include' \
      "${MAKEDEFS}"; then
    :
  else
    die "unexpected mbedTLS CXXFLAGS context"
  fi
  grep -Eq '^#define MBEDTLS_CCM_C$|^/\* #define MBEDTLS_CCM_C \*/$' \
    "${MBEDTLS_CFG}" || die "unexpected MBEDTLS_CCM_C context"
  test -f "${AGENT_PATCH}" || die "missing patch: ${AGENT_PATCH}"
  if b3_enabled; then
    test -f "${AI_AGENT_B3_PATCH}" || die "missing patch: ${AI_AGENT_B3_PATCH}"
  fi
  if b31_enabled; then
    test -f "${AI_AGENT_B31_PATCH}" || die "missing patch: ${AI_AGENT_B31_PATCH}"
  fi
  if b32_enabled; then
    test -f "${AI_AGENT_B32_PATCH}" || die "missing patch: ${AI_AGENT_B32_PATCH}"
  fi
  if variant_enabled; then
    test -f "${SCRIPT_DIR}/../patches/${AI_AGENT_VARIANT_PATCH}" ||
      die "missing variant patch: ${AI_AGENT_VARIANT_PATCH}"
  fi
}

agent_patch_ready()
{
  git -C "${AGENT_DIR}" apply --check -p3 "${AGENT_PATCH}" >/dev/null 2>&1
}

source_already_integrated()
{
  # The contest workspace may intentionally carry the ai_agent changes from
  # 0003 plus later local fixes.  In that state the original patch cannot be
  # re-applied or reverse-checked as one exact diff, but the build must not
  # overwrite the user's source tree.  Check a few stable integration points
  # before treating the source patch as already present.
  test -f "${AGENT_DIR}/include/voice/mimo_voice.h" &&
  test -f "${AGENT_DIR}/src/voice/mimo_voice.c" &&
  grep -Fq 'voice_echo recordmem' \
    "${AGENT_DIR}/src/channels/nsh_commands.c" &&
  grep -Fq 'llm_heap_gate_check' \
    "${AGENT_DIR}/src/llm/llm_parse.c" &&
  grep -Fq 'TLS_WRITE_CHUNK_BYTES' \
    "${AGENT_DIR}/src/infra/vela_tls.c"
}

agent_patch_applied()
{
  # 0003 is the complete baseline patch.  Optional diagnostics are applied
  # on top when explicitly enabled.
  if b32_enabled; then
    git -C "${AGENT_DIR}" apply --reverse --check -p1 "${AI_AGENT_B32_PATCH}"
  elif variant_enabled; then
    git -C "${AGENT_DIR}" apply --reverse --check -p1 \
      "${SCRIPT_DIR}/../patches/${AI_AGENT_VARIANT_PATCH}"
  elif b31_enabled; then
    git -C "${AGENT_DIR}" apply --reverse --check -p1 "${AI_AGENT_B31_PATCH}"
  elif b3_enabled; then
    git -C "${AGENT_DIR}" apply --reverse --check -p1 "${AI_AGENT_B3_PATCH}"
  else
    git -C "${AGENT_DIR}" apply --reverse --check -p3 "${AGENT_PATCH}" \
      >/dev/null 2>&1 || source_already_integrated
  fi
}

all_applied()
{
  ! grep -Fq '${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/include' "${MAKEDEFS}" &&
  ! grep -Fq '${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/mbedtls/include' "${MAKEDEFS}" &&
  grep -Fqx '/* #define MBEDTLS_CCM_C */' "${MBEDTLS_CFG}"
}

all_ready()
{
  grep -Fq '${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/include' "${MAKEDEFS}" &&
  grep -Fq '${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/mbedtls/include' "${MAKEDEFS}" &&
  grep -Fqx '#define MBEDTLS_CCM_C' "${MBEDTLS_CFG}"
}

state_matches_root()
{
  test -f "${MARKER}" && test "$(cat "${MARKER}")" = "${ROOT_DIR}"
}

clear_state()
{
  rm -f "${STATE_DIR}/make.defs" "${STATE_DIR}/mbedtls_config.h" \
    "${STATE_DIR}/make.defs.sha256" "${STATE_DIR}/mbedtls_config.h.sha256" \
    "${MARKER}"
  rmdir "${STATE_DIR}" 2>/dev/null || true
}


expected_patched_hash()
{
  local target="$1"
  local backup="$2"
  local tmp

  tmp="$(mktemp "${TMPDIR:-/tmp}/ai-agent-backport-expected.XXXXXX")"
  cp -p "${backup}" "${tmp}"

  case "${target}" in
    "${MAKEDEFS}")
      sed -i \
        's|CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/include|CFLAGS += -isystem $(APPDIR)/crypto/mbedtls/include|g; s|CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/mbedtls/include|CFLAGS += -isystem $(APPDIR)/crypto/mbedtls/mbedtls/include|g; s|CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/include|CXXFLAGS += -isystem $(APPDIR)/crypto/mbedtls/include|g; s|CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/mbedtls/include|CXXFLAGS += -isystem $(APPDIR)/crypto/mbedtls/mbedtls/include|g' \
        "${tmp}"
      ;;
    "${MBEDTLS_CFG}")
      sed -i 's/^#define MBEDTLS_CCM_C$/\/\* #define MBEDTLS_CCM_C \*\//' \
        "${tmp}"
      ;;
    *)
      rm -f "${tmp}"
      return 1
      ;;
  esac

  file_hash "${tmp}"
  rm -f "${tmp}"
}

state_target_is_known()
{
  local target="$1"
  local backup="$2"
  local patched_hash_file="$3"
  local current_hash backup_hash expected_hash

  test -f "${target}" || return 1
  test -f "${backup}" || return 1

  current_hash="$(file_hash "${target}")"
  backup_hash="$(file_hash "${backup}")"

  if test "${current_hash}" = "${backup_hash}"; then
    return 0
  fi

  if test -s "${patched_hash_file}" &&
     test "${current_hash}" = "$(cat "${patched_hash_file}")"; then
    return 0
  fi

  expected_hash="$(expected_patched_hash "${target}" "${backup}")" || return 1
  test "${current_hash}" = "${expected_hash}"
}

recover_stale_state()
{
  local makedefs_backup="${STATE_DIR}/make.defs"
  local mbedtls_backup="${STATE_DIR}/mbedtls_config.h"

  state_matches_root || return 1

  # Optional layered patches touch ai_agent sources and cannot be reconstructed
  # safely from the two saved compatibility files alone.
  if b3_enabled || b31_enabled || b32_enabled || variant_enabled; then
    return 1
  fi

  # Never overwrite arbitrary user edits.  Recovery is allowed only when each
  # compatibility target is exactly either:
  #   1) the saved pre-apply file,
  #   2) the saved post-apply hash, or
  #   3) the deterministic post-apply transform of the saved file.
  state_target_is_known \
    "${MAKEDEFS}" \
    "${makedefs_backup}" \
    "${STATE_DIR}/make.defs.sha256" || return 1

  state_target_is_known \
    "${MBEDTLS_CFG}" \
    "${mbedtls_backup}" \
    "${STATE_DIR}/mbedtls_config.h.sha256" || return 1

  echo "ai_agent backport: recovering stale saved state" >&2
  cp -p "${makedefs_backup}" "${MAKEDEFS}"
  cp -p "${mbedtls_backup}" "${MBEDTLS_CFG}"
  clear_state
  return 0
}

restore_state()
{
  local target backup patched

  state_matches_root || die "no matching saved state"

  if ! git -C "${AGENT_DIR}" apply --reverse --check -p3 "${AGENT_PATCH}" \
      >/dev/null 2>&1; then
    source_already_integrated ||
      die "ai_agent patch changed after apply; refusing to overwrite it"
  else
    git -C "${AGENT_DIR}" apply --reverse -p3 "${AGENT_PATCH}"
  fi

  # NuttX's ESP32-S3 context target resets and repatches the mbedTLS
  # submodule.  It can therefore restore MBEDTLS_CFG to its pre-apply
  # content before this wrapper's EXIT trap runs.  Both the saved original
  # and the expected patched contents are safe restoration states.
  for target in "${MAKEDEFS}" "${MBEDTLS_CFG}"; do
    case "${target}" in
      "${MAKEDEFS}")
        backup="${STATE_DIR}/make.defs"
        patched="${STATE_DIR}/make.defs.sha256"
        ;;
      "${MBEDTLS_CFG}")
        backup="${STATE_DIR}/mbedtls_config.h"
        patched="${STATE_DIR}/mbedtls_config.h.sha256"
        ;;
    esac

    if ! state_target_is_known "${target}" "${backup}" "${patched}"; then
      die "${target} changed after apply; refusing to overwrite it"
    fi
  done

  if variant_enabled; then
    git -C "${AGENT_DIR}" apply --reverse -p1 \
      "${SCRIPT_DIR}/../patches/${AI_AGENT_VARIANT_PATCH}"
  fi
  if b32_enabled; then
    git -C "${AGENT_DIR}" apply --reverse -p1 "${AI_AGENT_B32_PATCH}"
  fi
  if b31_enabled; then
    git -C "${AGENT_DIR}" apply --reverse -p1 "${AI_AGENT_B31_PATCH}"
  fi
  if b3_enabled; then
    git -C "${AGENT_DIR}" apply --reverse -p1 "${AI_AGENT_B3_PATCH}"
  fi
  cp -p "${STATE_DIR}/make.defs" "${MAKEDEFS}"
  cp -p "${STATE_DIR}/mbedtls_config.h" "${MBEDTLS_CFG}"
  clear_state
}

apply_backport()
{
  require_targets

  if state_matches_root; then
    if all_applied && agent_patch_applied; then
      echo "ALREADY_APPLIED"
      return 0
    fi

    # A previous interrupted build can leave STATE_DIR behind while the next
    # NuttX context refresh restores one or both compatibility targets.  Do not
    # treat that recoverable situation as fatal.  Restore only known saved/
    # deterministic target contents and never overwrite ai_agent source edits.
    if recover_stale_state; then
      echo "ai_agent backport: stale state cleared; retrying apply" >&2
      require_targets
    else
      die "saved backport state is inconsistent; refusing to overwrite unknown target changes"
    fi
  fi

  if all_applied && agent_patch_applied; then
    echo "ALREADY_APPLIED"
    return 0
  fi
  all_ready && { agent_patch_ready || source_already_integrated; } ||
    die "targets are partially modified; refusing unsafe apply"

  mkdir -p "${STATE_DIR}"
  printf '%s\n' "${ROOT_DIR}" > "${MARKER}"
  cp -p "${MAKEDEFS}" "${STATE_DIR}/make.defs"
  cp -p "${MBEDTLS_CFG}" "${STATE_DIR}/mbedtls_config.h"

  if ! sed -i \
    's|CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/include|CFLAGS += -isystem $(APPDIR)/crypto/mbedtls/include|g; s|CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/mbedtls/include|CFLAGS += -isystem $(APPDIR)/crypto/mbedtls/mbedtls/include|g; s|CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/include|CXXFLAGS += -isystem $(APPDIR)/crypto/mbedtls/include|g; s|CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/mbedtls/include|CXXFLAGS += -isystem $(APPDIR)/crypto/mbedtls/mbedtls/include|g' \
    "${MAKEDEFS}"; then
    restore_state
  fi
  if ! sed -i 's/^#define MBEDTLS_CCM_C$/\/\* #define MBEDTLS_CCM_C \*\//' "${MBEDTLS_CFG}"; then
    restore_state
  fi
  if agent_patch_ready; then
    if ! git -C "${AGENT_DIR}" apply -p3 "${AGENT_PATCH}"; then
      restore_state
      die "failed to apply ai_agent startup/request patch"
    fi
  else
    echo "ai_agent source already integrated; skipped source patch" >&2
  fi
  if b3_enabled && ! git -C "${AGENT_DIR}" apply -p1 "${AI_AGENT_B3_PATCH}"; then
    git -C "${AGENT_DIR}" apply --reverse -p3 "${AGENT_PATCH}"
    cp -p "${STATE_DIR}/make.defs" "${MAKEDEFS}"
    cp -p "${STATE_DIR}/mbedtls_config.h" "${MBEDTLS_CFG}"
    clear_state
    die "failed to apply ai_agent B3 functional restoration patch"
  fi
  if b31_enabled && ! git -C "${AGENT_DIR}" apply -p1 "${AI_AGENT_B31_PATCH}"; then
    if b3_enabled; then
      git -C "${AGENT_DIR}" apply --reverse -p1 "${AI_AGENT_B3_PATCH}"
    fi
    git -C "${AGENT_DIR}" apply --reverse -p3 "${AGENT_PATCH}"
    cp -p "${STATE_DIR}/make.defs" "${MAKEDEFS}"
    cp -p "${STATE_DIR}/mbedtls_config.h" "${MBEDTLS_CFG}"
    clear_state
    die "failed to apply ai_agent B3.1 message-bus gate patch"
  fi
  if b32_enabled && ! git -C "${AGENT_DIR}" apply -p1 "${AI_AGENT_B32_PATCH}"; then
    git -C "${AGENT_DIR}" apply --reverse -p1 "${AI_AGENT_B31_PATCH}"
    if b3_enabled; then
      git -C "${AGENT_DIR}" apply --reverse -p1 "${AI_AGENT_B3_PATCH}"
    fi
    git -C "${AGENT_DIR}" apply --reverse -p3 "${AGENT_PATCH}"
    cp -p "${STATE_DIR}/make.defs" "${MAKEDEFS}"
    cp -p "${STATE_DIR}/mbedtls_config.h" "${MBEDTLS_CFG}"
    clear_state
    die "failed to apply ai_agent B3.2 monotonic/wlan-init patch"
  fi
  if variant_enabled && ! git -C "${AGENT_DIR}" apply -p1 \
      "${SCRIPT_DIR}/../patches/${AI_AGENT_VARIANT_PATCH}"; then
    if b32_enabled; then
      git -C "${AGENT_DIR}" apply --reverse -p1 "${AI_AGENT_B32_PATCH}"
    fi
    if b31_enabled; then
      git -C "${AGENT_DIR}" apply --reverse -p1 "${AI_AGENT_B31_PATCH}"
    fi
    if b3_enabled; then
      git -C "${AGENT_DIR}" apply --reverse -p1 "${AI_AGENT_B3_PATCH}"
    fi
    git -C "${AGENT_DIR}" apply --reverse -p3 "${AGENT_PATCH}"
    cp -p "${STATE_DIR}/make.defs" "${MAKEDEFS}"
    cp -p "${STATE_DIR}/mbedtls_config.h" "${MBEDTLS_CFG}"
    clear_state
    die "failed to apply ai_agent variant patch"
  fi
  all_applied && agent_patch_applied || { restore_state; die "post-apply context check failed"; }
  file_hash "${MAKEDEFS}" > "${STATE_DIR}/make.defs.sha256"
  file_hash "${MBEDTLS_CFG}" > "${STATE_DIR}/mbedtls_config.h.sha256"
  echo "APPLIED"
}

restore_backport()
{
  require_targets
  if state_matches_root; then
    restore_state
    echo "REVERTED"
  elif all_applied && agent_patch_applied; then
    echo "ALREADY_APPLIED"
  else
    echo "REVERTED"
  fi
}

status_backport()
{
  require_targets
  if state_matches_root; then
    all_applied && agent_patch_applied && echo "APPLIED" || { echo "DIRTY"; return 1; }
  elif all_applied && agent_patch_applied; then
    echo "ALREADY_APPLIED"
  elif all_ready && { agent_patch_ready || source_already_integrated; }; then
    echo "READY"
  else
    echo "DIRTY"
    return 1
  fi
}

case "${1:-}" in
  apply) apply_backport ;;
  restore) restore_backport ;;
  status) status_backport ;;
  *) echo "Usage: $0 {apply|restore|status}" >&2; exit 2 ;;
esac

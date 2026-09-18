#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_FILE="${SCRIPT_DIR}/../skills/robot-proactive-idle.md"
REMOTE_DIR="${1:-${ROBOT_AGENT_SKILLS_DIR:-}}"

if [[ -z "${REMOTE_DIR}" ]]; then
  echo "usage: $0 <official-agent-skills-dir>" >&2
  exit 2
fi

if ! command -v adb >/dev/null 2>&1; then
  echo "adb is required to install contest robot skills" >&2
  exit 1
fi

adb shell "mkdir -p ${REMOTE_DIR}"
adb push "${SKILL_FILE}" "${REMOTE_DIR}/robot-proactive-idle.md"
echo "Installed ${SKILL_FILE} to ${REMOTE_DIR}/robot-proactive-idle.md"

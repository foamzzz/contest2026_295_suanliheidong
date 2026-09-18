#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Temporary ESP32-S3 Wi-Fi association compatibility patch.
# packages/ai_agent is modified only while the build wrapper is running.

set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly OPENVELA_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
readonly TARGET="${OPENVELA_ROOT}/packages/ai_agent/src/infra/network_manager.c"

action="${1:-}"
if [[ "${action}" != "apply" && "${action}" != "restore" ]]; then
  echo "usage: $0 {apply|restore}" >&2
  exit 2
fi

python3 - "${action}" "${TARGET}" <<'PY'
from pathlib import Path
import sys

action = sys.argv[1]
target = Path(sys.argv[2])

old = r"""    snprintf(cmd, sizeof(cmd), "ifup %s", dev); /* bring up iface first */
    system(cmd);
    usleep(500000); /* wait for driver ready */

    snprintf(cmd, sizeof(cmd), "wapi mode %s 2", dev); /* MANAGED */
    system(cmd);

    if (pass && pass[0]) {
        snprintf(cmd, sizeof(cmd), "wapi psk %s %s 3", dev, q_pass);
        system(cmd);
    }

    snprintf(cmd, sizeof(cmd), "wapi essid %s %s 1", dev, q_ssid);
    system(cmd);
    usleep(2000000); /* wait for AP association */

    /* P1: verify WiFi association before requesting DHCP.
     * 'wapi status' always exits 0 (it just prints iface state), so
     * checking its return code is useless. Instead, parse the output
     * for "Not-Associated" which wapi prints when no AP is joined.
     * If popen fails we fall through and let network_wait_connected
     * handle the timeout. */
    {
        bool associated = true;
        snprintf(cmd, sizeof(cmd), "wapi status %s", dev);
        FILE* fp = popen(cmd, "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "Not-Associated")) {
                    associated = false;
                    break;
                }
            }
            pclose(fp);
        }
        if (!associated) {
            syslog(LOG_ERR,
                "[%s] WiFi not associated (no carrier), skipping DHCP\n", TAG);
            return ERROR;
        }
    }

    snprintf(cmd, sizeof(cmd), "renew %s", dev); /* DHCP */
    system(cmd);
"""
new = r"""    snprintf(cmd, sizeof(cmd), "ifup %s", dev); /* bring up iface first */
    system(cmd);
    usleep(500000); /* wait for driver ready */

    /*
     * ESP32-S3/WAPI compatibility:
     *
     * 1. Clear any stale IPv4 address before association. Otherwise
     *    network_is_connected() can mistake an old/default address for a
     *    successful Wi-Fi connection after DHCP fails.
     * 2. This driver requires ESSID to be selected before PSK is installed.
     *    PSK -> ESSID leaves AP as ff:ff:ff:ff:ff:ff on this board, while
     *    ESSID -> PSK completes association.
     */
    snprintf(cmd, sizeof(cmd), "ifconfig %s 0.0.0.0", dev);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "wapi mode %s 2", dev); /* MANAGED */
    system(cmd);

    snprintf(cmd, sizeof(cmd), "wapi essid %s %s 1", dev, q_ssid);
    system(cmd);

    if (pass && pass[0]) {
        snprintf(cmd, sizeof(cmd), "wapi psk %s %s 3", dev, q_pass);
        system(cmd);
    }

    /*
     * This board's WAPI build has no "status" subcommand. Poll "wapi show"
     * and require a real AP BSSID before starting DHCP.
     */
    {
        bool associated = false;
        int attempt;

        for (attempt = 0; attempt < 24 && !associated; attempt++) {
            snprintf(cmd, sizeof(cmd), "wapi show %s", dev);
            FILE* fp = popen(cmd, "r");

            if (fp) {
                char line[256];

                while (fgets(line, sizeof(line), fp)) {
                    char* ap = strstr(line, "AP:");
                    char bssid[32];

                    if (ap != NULL &&
                        sscanf(ap + 3, "%31s", bssid) == 1 &&
                        strcmp(bssid, "ff:ff:ff:ff:ff:ff") != 0 &&
                        strcmp(bssid, "00:00:00:00:00:00") != 0) {
                        associated = true;
                        break;
                    }
                }

                pclose(fp);
            }

            if (!associated)
                usleep(250000);
        }

        if (!associated) {
            syslog(LOG_ERR,
                "[%s] WiFi association timed out; skipping DHCP\n", TAG);
            return ERROR;
        }
    }

    /*
     * Association is confirmed. DHCP can still race the final driver state
     * transition, so retry renew a few times. A failed renew must not fall
     * through to network_wait_connected(), where another stale IPv4 address
     * could otherwise produce a false-positive.
     */
    {
        int renew_ret = ERROR;
        int attempt;

        for (attempt = 0; attempt < 3; attempt++) {
            snprintf(cmd, sizeof(cmd), "renew %s", dev);
            renew_ret = system(cmd);
            if (renew_ret == 0)
                break;

            usleep(500000);
        }

        if (renew_ret != 0) {
            syslog(LOG_ERR,
                "[%s] DHCP renew failed after WiFi association\n", TAG);
            return ERROR;
        }
    }
"""

text = target.read_text()

if action == "apply":
    if old in text:
        target.write_text(text.replace(old, new, 1))
        print("APPLIED")
    elif new in text:
        print("ALREADY_APPLIED")
    else:
        print(
            "Wi-Fi compatibility patch refused: expected network_wifi_connect "
            "block does not match current packages/ai_agent source.",
            file=sys.stderr,
        )
        sys.exit(1)
else:
    if new in text:
        target.write_text(text.replace(new, old, 1))
        print("REVERTED")
    elif old in text:
        print("ALREADY_RESTORED")
    else:
        print(
            "Wi-Fi compatibility restore refused: neither known block matches "
            "current packages/ai_agent source.",
            file=sys.stderr,
        )
        sys.exit(1)
PY

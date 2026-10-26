#!/bin/bash
set -euo pipefail
task_mode=${1:---service}
if [[ $task_mode != --service && $task_mode != --hooks ]]; then
    echo "Usage: $0 [--hooks|--service]" >&2
    exit 2
fi
if [[ $EUID -ne 0 ]]; then
    echo 'Run this installer with pkexec.' >&2
    exit 1
fi
task_source=$(cd -- "$(dirname -- "$0")" && pwd)
task_repo=$(cd -- "$task_source/../.." && pwd)
/usr/bin/python3 "$task_source/logi-host-switch.py" --config "$task_source/logi-host-switch.json" --check
test -x "$task_repo/build/logid"
install -d /var/lib/logi-host-switch /var/cache/logi-host-switch /usr/local/lib/logi-host-switch
if [[ ! -f /var/lib/logi-host-switch/logid.before-host-switch ]]; then
    cp -a /usr/bin/logid /var/lib/logi-host-switch/logid.before-host-switch
fi
install -m 755 "$task_source/logi-host-switch.py" /usr/local/lib/logi-host-switch/logi-host-switch.py
install -m 755 "$task_source/run-event.sh" /usr/local/lib/logi-host-switch/run-event.sh
if [[ ! -f /etc/logi-host-switch.json ]]; then
    install -m 644 "$task_source/logi-host-switch.json" /etc/logi-host-switch.json
fi
install -m 644 "$task_source/logi-host-switch.service" /etc/systemd/system/logi-host-switch.service
if [[ $task_mode == --hooks ]]; then
    # Stop the companion before logid starts invoking the native hooks.
    systemctl disable --now logi-host-switch.service
fi
systemctl stop logid.service
install -m 755 "$task_repo/build/logid" /usr/bin/logid
systemctl daemon-reload
systemctl start logid.service
if [[ $task_mode == --service ]]; then
    systemctl enable logi-host-switch.service
    systemctl restart logi-host-switch.service
fi

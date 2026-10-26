#!/bin/sh
# Run from logid's on_connect / on_disconnect hooks.
set -eu
export XDG_CACHE_HOME=/var/cache/logi-host-switch
export XDG_STATE_HOME=/var/lib/logi-host-switch
exec /usr/bin/python3 /usr/local/lib/logi-host-switch/logi-host-switch.py --event "$@"

#!/bin/sh
# PC hosts for the interactive demo (docker/emu-web.sh with MELEE_WEB_INSTANCES=2): runs tools/group/web_hosts.py inside
# the web container, so both consoles' Available Games lists show games with Mii heads, the emblem, an IN MATCH row
# and more games than one page holds.
#
#   docker/emu-web-hosts.sh [N]     # start N lobby hosts (default 5) plus a pair playing a match
#   docker/emu-web-hosts.sh stop    # stop them
#
# The sources come from the work volume (/work/src, as of the last docker/emu-test.sh build-dev). Nothing outside the
# container changes; the hosts vanish with it.
set -eu
NAME=${MELEE_WEB_NAME:-melee3ds-emu-web}
case "${1:-5}" in
    stop) docker exec "$NAME" sh -c 'pkill -f web_hosts.py; pkill group_client' || true; echo "Stopped the PC hosts."; exit 0 ;;
esac
docker exec "$NAME" sh -c 'pkill -f web_hosts.py; pkill group_client' >/dev/null 2>&1 || true
docker exec -d "$NAME" sh -c "cd /work/src && PYTHONDONTWRITEBYTECODE=1 MELEE_GROUP_TOOLS_DIR=/tmp/group-tools python3 tools/group/web_hosts.py --hosts ${1:-5} >/tmp/web-hosts.log 2>&1"
echo "Started the PC hosts in $NAME (log: docker exec $NAME cat /tmp/web-hosts.log)."

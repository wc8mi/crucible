#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 David Behnke
#
# Deploy the Crucible site (built by tools/crucible_site.py) to tinker.wc8mi.net/crucible/, then clear Cloudflare's
# cache for that path so no visitor gets a new page with old scripts.
#
# The purge token stays on this computer: ~/.config/crucible/cloudflare.env (mode 600) holds CF_ZONE_ID and
# CF_PURGE_TOKEN, a token with Zone > Cache Purge only. Without the file the upload still runs and the script says
# the cache was not cleared.
#
#   tools/crucible_deploy.sh            # deploy build/crucible-site
#   tools/crucible_deploy.sh --purge    # only clear the cache

set -eu

HOST=debian@azimuth-caddy-ingress
DEST=/var/www/tinker/crucible/
PREFIX=tinker.wc8mi.net/crucible/
SITE=https://tinker.wc8mi.net/crucible/
ENV=${CRUCIBLE_CF_ENV:-$HOME/.config/crucible/cloudflare.env}
KEEP=5				# deploy backups kept on the server
OUT=$(cd "$(dirname "$0")/.." && pwd)/build/crucible-site

purge() {
	if [ ! -r "$ENV" ]; then
		echo "deploy: no $ENV, Cloudflare's cache NOT cleared (old files stay up to 5 minutes)" >&2
		return 0
	fi
	# shellcheck disable=SC1090
	. "$ENV"
	reply=$(curl -s -X POST -H "Authorization: Bearer $CF_PURGE_TOKEN" -H "Content-Type: application/json" \
		--data "{\"prefixes\":[\"$PREFIX\"]}" "https://api.cloudflare.com/client/v4/zones/$CF_ZONE_ID/purge_cache")
	case $reply in
	*'"success":true'*) echo "deploy: Cloudflare cache cleared for $PREFIX" ;;
	*) echo "deploy: cache purge failed: $reply" >&2; return 1 ;;
	esac
}

if [ "${1:-}" = "--purge" ]; then
	purge
	exit
fi

[ -f "$OUT/index.html" ] && [ -f "$OUT/update.html" ] || { echo "deploy: build $OUT first (tools/crucible_site.py)" >&2; exit 1; }
if grep -rqs 'api\.radiofoundry\.org' --include='*.js' --include='*.html' --include='*.json' "$OUT"; then
	echo "deploy: the build names a RadioFoundry service; not deployed" >&2
	exit 1
fi

stamp=$(date +%Y%m%d-%H%M%S)
# Keep the last $KEEP deploy backups (named by time, so sorted oldest first); other backups there are left alone
ssh "$HOST" "sudo mkdir -p /var/www/tinker-backups && sudo cp -a $DEST /var/www/tinker-backups/crucible-$stamp &&
	cd /var/www/tinker-backups && ls -1d crucible-[0-9]* | head -n -$KEEP | xargs -r sudo rm -rf"
rsync -az --delete "$OUT/" "$HOST:/tmp/crucible-deploy/"
ssh "$HOST" "sudo rsync -a --delete --chown=root:root --chmod=D755,F644 /tmp/crucible-deploy/ $DEST && rm -rf /tmp/crucible-deploy"
echo "deploy: uploaded $(du -sh "$OUT" | cut -f1), old version in /var/www/tinker-backups/crucible-$stamp"
purge

want=$(sed -n 's/.*"\(crucible-[0-9a-f]\{8\}\)".*/\1/p' "$OUT/data/site.json" | head -1)
got=$(curl -s "${SITE}data/site.json" | sed -n 's/.*"\(crucible-[0-9a-f]\{8\}\)".*/\1/p' | head -1)
[ "$want" = "$got" ] && echo "deploy: live at $SITE ($got)" || { echo "deploy: live site shows $got, expected $want" >&2; exit 1; }

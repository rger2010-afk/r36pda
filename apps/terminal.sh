#!/bin/sh
# Терминал для r36pda. На R36S (ArkOS) использует st/fbterm, иначе fallback.
TERM="xterm"
if command -v st >/dev/null 2>&1; then TERM="st"; fi
if command -v fbterm >/dev/null 2>&1; then TERM="fbterm"; fi
exec $TERM
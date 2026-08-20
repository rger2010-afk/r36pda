#!/bin/bash
# r36pda — PDA desktop launcher (PortMaster-style layout).
# Скрипт лежит прямо в /roms/ports/, папка с данными — рядом: /roms/ports/r36pda/
# По образцу штатных портов: ES НЕ убиваем, SDL сам перехватывает экран.

PORTNAME="r36pda"
HERE="$(cd "$(dirname "$0")" && pwd)"
GAMEDIR="$HERE/$PORTNAME"
LOG="$GAMEDIR/log.txt"

cd "$GAMEDIR" || exit 1

chmod 666 /dev/tty0 /dev/tty1 /dev/uinput /dev/fb0 2>/dev/null
chmod +x ./r36pda

export TERM=linux
printf "\033c" > /dev/tty0 2>/dev/null
printf "\033c" > /dev/tty1 2>/dev/null

export SDL_RENDER_DRIVER=software

./r36pda > "$LOG" 2>&1
exit 0
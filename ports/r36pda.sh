#!/bin/bash
# Порт-лаунчер для ArkOS: запускается из меню Ports в EmulationStation.
# Управление: A/B — запуск, D-Pad — движение, F1 — назад в ES.

PORTNAME="r36pda"
GAMEDIR="/roms/ports/r36pda"
export SDL_VIDEODRIVER=fbcon
export SDL_FBDEV=/dev/fb0

cd "$GAMEDIR" || exit 1

# права на терминал и джойстик
chmod 666 /dev/tty1 2>/dev/null
chmod 666 /dev/input/js0 2>/dev/null
chmod +x ./r36pda ./apps/*.sh 2>/dev/null

# остановить ES, чтобы не конфликтовать за экран, запустить лаунчер,
# затем вернуть ES
pkill -f emulationstation 2>/dev/null
sleep 1
./r36pda < /dev/tty1 > /dev/tty1 2>&1
emulationstation &
exit 0
#!/bin/bash
# r36pda — PDA desktop launcher (PortMaster-style layout).
# Скрипт лежит прямо в /roms/ports/, папка с данными — рядом: /roms/ports/r36pda/

PORTNAME="r36pda"
HERE="$(cd "$(dirname "$0")" && pwd)"
GAMEDIR="$HERE/$PORTNAME"
LOG="/tmp/r36pda.log"

cd "$GAMEDIR" || { echo "cannot cd to $GAMEDIR" > "$LOG"; exit 1; }

chmod 666 /dev/tty1 2>/dev/null
chmod 666 /dev/input/js0 2>/dev/null
chmod +x ./r36pda ./apps/*.sh 2>/dev/null

echo "=== r36pda launcher ===" > "$LOG"
echo "dir: $GAMEDIR" >> "$LOG"
echo "bin: $(ls -la ./r36pda 2>&1)" >> "$LOG"

# остановить ES, чтобы не конфликтовать за экран
pkill -f emulationstation 2>/dev/null
sleep 1

# запуск с видеодрайвером fbcon; stdout/err в лог, картинка на fb
export SDL_VIDEODRIVER=fbcon
export SDL_FBDEV=/dev/fb0
./r36pda >> "$LOG" 2>&1
code=$?
echo "exit code: $code" >> "$LOG"
echo "log: $LOG" > /dev/tty1 2>&1

# вернуть эмулятор
emulationstation &
exit 0
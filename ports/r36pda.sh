#!/bin/bash
# r36pda — порт-лаунчер для ArkOS.
# Папка может лежать где угодно в /roms/ports/ — путь определяется сам.

PORTNAME="r36pda"
GAMEDIR="$(cd "$(dirname "$0")" && pwd)"
LOG="/tmp/r36pda.log"

cd "$GAMEDIR" || { echo "cannot cd to $GAMEDIR" > "$LOG"; exit 1; }

# права на терминал, джойстик и свои файлы
chmod 666 /dev/tty1 2>/dev/null
chmod 666 /dev/input/js0 2>/dev/null
chmod +x ./r36pda ./apps/*.sh 2>/dev/null

echo "=== r36pda launcher ===" > "$LOG"
echo "dir: $GAMEDIR" >> "$LOG"
echo "bin: $(ls -la ./r36pda 2>&1)" >> "$LOG"

# проверка SDL2 в системе
if ! ldconfig -p 2>/dev/null | grep -qi "libSDL2"; then
    echo "ОШИБКА: SDL2 не найдена в системе." >> "$LOG"
    echo "ОШИБКА: SDL2 не найдена в системе." > /dev/tty1 2>&1
    echo "Установи: sudo apt-get install libsdl2-2.0-0" >> "$LOG"
    sleep 8
    exit 1
fi
echo "SDL2: найдена" >> "$LOG"

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
#!/bin/sh
# Запуск игр: возврат в EmulationStation на R36S, на ПК — заглушка.
if command -v emulationstation >/dev/null 2>&1; then
    pkill emulationstation 2>/dev/null
    exec emulationstation
else
    echo "На ПК игр нет — это место для запуска EmulationStation на R36S." 
    sleep 2
fi
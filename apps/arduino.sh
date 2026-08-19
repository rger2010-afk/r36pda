#!/bin/sh
# Ардуино-рабочее место: список скетчей, компиляция, прошивка через arduino-cli.
# Убедитесь, что arduino-cli установлен:  arduino-cli core install arduino:avr

if ! command -v arduino-cli >/dev/null 2>&1; then
    echo "arduino-cli не установлен."
    echo "Установка: sudo apt install arduino-cli && arduino-cli core install arduino:avr"
    read -r _ 
    exit 1
fi

echo "=== Скетчи в ~/sketches ==="
mkdir -p ~/sketches
ls -la ~/sketches
echo ""
echo "Создать новый:  arduino-cli sketch new ~/sketches/mysketch"
echo "Компиль:        arduino-cli compile ~/sketches/mysketch"
echo "Прошить:        arduino-cli upload -p /dev/ttyACM0 -b arduino:avr:uno ~/sketches/mysketch"
exec bash
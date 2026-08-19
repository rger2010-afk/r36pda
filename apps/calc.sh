#!/bin/sh
# Простой калькулятор через python3 (есть на ArkOS).
if command -v python3 >/dev/null 2>&1; then
    exec python3 -c 'while True:
        try:
            print(eval(input("=> ")))
        except (KeyboardInterrupt, EOFError):
            break
        except Exception as e:
            print("err:", e)'
else
    echo "python3 не найден"
    read -r _
fi
#!/bin/sh
# Файловый менеджер: предпочитаем удобный, fallback на ls.
if command -v ranger >/dev/null 2>&1; then exec ranger; fi
if command -v mc >/dev/null 2>&1; then exec mc; fi
exec bash -c 'ls -la; echo "Установите mc или ranger для удобного файл-менеджера."; read -r _'
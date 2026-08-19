# RUNBOOK — как запустить r36pda на R36S

Пошагово от «включил консоль» до «рабочего стола КПК».

---

## Самый простой путь: порт на SD-карте (без интернета и без сборки)

Это способ для тех, у кого уже есть готовый пакет `r36pda-port`
(собранный один раз на другой консоли/ПК). Дальше — просто копирование:

1. Вставь SD-карту в ПК (или достань карту с играми).
2. Скопируй содержимое папки `r36pda-port/` на карту в каталог
   **`/roms/ports/`** — чтобы получилось:
   ```
   /roms/ports/r36pda/r36pda.sh
   /roms/ports/r36pda/r36pda
   /roms/ports/r36pda/apps.cfg
   /roms/ports/r36pda/apps/*.sh
   ```
   (Если раздела `roms` на карте нет — создай его.)
3. Вставь карту в консоль, включи.
4. В EmulationStation зайди в меню **Ports** → выбери **r36pda**.

Готово — никакой сети и компиляции не нужно. Лаунчер сам остановит
EmulationStation, покажет рабочий стол, а при выходе вернёт эмулятор обратно.

**Как собрать этот порт один раз:** см. шаг 3 ниже (`make package`).

---

## 0. Что понадобится

- R36S + Wi-Fi донгл (Realtek/Ralink; встроенный Wi-Fi обычно RTL8188)
- Компьютер в той же сети (для SSH/scp)
- (Опционально) кардридер для microSD, если нет Wi-Fi

Логин на консоль по SSH: `ark` / `ark` (смени после первого входа: `passwd`).

---

## 1. Подключение к консоли по SSH

1. Вставь Wi-Fi донгл в OTG-порт (правый USB-C).
2. В EmulationStation: **Options → Connect to WiFi** — выбери сеть, введи пароль.
3. **Options → Enable Remote Services** — включить (первый раз).
4. **Options → Network Info** — запомни IP.
5. С ПК: `ssh ark@<IP>` (Windows: `ssh` в терминале или putty).

Чтобы SSH работал всегда (а не только после включения меню):

```sh
sudo systemctl enable sshd
```

---

## 2. Перенос проекта на консоль

### Вариант А — по SSH (проще)

С ПК, из папки проекта:

```sh
scp -r r36pda ark@<IP>:/home/ark/
```

### Вариант Б — через SD-карту

1. Скопируй папку `r36pda` на FAT32-раздел EASYROMS карты (виден на ПК как диск).
2. Вставь карту в консоль, по SSH скопируй:

```sh
cp -r /roms/r36pda /home/ark/
```

---

## 3. Установка тулчейна (один раз, нужен интернет)

Эти шаги нужны **только для сборки** (`make` / `make package`).
Если ты ставишь через готовый порт с SD-карты — пропусти.

```sh
sudo apt-get update
sudo apt-get install --reinstall g++ libsdl2-dev make
```

Если apt ругается на репозитории — добавить их:

```sh
sudo apt-get install ca-certificates curl gnupg
```

---

## 4. Сборка

```sh
cd ~/r36pda
make
```

Должен появиться бинарник `r36pda`. Ошибки сборки — присылай, разберём.

### Собрать переносимый порт (для установки без интернета)

```sh
cd ~/r36pda
make package
```

Появится папка `r36pda-port/r36pda/`. Скопируй её содержимое на SD-карту
в `/roms/ports/` (см. шаг «Самый простой путь» выше) — и она запустится
с любого другого R36S без сети и компиляции.

---

## 5. Запуск

Сначала выйди из EmulationStation, чтобы не было конфликта за экран:

```sh
sudo systemctl stop emulationstation    # остановить
```

Затем запусти лаунчер:

```sh
cd ~/r36pda && ./r36pda
```

Полный экран (на всякий случай):

```sh
R36PDA_FULLSCREEN=1 ./r36pda
```

**Если ничего не отображается**, запусти с диагностикой:

```sh
sudo SDL_VIDEODRIVER=fbcon SDL_FBDEV=/dev/fb0 ./r36pda
```

---

## 6. Управление в лаунчере

| Действие | Джойстик | Клавиатура |
|---|---|---|
| Движение | D-Pad / стики | стрелки / WASD |
| Запустить | A или B | Enter |
| Вернуться в эмулятор | — | F1 |
| Выход | Fn+Start | Esc |

Маппинг кнопок R36S (из README dov/r36s-programming):
0=B, 1=A, 2=X, 3=Y, 8..11=D-Pad, 12=Select, 13=Start, 16=Fn.

---

## 7. Настройка приложений

### Терминал

Уже настроен: ищет `st`, затем `fbterm`. Если нет — поставь:

```sh
sudo apt-get install stterm      # или: libsdl2-ttf-dev + fbterm
```

### Ардуино

```sh
sudo apt-get install arduino-cli
arduino-cli core install arduino:avr
# права на USB-порт ардуинки:
sudo usermod -a -G dialout ark
```

Проверка:

```sh
arduino-cli board listall | grep -i uno
arduino-cli compile ~/sketches/mysketch
arduino-cli upload -p /dev/ttyACM0 -b arduino:avr:uno ~/sketches/mysketch
```

Скетчи храни в `~/sketches` — иконка «Ардуино» их показывает.

### Игры

Иконка «Игры» возвращает в EmulationStation:

```sh
sudo systemctl enable emulationstation
```

После выхода из игр перезапусти лаунчер: `./r36pda`.

---

## 8. Автозапуск (необязательно)

Чтобы при загрузке консоль сразу показывала КПК-десктоп:

```sh
sudo nano /etc/rc.local
# добавить перед exit 0:
#   sudo systemctl stop emulationstation
#   su - ark -c "cd /home/ark/r36pda && ./r36pda" &
```

Вернуться к играм по умолчанию — убрать строки или `sudo systemctl enable emulationstation`.

---

## 9. Частые проблемы

| Проблема | Решение |
|---|---|
| Нет картинки, чёрный экран | остановить ES, проверить fbcon (`ls /dev/fb*`) |
| Джойстик не реагирует | `ls /dev/input/js*`; без геймпада работает клавиатура |
| SSH не подключается | заново Enable Remote Services, сменить пароль |
| apt не качает | проверить Wi-Fi, `sudo apt-get update` |
| Окно не на весь экран | `R36PDA_FULLSCREEN=1 ./r36pda` |
| Порт не виден в меню Ports | перезапустить ES (Options→Quit→Restart) |
| Порт запустился и сразу вернул в меню | права: `chmod +x /roms/ports/r36pda/r36pda.sh` |

---

## 10. Быстрый старт одной строкой (после шага 3)

```sh
cd ~/r36pda && make && sudo systemctl stop emulationstation && ./r36pda
```
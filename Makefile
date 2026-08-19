# r36pda — сборка на R36S (ArkOS) / Linux
#   sudo apt-get install --reinstall g++ libsdl2-dev   (один раз)
#   make && make install

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)
ifeq ($(strip $(SDL_LIBS)),)
  SDL_CFLAGS :=
  SDL_LIBS   := -lSDL2
endif

PREFIX ?= /home/ark
DEST   := $(PREFIX)/r36pda
PORT   := r36pda-port

SOURCES := src/main.cpp
BIN     := r36pda

.PHONY: all clean install package

all: $(BIN)

$(BIN): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -o $@ $(SOURCES) $(SDL_LIBS)

install: $(BIN)
	mkdir -p $(DEST)
	cp $(BIN) $(DEST)/
	cp config/apps.cfg $(DEST)/
	cp -r apps $(DEST)/
	@echo "Установлено в $(DEST). Запуск: $(DEST)/r36pda"

# Собирает самодостаточную папку-порт для копирования на SD-карту ArkOS.
# Куда класть на SD: /roms/ports/r36pda/
package: $(BIN)
	rm -rf $(PORT)
	mkdir -p $(PORT)/r36pda
	cp $(BIN) $(PORT)/r36pda/
	cp config/apps.cfg $(PORT)/r36pda/
	cp -r apps $(PORT)/r36pda/
	cp ports/r36pda.sh $(PORT)/r36pda/
	chmod +x $(PORT)/r36pda/r36pda.sh $(PORT)/r36pda/apps/*.sh
	@echo ""
	@echo "Готово: $(PORT)/"
	@echo "Скопируй содержимое $(PORT)/ в каталог /roms/ports/ на SD-карте:"
	@echo "  /roms/ports/r36pda/r36pda.sh"
	@echo "Затем в меню EmulationStation открой Ports -> r36pda"

clean:
	rm -f $(BIN)
	rm -rf $(PORT)
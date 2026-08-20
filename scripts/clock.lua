-- Скриптовое приложение для r36pda: часы.
-- frame() вызывается каждый кадр. r36.print выводит строку.

local h = 12
local m = 0

function frame()
    local t = r36.now()
    r36.cls()
    r36.text(60, 120, t, 8, 255, 220, 60)
    r36.text(60, 260, "Часы", 2, 150, 200, 255)
    r36.text(60, 290, "A/стрелки: нет  B: в меню", 2, 120, 120, 140)

    local b = r36.button()
    if b == "b" then r36.exit() end
end
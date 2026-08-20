-- Демо: системная информация через r36.run и r36.list.
-- Показывает первые строки из /proc/cpuinfo и список файлов в текущей папке.

function frame()
    r36.cls()
    local out = r36.run("cat /proc/cpuinfo | head -3")
    r36.text(20, 30, "=== CPU ===", 2, 255, 220, 60)
    for i, s in ipairs(out) do
        r36.text(20, 50 + (i - 1) * 18, s, 2, 220, 220, 220)
    end
    r36.text(20, 120, "=== Файлы ===", 2, 255, 220, 60)
    local files = r36.list(".")
    for i, s in ipairs(files) do
        r36.text(20, 140 + (i - 1) * 18, tostring(i) .. ". " .. tostring(s), 2, 180, 180, 180)
        if i >= 12 then break end
    end
    local b = r36.button()
    if b == "b" then r36.exit() end
end
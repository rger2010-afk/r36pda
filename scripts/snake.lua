-- Игра "змейка" на Lua: стрелки двигают, A - старт/пауза, B - выход.
-- Поле 20x15, клетка 20px.

local w, h = 20, 15
local cell = 24
local ox, oy = 60, 60
local snake, dir, food, score, gameover, running

function reset()
    snake = {{x = 5, y = 7}, {x = 4, y = 7}, {x = 3, y = 7}}
    dir = {x = 1, y = 0}
    score = 0
    gameover = false
    running = true
    food = {x = math.random(0, w - 1), y = math.random(0, h - 1)}
end

reset()

function frame()
    local b = r36.button()
    if b == "up" and dir.y == 0 then dir = {x = 0, y = -1}
    elseif b == "down" and dir.y == 0 then dir = {x = 0, y = 1}
    elseif b == "left" and dir.x == 0 then dir = {x = -1, y = 0}
    elseif b == "right" and dir.x == 0 then dir = {x = 1, y = 0}
    elseif b == "b" then r36.exit()
    elseif b == "a" and gameover then reset()
    end

    r36.cls()

    if not gameover then
        local head = {x = snake[1].x + dir.x, y = snake[1].y + dir.y}
        if head.x < 0 or head.x >= w or head.y < 0 or head.y >= h then
            gameover = true
        else
            for _, s in ipairs(snake) do
                if s.x == head.x and s.y == head.y then gameover = true end
            end
        end
        if not gameover then
            table.insert(snake, 1, head)
            if head.x == food.x and head.y == food.y then
                score = score + 1
                food = {x = math.random(0, w - 1), y = math.random(0, h - 1)}
            else
                table.remove(snake)
            end
        end
    end

    -- поле
    r36.rect(ox - 4, oy - 4, w * cell + 8, h * cell + 8, 40, 40, 60)
    r36.rect(ox, oy, w * cell, h * cell, 10, 10, 16)
    -- еда
    r36.rect(ox + food.x * cell, oy + food.y * cell, cell, cell, 255, 60, 60)
    -- змейка
    for _, s in ipairs(snake) do
        r36.rect(ox + s.x * cell, oy + s.y * cell, cell - 2, cell - 2, 80, 220, 80)
    end
    r36.text(20, 24, "Змейка  очки: " .. tostring(score), 2, 255, 220, 60)
    if gameover then
        r36.text(200, 210, "GAME OVER", 4, 255, 80, 80)
        r36.text(200, 260, "A: заново  B: выход", 2, 200, 200, 200)
    end
    r36.delay(6)
end
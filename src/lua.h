// lua.h — встроенный Lua 5.3 для своих приложений.
// Скрипты лежат в scripts/*.lua и появляются на рабочем столе.
// Сборка: g++ ... -llua5.3 -lm -ldl  (линкуется статически в CI)
#pragma once
#include "files.h"
#include "app.h"

#ifdef USE_LUA
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#endif

struct LuaState {
    std::vector<std::string> out;       // вывод (print) для экрана
    std::vector<std::string> keys;      // очередь нажатий
    int cx = 320, cy = 240;             // курсор
    bool running = true;
    int delay = 0;
#ifdef USE_LUA
    lua_State *L = nullptr;
#endif
};

static LuaState g_lua;

#ifdef USE_LUA
// ---- Lua API ----
static int l_print(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    if (g_lua.out.size() > 100) g_lua.out.erase(g_lua.out.begin());
    g_lua.out.push_back(s ? s : "");
    return 0;
}
static int l_cls(lua_State *L) { g_lua.out.clear(); return 0; }
static int l_now(lua_State *L) {
    char b[16]; read_clock(b, sizeof b);
    lua_pushstring(L, b);
    return 1;
}
static int l_button(lua_State *L) {
    if (g_lua.keys.empty()) lua_pushnil(L);
    else { lua_pushstring(L, g_lua.keys[0].c_str()); g_lua.keys.erase(g_lua.keys.begin()); }
    return 1;
}
static int l_keydown(lua_State *L) {
    const char *k = luaL_checkstring(L, 1);
    for (auto &s : g_lua.keys) if (s == k) { lua_pushboolean(L, 1); return 1; }
    lua_pushboolean(L, 0);
    return 1;
}
static int l_cursor(lua_State *L) {
    lua_pushnumber(L, g_lua.cx);
    lua_pushnumber(L, g_lua.cy);
    return 2;
}
static int l_rect(lua_State *L) {
    int x = (int)luaL_checknumber(L, 1);
    int y = (int)luaL_checknumber(L, 2);
    int w = (int)luaL_checknumber(L, 3);
    int h = (int)luaL_checknumber(L, 4);
    Uint8 r = (Uint8)luaL_checknumber(L, 5);
    Uint8 g = (Uint8)luaL_checknumber(L, 6);
    Uint8 b = (Uint8)luaL_checknumber(L, 7);
    extern Renderer *g_R;
    fillrect(*g_R, x, y, w, h, ::rgb(r, g, b));
    return 0;
}
static int l_text(lua_State *L) {
    int x = (int)luaL_checknumber(L, 1);
    int y = (int)luaL_checknumber(L, 2);
    const char *s = luaL_checkstring(L, 3);
    int sc = (int)luaL_optnumber(L, 4, 2);
    Uint8 r = (Uint8)luaL_optnumber(L, 5, 255);
    Uint8 g = (Uint8)luaL_optnumber(L, 6, 255);
    Uint8 b = (Uint8)luaL_optnumber(L, 7, 255);
    extern Renderer *g_R;
    drawtext(*g_R, s, x, y, sc, ::rgb(r, g, b));
    return 0;
}
static int l_run(lua_State *L) {
    const char *cmd = luaL_checkstring(L, 1);
    std::vector<std::string> out;
    run_cmd_lines(cmd, out);
    lua_newtable(L);
    for (size_t i = 0; i < out.size(); ++i) {
        lua_pushinteger(L, (lua_Integer)(i + 1));
        lua_pushstring(L, out[i].c_str());
        lua_settable(L, -3);
    }
    return 1;
}
static int l_exit(lua_State *L) { g_lua.running = false; return 0; }
static int l_delay(lua_State *L) { g_lua.delay = (int)luaL_optnumber(L, 1, 0); return 0; }
static int l_list(lua_State *L) {
    const char *dir = luaL_optstring(L, 1, ".");
    lua_newtable(L);
    int idx = 1;
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            lua_pushinteger(L, idx++);
            lua_pushstring(L, e->d_name);
            lua_settable(L, -3);
        }
        closedir(d);
    }
    return 1;
}

static const luaL_Reg r36lib[] = {
    {"print", l_print}, {"cls", l_cls}, {"now", l_now},
    {"button", l_button}, {"keydown", l_keydown}, {"cursor", l_cursor},
    {"rect", l_rect}, {"text", l_text}, {"run", l_run},
    {"exit", l_exit}, {"delay", l_delay}, {"list", l_list},
    {nullptr, nullptr}
};

static void lua_init() {
    g_lua.L = luaL_newstate();
    luaL_openlibs(g_lua.L);
    luaL_newlib(g_lua.L, r36lib);
    lua_setglobal(g_lua.L, "r36");
}

// загрузить скрипт из файла
static bool lua_load_file(const std::string &path) {
    if (!g_lua.L) lua_init();
    g_lua.out.clear();
    g_lua.running = true;
    if (luaL_loadfile(g_lua.L, path.c_str()) != LUA_OK) {
        g_lua.out.push_back("lua error: " + std::string(lua_tostring(g_lua.L, -1)));
        lua_pop(g_lua.L, 1);
        return false;
    }
    if (lua_pcall(g_lua.L, 0, 0, 0) != LUA_OK) {
        g_lua.out.push_back("lua: " + std::string(lua_tostring(g_lua.L, -1)));
        lua_pop(g_lua.L, 1);
        return false;
    }
    return true;
}

static void lua_frame() {
    if (!g_lua.L) return;
    if (g_lua.delay > 0) { --g_lua.delay; return; }
    lua_getglobal(g_lua.L, "frame");
    if (lua_isfunction(g_lua.L, -1)) {
        if (lua_pcall(g_lua.L, 0, 0, 0) != LUA_OK) {
            g_lua.out.push_back("lua: " + std::string(lua_tostring(g_lua.L, -1)));
            lua_pop(g_lua.L, 1);
            g_lua.running = false;
        }
    } else lua_pop(g_lua.L, 1);
}

static std::vector<App> lua_apps() {
    std::vector<App> out;
    DIR *d = opendir("scripts");
    if (!d) return out;
    struct dirent *e;
    while ((e = readdir(d))) {
        std::string n = e->d_name;
        if (n.size() < 5 || n.substr(n.size() - 4) != ".lua") continue;
        App a;
        a.name = n.substr(0, n.size() - 4);
        a.mode = M_SCRIPT;
        a.script = "scripts/" + n;
        a.color[0] = 150; a.color[1] = 200; a.color[2] = 255;
        out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](const App &a, const App &b) { return a.name < b.name; });
    return out;
}
#endif // USE_LUA
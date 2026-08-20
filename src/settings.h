// settings.h — настройки: клавиши, размеры, лог. Хранятся в config/settings.cfg
#pragma once
#include "ui.h"

enum Btn { BTN_A, BTN_B, BTN_START, BTN_SELECT, BTN_FN,
           BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_MAX };
static const char *BTN_NAME[BTN_MAX] = {
    "A", "B", "Start", "Select", "Fn", "Up", "Down", "Left", "Right" };

struct Settings {
    // маппинг кнопок геймпада (номер кнопки SDL)
    int btn[BTN_MAX];
    // оси стика для курсора: ось X, ось Y, порог
    int axis_x = 0, axis_y = 1, axis_thresh = 10000;
    int axis_scale = 6;          // пикселей на условную единицу смещения
    int cursor_speed = 3;        // множитель скорости курсора
    int cur_r = 255, cur_g = 255, cur_b = 0;   // цвет курсора
    int icon_size = 84;          // размер иконки
    int kb_scale = 2;            // масштаб шрифта клавиатуры
    bool log_enabled = false;    // писать debug.log
    bool ui = false;

    Settings() { defaults(); }
    void defaults() {
        btn[BTN_A]=1; btn[BTN_B]=0; btn[BTN_START]=13; btn[BTN_SELECT]=12; btn[BTN_FN]=16;
        btn[BTN_UP]=8; btn[BTN_DOWN]=9; btn[BTN_LEFT]=10; btn[BTN_RIGHT]=11;
        axis_x=0; axis_y=1; axis_thresh=10000; axis_scale=6;
        cursor_speed=3; cur_r=255; cur_g=255; cur_b=0;
        icon_size=84; kb_scale=2; log_enabled=false;
    }
    std::string find_path() {
        for (const char *p : { "config/settings.cfg", "settings.cfg" })
            if (FILE *f = fopen(p, "r")) { fclose(f); return p; }
        return "config/settings.cfg";
    }
    void save() {
        std::string p = find_path();
        FILE *f = fopen(p.c_str(), "w");
        if (!f) return;
        fprintf(f, "# r36pda settings\n");
        for (int i = 0; i < BTN_MAX; ++i)
            fprintf(f, "btn.%s=%d\n", BTN_NAME[i], btn[i]);
        fprintf(f, "axis.x=%d\n", axis_x);
        fprintf(f, "axis.y=%d\n", axis_y);
        fprintf(f, "axis.thresh=%d\n", axis_thresh);
        fprintf(f, "axis.scale=%d\n", axis_scale);
        fprintf(f, "cursor.speed=%d\n", cursor_speed);
        fprintf(f, "cursor.r=%d\n", cur_r);
        fprintf(f, "cursor.g=%d\n", cur_g);
        fprintf(f, "cursor.b=%d\n", cur_b);
        fprintf(f, "icon.size=%d\n", icon_size);
        fprintf(f, "kb.scale=%d\n", kb_scale);
        fprintf(f, "log=%d\n", log_enabled ? 1 : 0);
        fclose(f);
    }
    void load() {
        defaults();
        std::string p = find_path();
        FILE *f = fopen(p.c_str(), "r");
        if (!f) return;
        char line[256];
        while (fgets(line, sizeof line, f)) {
            if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
            char k[64] = {0}, v[128] = {0};
            if (sscanf(line, "%63[^=]=%127s", k, v) != 2) continue;
            int iv = atoi(v);
            for (int i = 0; i < BTN_MAX; ++i) {
                char kk[64];
                snprintf(kk, sizeof kk, "btn.%s", BTN_NAME[i]);
                if (strcmp(k, kk) == 0) { btn[i] = iv; break; }
            }
            if (!strcmp(k, "axis.x")) axis_x = iv;
            else if (!strcmp(k, "axis.y")) axis_y = iv;
            else if (!strcmp(k, "axis.thresh")) axis_thresh = iv;
            else if (!strcmp(k, "axis.scale")) axis_scale = iv;
            else if (!strcmp(k, "cursor.speed")) cursor_speed = iv;
            else if (!strcmp(k, "cursor.r")) cur_r = iv;
            else if (!strcmp(k, "cursor.g")) cur_g = iv;
            else if (!strcmp(k, "cursor.b")) cur_b = iv;
            else if (!strcmp(k, "icon.size")) icon_size = iv;
            else if (!strcmp(k, "kb.scale")) kb_scale = iv;
            else if (!strcmp(k, "log")) log_enabled = iv != 0;
        }
        fclose(f);
        if (icon_size < 48) icon_size = 48;
        if (icon_size > 140) icon_size = 140;
        if (kb_scale < 1) kb_scale = 1;
        if (kb_scale > 3) kb_scale = 3;
    }
    // полный сброс к заводским настройкам и сохранение
    void reset() { defaults(); save(); }
    // лог событий (в debug.log рядом с бинарником)
    void log(const char *fmt, ...) {
        if (!log_enabled) return;
        static FILE *lf = nullptr;
        if (!lf) {
            lf = fopen("debug.log", "a");
            if (!lf) return;
        }
        va_list ap;
        va_start(ap, fmt);
        char b[512];
        vsnprintf(b, sizeof b, fmt, ap);
        va_end(ap);
        time_t t = time(nullptr);
        struct tm tmv;
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char ts[24];
        strftime(ts, sizeof ts, "%H:%M:%S", &tmv);
        fprintf(lf, "[%s] %s\n", ts, b);
        fflush(lf);
    }
};
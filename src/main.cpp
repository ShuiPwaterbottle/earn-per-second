// ============================================================================
// EarnPerSecond — 每秒工资计算器  (v1.4 全自绘版)
// ----------------------------------------------------------------------------
// 两页式 Win32 GUI 应用，**完全自绘（owner-draw）**：
//   第一页（设置页）：填写月薪/日薪/时薪与工作时间，预览每秒工资；
//   第二页（计时页）：点击「开始上班」后每秒刷新已赚到的钱（高精度计时）。
//
// 为什么全自绘：
//   实测发现部分 Windows 环境（主题/视觉样式异常、特定会话）下，标准子控件的
//   文字渲染会失效 —— 单选按钮文字、STATIC 标签、EDIT 输入内容显示为空白，
//   而按钮/分组框文字正常。为彻底绕开主题渲染，本版不创建任何 STATIC/BUTTON/
//   EDIT 子控件，全部界面（含文字、按钮、单选、输入框）都在主窗口 WM_PAINT
//   中用最基本的 GDI 文本 API（DrawText/SetTextColor）绘制，任何环境下都
//   必定完整渲染。
//
// 技术要点：
//   - 高精度计时 QueryPerformanceCounter，1 秒定时器刷新；
//   - 配置持久化 salary.ini（exe 目录优先，其次 %APPDATA%）；
//   - 输入框仅接受数字与小数点（无需 IME）；
//   - DPI-unaware：由系统统一缩放，避免 DPI-aware 在缩放显示下的渲染问题。
//
// 编译（MinGW-w64）：
//   g++ -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ \
//       src/main.cpp -o build/EarnPerSecond.exe -luser32 -lgdi32 -lshell32
// ============================================================================

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

// ---------------------------------------------------------------------------
// 编码辅助（窄字符串 = UTF-8）
// ---------------------------------------------------------------------------
static std::wstring wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// ---------------------------------------------------------------------------
// 布局（客户端坐标，490x610 设计尺寸）
// ---------------------------------------------------------------------------
struct Rect { int x, y, w, h; };

// 第一页（设置页）
static const Rect R_TITLE      = {10, 18, 470, 36};
static const Rect R_SUBTITLE   = {10, 58, 470, 18};
static const Rect R_GROUP1     = {10, 86, 470, 158};
static const Rect R_RADIO[3]   = {{30, 108, 64, 22}, {98, 108, 64, 22}, {166, 108, 64, 22}};
static const Rect R_LABEL_AMT  = {30, 140, 80, 20};
static const Rect R_INPUT_AMT  = {110, 138, 130, 24};
static const Rect R_LABEL_HRS  = {250, 140, 90, 20};
static const Rect R_INPUT_HRS  = {340, 138, 60, 24};
static const Rect R_LABEL_DAYS = {30, 172, 90, 20};
static const Rect R_INPUT_DAYS = {120, 170, 60, 24};
static const Rect R_RATE       = {30, 204, 300, 20};
static const Rect R_BTN_SAVE   = {345, 200, 110, 28};
static const Rect R_GROUP2     = {10, 254, 470, 118};
static const Rect R_USAGE[4]   = {{30, 274, 440, 18}, {30, 296, 440, 18}, {30, 318, 440, 18}, {30, 340, 440, 18}};
static const Rect R_TOTAL      = {30, 392, 300, 20};
static const Rect R_BTN_RESET  = {345, 388, 110, 28};
static const Rect R_BTN_START  = {105, 432, 280, 46};
static const Rect R_FOOTER     = {10, 580, 470, 18};

// 第二页（计时页）
static const Rect R_WORKGRP    = {10, 20, 470, 400};
static const Rect R_MONEY      = {25, 70, 440, 110};
static const Rect R_WRATE      = {25, 196, 440, 20};
static const Rect R_WTIME      = {25, 226, 440, 20};
static const Rect R_BTN_PAUSE  = {60, 290, 110, 40};
static const Rect R_BTN_STOP   = {190, 290, 110, 40};
static const Rect R_BTN_BACK   = {320, 290, 110, 40};

enum Page { PAGE_SETTINGS = 1, PAGE_WORK = 2 };
enum WorkState { ST_IDLE = 0, ST_WORKING, ST_PAUSED };
enum Field { FIELD_NONE = -1, FIELD_AMT = 0, FIELD_HRS, FIELD_DAYS };

// ---------------------------------------------------------------------------
// 应用状态
// ---------------------------------------------------------------------------
struct Config {
    int    type        = 0;      // 0=月薪 1=日薪 2=时薪
    double amount      = 8000.0;
    double hours       = 8.0;
    double days        = 5.0;
    double accumulated = 0.0;
};

static Config    g_cfg;
static int       g_page = PAGE_SETTINGS;
static WorkState g_state = ST_IDLE;
static double    g_rate = 0.0;
static double    g_lastEarned = 0.0, g_lastElapsed = 0.0;
static LARGE_INTEGER g_freq, g_sessionStart, g_pauseStart;
static double    g_pausedTotal = 0.0;
static HWND      g_hwnd = nullptr;
static HFONT     g_fontBig = nullptr, g_fontTitle = nullptr, g_fontNormal = nullptr, g_fontSmall = nullptr;

static std::string g_inputText[3];  // 输入框内容（金额/小时/天数）
static int g_focusField = FIELD_NONE;

// ---------------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------------
static double nowSec() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)g_freq.QuadPart;
}

static double sessionElapsedSec() {
    if (g_state == ST_IDLE) return 0.0;
    double start = (double)g_sessionStart.QuadPart / (double)g_freq.QuadPart;
    double e = nowSec() - start - g_pausedTotal;
    return e < 0.0 ? 0.0 : e;
}

static double calcRate(const Config& c) {
    if (c.amount <= 0.0) return 0.0;
    if (c.type == 0) {
        double secs = c.hours * c.days * (52.0 / 12.0) * 3600.0;
        return secs > 0.0 ? c.amount / secs : 0.0;
    }
    if (c.type == 1) {
        double secs = c.hours * 3600.0;
        return secs > 0.0 ? c.amount / secs : 0.0;
    }
    return c.amount / 3600.0;
}

static std::string fmtMoney(double v) { char b[64]; snprintf(b, sizeof(b), "%.2f", v); return b; }
static std::string fmtRate(double v)  { char b[64]; snprintf(b, sizeof(b), "%.4f", v); return b; }
static std::string fmtTime(double sec) {
    long long s = (long long)(sec + 0.5);
    char b[64];
    snprintf(b, sizeof(b), "%02lld:%02lld:%02lld", s / 3600, (s % 3600) / 60, s % 60);
    return b;
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r' || s[b-1] == '\n')) b--;
    if (b - a >= 3 && (unsigned char)s[a] == 0xEF && (unsigned char)s[a+1] == 0xBB && (unsigned char)s[a+2] == 0xBF) a += 3;
    return s.substr(a, b - a);
}

// ---------------------------------------------------------------------------
// 配置读写
// ---------------------------------------------------------------------------
static std::wstring configPathW() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t pos = p.find_last_of(L'\\');
    std::wstring dir = (pos == std::wstring::npos) ? L"" : p.substr(0, pos + 1);
    std::wstring cand = dir + L"salary.ini";
    HANDLE h = CreateFileW(cand.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return cand; }
    wchar_t ap[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, ap))) {
        std::wstring dir2 = std::wstring(ap) + L"\\EarnPerSecond";
        CreateDirectoryW(dir2.c_str(), nullptr);
        return dir2 + L"\\salary.ini";
    }
    return cand;
}

static std::string readAllTextW(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    std::string out;
    char buf[4096];
    DWORD rd = 0;
    while (ReadFile(h, buf, sizeof(buf), &rd, nullptr) && rd > 0) out.append(buf, rd);
    CloseHandle(h);
    return out;
}

static bool writeAllTextW(const std::wstring& path, const std::string& text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, text.data(), (DWORD)text.size(), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == text.size();
}

static void loadConfig() {
    std::string data = readAllTextW(configPathW());
    size_t pos = 0;
    while (pos < data.size()) {
        size_t nl = data.find('\n', pos);
        std::string line = trim(data.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = (nl == std::string::npos) ? data.size() : nl + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k == "type")             g_cfg.type        = (int)atof(v.c_str());
        else if (k == "amount")      g_cfg.amount      = atof(v.c_str());
        else if (k == "hours")       g_cfg.hours       = atof(v.c_str());
        else if (k == "days")        g_cfg.days        = atof(v.c_str());
        else if (k == "accumulated") g_cfg.accumulated = atof(v.c_str());
    }
    if (g_cfg.amount <= 0) g_cfg.amount = 8000.0;
    if (g_cfg.hours  <= 0) g_cfg.hours  = 8.0;
    if (g_cfg.days   <= 0) g_cfg.days   = 5.0;
}

static void saveConfig() {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "[earn-per-second]\ntype=%d\namount=%.6f\nhours=%.6f\ndays=%.6f\naccumulated=%.6f\n",
             g_cfg.type, g_cfg.amount, g_cfg.hours, g_cfg.days, g_cfg.accumulated);
    writeAllTextW(configPathW(), buf);
}

// ---------------------------------------------------------------------------
// 状态派生
// ---------------------------------------------------------------------------
static void refreshConfigFromInputs() {
    g_cfg.amount = atof(g_inputText[FIELD_AMT].c_str());
    g_cfg.hours  = atof(g_inputText[FIELD_HRS].c_str());
    g_cfg.days   = atof(g_inputText[FIELD_DAYS].c_str());
}

static double previewRate() {
    Config c = g_cfg;
    c.amount = atof(g_inputText[FIELD_AMT].c_str());
    c.hours  = atof(g_inputText[FIELD_HRS].c_str());
    c.days   = atof(g_inputText[FIELD_DAYS].c_str());
    return calcRate(c);
}

// ---------------------------------------------------------------------------
// 会话控制
// ---------------------------------------------------------------------------
static void startSession() {
    refreshConfigFromInputs();
    if (g_cfg.amount <= 0 || g_cfg.hours <= 0 || g_cfg.days <= 0) {
        MessageBoxW(g_hwnd, L"请先填写正确的工资设置：\n金额、每天工作小时、每周工作天数都要大于 0。",
                    L"提示", MB_OK | MB_ICONWARNING);
        return;
    }
    g_rate = calcRate(g_cfg);
    if (g_rate <= 0.0) {
        MessageBoxW(g_hwnd, L"每秒工资为 0，请检查设置。", L"提示", MB_OK | MB_ICONWARNING);
        return;
    }
    g_state = ST_WORKING;
    g_pausedTotal = 0.0;
    g_lastEarned = 0.0;
    g_lastElapsed = 0.0;
    QueryPerformanceCounter(&g_sessionStart);
    SetTimer(g_hwnd, 1, 1000, nullptr);
    g_page = PAGE_WORK;
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

static void pauseSession() {
    g_state = ST_PAUSED;
    QueryPerformanceCounter(&g_pauseStart);
    KillTimer(g_hwnd, 1);
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

static void resumeSession() {
    g_pausedTotal += nowSec() - (double)g_pauseStart.QuadPart / (double)g_freq.QuadPart;
    g_state = ST_WORKING;
    SetTimer(g_hwnd, 1, 1000, nullptr);
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

static void stopSession(bool showDialog) {
    if (g_state == ST_IDLE) return;
    double elapsed = sessionElapsedSec();
    double earned  = g_rate * elapsed;
    g_lastEarned  = earned;
    g_lastElapsed = elapsed;
    g_cfg.accumulated += earned;
    g_state = ST_IDLE;
    g_pausedTotal = 0.0;
    KillTimer(g_hwnd, 1);
    saveConfig();
    if (showDialog) {
        std::string msg = "本次工作：" + fmtTime(elapsed) + "\n"
                        + "本次收入：¥" + fmtMoney(earned) + "\n"
                        + "累计收入：¥" + fmtMoney(g_cfg.accumulated) + "\n\n"
                        + "辛苦啦，下班快乐！";
        MessageBoxW(g_hwnd, wide(msg).c_str(), L"下班结算", MB_OK | MB_ICONINFORMATION);
    }
    g_page = PAGE_SETTINGS;
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

static void resetAccumulated() {
    int r = MessageBoxW(g_hwnd, L"确定要把累计收入清零吗？此操作不可撤销。",
                        L"重置累计收入", MB_YESNO | MB_ICONQUESTION);
    if (r == IDYES) {
        g_cfg.accumulated = 0.0;
        saveConfig();
        InvalidateRect(g_hwnd, nullptr, TRUE);
    }
}

// ---------------------------------------------------------------------------
// 自绘辅助
// ---------------------------------------------------------------------------
static void drawText(HDC hdc, const Rect& r, const std::string& text, HFONT font,
                     COLORREF color, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    HFONT old = (HFONT)SelectObject(hdc, font);
    RECT rc = {r.x, r.y, r.x + r.w, r.y + r.h};
    std::wstring w = wide(text);
    DrawTextW(hdc, w.c_str(), (int)w.size(), &rc, fmt);
    SelectObject(hdc, old);
}

static void drawBox(HDC hdc, const Rect& r, bool down = false, bool enabled = true) {
    RECT rc = {r.x, r.y, r.x + r.w, r.y + r.h};
    // 填充
    HBRUSH br = CreateSolidBrush(enabled ? (down ? RGB(200, 200, 200) : RGB(235, 235, 235)) : RGB(230, 230, 230));
    FillRect(hdc, &rc, br);
    DeleteObject(br);
    // 边框
    HBRUSH eb = CreateSolidBrush(RGB(140, 140, 140));
    FrameRect(hdc, &rc, eb);
    DeleteObject(eb);
}

static void drawGroup(HDC hdc, const Rect& r, const std::string& title, HFONT font) {
    RECT rc = {r.x, r.y, r.x + r.w, r.y + r.h};
    HBRUSH eb = CreateSolidBrush(RGB(150, 150, 150));
    FrameRect(hdc, &rc, eb);
    DeleteObject(eb);
    // 标题（左上角，盖住边框线）
    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, RGB(240, 240, 240));
    SetTextColor(hdc, RGB(40, 40, 40));
    HFONT old = (HFONT)SelectObject(hdc, font);
    RECT tr = {r.x + 10, r.y - 8, r.x + 180, r.y + 12};
    std::wstring w = wide(title);
    DrawTextW(hdc, w.c_str(), (int)w.size(), &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, old);
    SetBkMode(hdc, TRANSPARENT);
}

static void drawInput(HDC hdc, const Rect& r, const std::string& text, bool focused) {
    RECT rc = {r.x, r.y, r.x + r.w, r.y + r.h};
    HBRUSH br = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc, br);
    DeleteObject(br);
    HBRUSH eb = CreateSolidBrush(focused ? RGB(0, 90, 180) : RGB(120, 120, 120));
    FrameRect(hdc, &rc, eb);
    DeleteObject(eb);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));
    HFONT old = (HFONT)SelectObject(hdc, g_fontNormal);
    RECT tr = {r.x + 4, r.y, r.x + r.w - 4, r.y + r.h};
    std::wstring w = wide(text);
    DrawTextW(hdc, w.c_str(), (int)w.size(), &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (focused) {
        // 简单光标
        int tw = 0;
        SIZE sz;
        GetTextExtentPoint32W(hdc, w.c_str(), (int)w.size(), &sz);
        tw = sz.cx;
        RECT cr = {r.x + 4 + tw, r.y + 3, r.x + 4 + tw + 1, r.y + r.h - 3};
        HBRUSH cb = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdc, &cr, cb);
        DeleteObject(cb);
    }
    SelectObject(hdc, old);
}

static void drawRadio(HDC hdc, const Rect& r, const std::string& text, bool checked) {
    // 圆圈
    int cy = r.y + r.h / 2;
    HBRUSH ob = CreateSolidBrush(checked ? RGB(0, 90, 180) : RGB(255, 255, 255));
    HBRUSH nb = CreateSolidBrush(RGB(120, 120, 120));
    SelectObject(hdc, ob);
    SelectObject(hdc, nb);
    Ellipse(hdc, r.x, cy - 8, r.x + 16, cy + 8);
    if (checked) {
        HBRUSH dot = CreateSolidBrush(RGB(255, 255, 255));
        SelectObject(hdc, dot);
        Ellipse(hdc, r.x + 4, cy - 4, r.x + 12, cy + 4);
        DeleteObject(dot);
    }
    DeleteObject(ob);
    DeleteObject(nb);
    Rect tr = {r.x + 20, r.y, r.w - 20, r.h};
    drawText(hdc, tr, text, g_fontNormal, RGB(30, 30, 30), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

// 判断点是否在矩形内（客户端坐标）
static bool inRect(const Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// ---------------------------------------------------------------------------
// WM_PAINT 全自绘
// ---------------------------------------------------------------------------
static void paintUI(HDC hdc) {
    // 背景
    RECT cr;
    GetClientRect(g_hwnd, &cr);
    HBRUSH bg = CreateSolidBrush(RGB(240, 240, 240));
    FillRect(hdc, &cr, bg);
    DeleteObject(bg);

    if (g_page == PAGE_SETTINGS) {
        drawText(hdc, R_TITLE, "每秒工资计算器", g_fontTitle, RGB(30, 30, 30), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(hdc, R_SUBTITLE, "设置你的工资，然后开始见证「时间就是金钱」", g_fontSmall, RGB(90, 90, 90), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        drawGroup(hdc, R_GROUP1, "工资设置", g_fontNormal);
        for (int i = 0; i < 3; i++)
            drawRadio(hdc, R_RADIO[i], i == 0 ? "月薪" : (i == 1 ? "日薪" : "时薪"), g_cfg.type == i);

        drawText(hdc, R_LABEL_AMT, "金额（元）：", g_fontNormal, RGB(30, 30, 30));
        drawInput(hdc, R_INPUT_AMT, g_inputText[FIELD_AMT], g_focusField == FIELD_AMT);
        drawText(hdc, R_LABEL_HRS, "每天工作(小时)：", g_fontNormal, RGB(30, 30, 30));
        drawInput(hdc, R_INPUT_HRS, g_inputText[FIELD_HRS], g_focusField == FIELD_HRS);
        drawText(hdc, R_LABEL_DAYS, "每周工作(天)：", g_fontNormal, RGB(30, 30, 30));
        drawInput(hdc, R_INPUT_DAYS, g_inputText[FIELD_DAYS], g_focusField == FIELD_DAYS);

        double pr = previewRate();
        drawText(hdc, R_RATE, "每秒工资 ≈ ¥" + fmtRate(pr) + "　｜　每小时 ≈ ¥" + fmtMoney(pr * 3600.0),
                 g_fontNormal, RGB(0, 90, 180));

        drawBox(hdc, R_BTN_SAVE, false, true);
        drawText(hdc, R_BTN_SAVE, "保存设置", g_fontNormal, RGB(20, 20, 20), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        drawGroup(hdc, R_GROUP2, "使用说明", g_fontNormal);
        const char* usage[4] = {
            "① 填写工资设置，点击「保存设置」",
            "② 点击「开始上班」进入计时页，每秒自动刷新已赚到的钱",
            "③ 计时页可「暂停 / 继续」；「下班结算」汇总本次收入并计入累计",
            "④ 设置与累计收入自动保存到 salary.ini，下次启动自动恢复",
        };
        for (int i = 0; i < 4; i++)
            drawText(hdc, R_USAGE[i], usage[i], g_fontSmall, RGB(60, 60, 60));

        drawText(hdc, R_TOTAL, "累计收入：¥" + fmtMoney(g_cfg.accumulated), g_fontNormal, RGB(30, 30, 30));
        bool resetEnabled = g_cfg.accumulated > 0.0;
        drawBox(hdc, R_BTN_RESET, false, resetEnabled);
        drawText(hdc, R_BTN_RESET, "重置累计", g_fontNormal, resetEnabled ? RGB(20, 20, 20) : RGB(150, 150, 150),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        drawBox(hdc, R_BTN_START, false, true);
        drawText(hdc, R_BTN_START, "开始上班 →", g_fontTitle, RGB(0, 90, 180), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        drawGroup(hdc, R_WORKGRP, "上班计时", g_fontNormal);

        double earned = (g_state == ST_IDLE) ? g_lastEarned : g_rate * sessionElapsedSec();
        drawText(hdc, R_MONEY, "¥" + fmtMoney(earned), g_fontBig, RGB(20, 20, 20), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(hdc, R_WRATE, "每秒 ¥" + fmtRate(g_rate), g_fontNormal, RGB(60, 60, 60), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        std::string timeText;
        if (g_state == ST_IDLE) {
            timeText = g_lastElapsed > 0.0
                ? "已下班 · 本次工作 " + fmtTime(g_lastElapsed) + "，收入 ¥" + fmtMoney(g_lastEarned)
                : "已工作 00:00:00";
        } else {
            timeText = (g_state == ST_PAUSED ? "已暂停 · 累计工作 " : "已工作 ") + fmtTime(sessionElapsedSec());
        }
        drawText(hdc, R_WTIME, timeText, g_fontNormal, RGB(60, 60, 60), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        bool working = (g_state == ST_WORKING);
        const char* pauseText = (g_state == ST_PAUSED) ? "继续上班" : "暂停";
        drawBox(hdc, R_BTN_PAUSE, false, working);
        drawText(hdc, R_BTN_PAUSE, pauseText, g_fontNormal, working ? RGB(20, 20, 20) : RGB(150, 150, 150),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawBox(hdc, R_BTN_STOP, false, true);
        drawText(hdc, R_BTN_STOP, "下班结算", g_fontNormal, RGB(20, 20, 20), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawBox(hdc, R_BTN_BACK, false, true);
        drawText(hdc, R_BTN_BACK, "返回设置", g_fontNormal, RGB(20, 20, 20), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    drawText(hdc, R_FOOTER, "EarnPerSecond v1.4.0 · MIT License · 开源项目", g_fontSmall, RGB(120, 120, 120),
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ---------------------------------------------------------------------------
// 鼠标/键盘交互
// ---------------------------------------------------------------------------
static void handleClick(int x, int y) {
    if (g_page == PAGE_SETTINGS) {
        for (int i = 0; i < 3; i++) {
            if (inRect(R_RADIO[i], x, y)) {
                g_cfg.type = i;
                InvalidateRect(g_hwnd, nullptr, TRUE);
                return;
            }
        }
        if (inRect(R_INPUT_AMT, x, y)) { g_focusField = FIELD_AMT; InvalidateRect(g_hwnd, nullptr, TRUE); return; }
        if (inRect(R_INPUT_HRS, x, y)) { g_focusField = FIELD_HRS; InvalidateRect(g_hwnd, nullptr, TRUE); return; }
        if (inRect(R_INPUT_DAYS, x, y)) { g_focusField = FIELD_DAYS; InvalidateRect(g_hwnd, nullptr, TRUE); return; }
        g_focusField = FIELD_NONE;
        if (inRect(R_BTN_SAVE, x, y)) {
            refreshConfigFromInputs();
            if (g_cfg.amount <= 0 || g_cfg.hours <= 0 || g_cfg.days <= 0) {
                MessageBoxW(g_hwnd, L"设置无效：金额、每天工作小时、每周工作天数都要大于 0。",
                            L"提示", MB_OK | MB_ICONWARNING);
            } else {
                saveConfig();
                MessageBoxW(g_hwnd, L"设置已保存到 salary.ini。", L"提示", MB_OK | MB_ICONINFORMATION);
            }
            return;
        }
        if (inRect(R_BTN_RESET, x, y) && g_cfg.accumulated > 0.0) { resetAccumulated(); return; }
        if (inRect(R_BTN_START, x, y)) { startSession(); return; }
        InvalidateRect(g_hwnd, nullptr, TRUE);
    } else {
        if (inRect(R_BTN_PAUSE, x, y)) {
            if (g_state == ST_WORKING) pauseSession();
            else if (g_state == ST_PAUSED) resumeSession();
            return;
        }
        if (inRect(R_BTN_STOP, x, y)) { stopSession(true); return; }
        if (inRect(R_BTN_BACK, x, y)) { stopSession(false); return; }
    }
}

static void handleKey(WPARAM ch) {
    if (g_focusField == FIELD_NONE) return;
    std::string& s = g_inputText[g_focusField];
    if (ch == 0x08) { // Backspace
        if (!s.empty()) s.pop_back();
    } else if (ch == 0x0D || ch == 0x1B) { // Enter/Esc -> 取消聚焦
        g_focusField = FIELD_NONE;
    } else if (ch >= '0' && ch <= '9') {
        if (s.size() < 12) s.push_back((char)ch);
    } else if (ch == '.') {
        if (s.find('.') == std::string::npos && s.size() < 12) s.push_back('.');
    } else if (ch == 0x2E) { // Delete? 忽略
    }
    refreshConfigFromInputs();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

// ---------------------------------------------------------------------------
// 窗口过程
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        g_fontBig    = CreateFontW(-44, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
        g_fontTitle  = CreateFontW(-22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
        g_fontNormal = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
        g_fontSmall  = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
        loadConfig();
        g_inputText[FIELD_AMT] = fmtMoney(g_cfg.amount);
        g_inputText[FIELD_HRS] = fmtMoney(g_cfg.hours);
        g_inputText[FIELD_DAYS] = fmtMoney(g_cfg.days);
        g_page = PAGE_SETTINGS;
        g_state = ST_IDLE;
        SetWindowTextW(hwnd, L"每秒工资 · Earn Per Second");
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paintUI(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return TRUE; // 背景在 WM_PAINT 中绘制，避免闪烁
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        handleClick(x, y);
        return 0;
    }
    case WM_CHAR: {
        handleKey(wp);
        return 0;
    }
    case WM_TIMER:
        if (wp == 1 && g_state == ST_WORKING) {
            double earned = g_rate * sessionElapsedSec();
            SetWindowTextW(hwnd, wide("已赚 ¥" + fmtMoney(earned) + " · 每秒工资").c_str());
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 506;
        mmi->ptMinTrackSize.y = 649;
        mmi->ptMaxTrackSize.x = 506;
        mmi->ptMaxTrackSize.y = 649;
        return 0;
    }
    case WM_DESTROY:
        if (g_state != ST_IDLE) stopSession(false);
        saveConfig();
        if (g_fontBig) DeleteObject(g_fontBig);
        if (g_fontTitle) DeleteObject(g_fontTitle);
        if (g_fontNormal) DeleteObject(g_fontNormal);
        if (g_fontSmall) DeleteObject(g_fontSmall);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    // DPI-unaware（见文件头说明）
    QueryPerformanceFrequency(&g_freq);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"EarnPerSecondWnd";
    wc.hIconSm       = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = {0, 0, 490, 610};
    AdjustWindowRect(&rc, style, FALSE);
    HWND hwnd = CreateWindowExW(0, L"EarnPerSecondWnd", L"每秒工资 · Earn Per Second",
                                style, CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "mpv_dynamic.h"

#include "resource.h"

#define ID_LISTBOX      100
#define ID_BTN_ADD      101
#define ID_BTN_DEL      102
#define ID_BTN_PLAY     103

#define WM_MPV_EVENT    (WM_USER + 1)

typedef struct {
    wchar_t *name;
    wchar_t *url;
} FavoriteItem;

typedef struct {
    mpv_handle *mpv;
    HWND hWnd;
    HWND hVideo;
    BOOL sizeAdjusted;
    BOOL isFullscreen;
    LONG_PTR savedStyle;
    WINDOWPLACEMENT savedPlacement;
    char errLog[8192];
    size_t errLogLen;
} PlaybackContext;

typedef struct {
    HWND *items;
    size_t count;
    size_t capacity;
} HwndList;

typedef struct {
    wchar_t *name;
    size_t nameLen;
    wchar_t *url;
    size_t urlLen;
} AddDlgData;

static struct {
    HWND hWnd;
    HWND hList;
    wchar_t configPath[MAX_PATH];
} g_main = { 0 };

static HINSTANCE g_hInst = NULL;
static HwndList g_playbackWindows = { 0 };

static char *WideToUtf8(const wchar_t *wstr)
{
    if (!wstr) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char *str = (char *)malloc(len);
    if (!str) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL);
    return str;
}

static void TrimWhitespace(wchar_t *str)
{
    size_t len = wcslen(str);
    while (len > 0 && (str[len - 1] == L'\n' || str[len - 1] == L'\r' ||
                       str[len - 1] == L' ' || str[len - 1] == L'\t')) {
        str[--len] = L'\0';
    }
    wchar_t *start = str;
    while (*start == L' ' || *start == L'\t') start++;
    if (start != str) {
        memmove(str, start, (wcslen(start) + 1) * sizeof(wchar_t));
    }
}

static void GetConfigPath(wchar_t *out, size_t size)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        wcsncpy(out, L"favorites.txt", size);
        out[size - 1] = L'\0';
        return;
    }
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash) slash[1] = L'\0';
    wcsncpy(out, path, size - 1);
    out[size - 1] = L'\0';
    size_t remain = size - wcslen(out) - 1;
    wcsncat(out, L"favorites.txt", remain);
}

static void GetExeDirectory(wchar_t *out, size_t size)
{
    if (size == 0) return;
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        out[0] = L'\0';
        return;
    }
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash) slash[1] = L'\0';
    wcsncpy(out, path, size - 1);
    out[size - 1] = L'\0';
}

static void EnsureDefaultMpvConfig(void)
{
    wchar_t exeDir[MAX_PATH];
    GetExeDirectory(exeDir, MAX_PATH);
    if (exeDir[0] == L'\0') return;

    wchar_t configPath[MAX_PATH];
    if (_snwprintf(configPath, MAX_PATH, L"%smpv.conf", exeDir) < 0) return;

    DWORD attr = GetFileAttributesW(configPath);
    if (attr != INVALID_FILE_ATTRIBUTES) return;

    FILE *fp = _wfopen(configPath, L"w");
    if (!fp) return;

    fwprintf(fp, L"# LivePlayer default mpv config\n");
    fwprintf(fp, L"# You can modify or delete this file; it will not be overwritten.\n");
    fwprintf(fp, L"vo=gpu\n");
    fwprintf(fp, L"hwdec=no\n");
    fwprintf(fp, L"keep-open=no\n");
    fwprintf(fp, L"ytdl=yes\n");
    fwprintf(fp, L"ytdl-format=best\n");
    fwprintf(fp, L"# Keyboard shortcuts (space/arrows/etc.) are handled by mpv itself.\n");
    fwprintf(fp, L"input-default-bindings=yes\n");
    fwprintf(fp, L"input-vo-keyboard=yes\n");
    fclose(fp);
}

static void SetDefaultFont(HWND hWnd)
{
    SendMessage(hWnd, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), MAKELPARAM(TRUE, 0));
}

/* 收藏列表管理 */

static void FavoriteAdd(const wchar_t *name, const wchar_t *url)
{
    FavoriteItem *item = (FavoriteItem *)malloc(sizeof(FavoriteItem));
    if (!item) return;
    item->name = _wcsdup(name);
    item->url = _wcsdup(url);
    if (!item->name || !item->url) {
        free(item->name);
        free(item->url);
        free(item);
        return;
    }
    int idx = ListBox_AddString(g_main.hList, name);
    if (idx != LB_ERR && idx != LB_ERRSPACE) {
        ListBox_SetItemData(g_main.hList, idx, (LPARAM)item);
    } else {
        free(item->name);
        free(item->url);
        free(item);
    }
}

static void FavoriteDelete(int idx)
{
    LPARAM data = ListBox_GetItemData(g_main.hList, idx);
    if (data != (LPARAM)LB_ERR) {
        FavoriteItem *item = (FavoriteItem *)data;
        if (item) {
            free(item->name);
            free(item->url);
            free(item);
        }
    }
    ListBox_DeleteString(g_main.hList, idx);
}

static void FavoriteClear(void)
{
    int count = ListBox_GetCount(g_main.hList);
    for (int i = count - 1; i >= 0; i--) {
        FavoriteDelete(i);
    }
}

static const FavoriteItem *FavoriteGet(int idx)
{
    LPARAM data = ListBox_GetItemData(g_main.hList, idx);
    if (data == (LPARAM)LB_ERR) return NULL;
    return (const FavoriteItem *)data;
}

static void LoadFavorites(void)
{
    FILE *fp = _wfopen(g_main.configPath, L"r, ccs=UTF-8");
    if (!fp) return;

    wchar_t name[4096];
    wchar_t url[4096];
    while (fgetws(name, 4096, fp) && fgetws(url, 4096, fp)) {
        TrimWhitespace(name);
        TrimWhitespace(url);
        if (name[0] != L'\0' && url[0] != L'\0') {
            FavoriteAdd(name, url);
        }
    }
    fclose(fp);
}

static void SaveFavorites(void)
{
    FILE *fp = _wfopen(g_main.configPath, L"w, ccs=UTF-8");
    if (!fp) {
        MessageBoxW(g_main.hWnd, L"无法保存收藏列表。", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    int count = ListBox_GetCount(g_main.hList);
    for (int i = 0; i < count; i++) {
        const FavoriteItem *item = FavoriteGet(i);
        if (!item) continue;
        fwprintf(fp, L"%s\n%s\n", item->name, item->url);
    }
    fclose(fp);
}

/* 播放窗口管理 */

static void PlaybackWindowAdd(HWND hWnd)
{
    if (g_playbackWindows.count == g_playbackWindows.capacity) {
        size_t newCap = g_playbackWindows.capacity ? g_playbackWindows.capacity * 2 : 4;
        HWND *newItems = (HWND *)realloc(g_playbackWindows.items, newCap * sizeof(HWND));
        if (!newItems) return;
        g_playbackWindows.items = newItems;
        g_playbackWindows.capacity = newCap;
    }
    g_playbackWindows.items[g_playbackWindows.count++] = hWnd;
}

static void PlaybackWindowRemove(HWND hWnd)
{
    for (size_t i = 0; i < g_playbackWindows.count; i++) {
        if (g_playbackWindows.items[i] == hWnd) {
            g_playbackWindows.items[i] = g_playbackWindows.items[--g_playbackWindows.count];
            return;
        }
    }
}

static void PlaybackWindowCloseAll(void)
{
    if (g_playbackWindows.count == 0) return;
    size_t n = g_playbackWindows.count;
    HWND *copy = (HWND *)malloc(n * sizeof(HWND));
    if (!copy) return;
    memcpy(copy, g_playbackWindows.items, n * sizeof(HWND));
    for (size_t i = 0; i < n; i++) {
        DestroyWindow(copy[i]);
    }
    free(copy);
}

/* mpv 相关 */

static void ShowMpvError(HWND hWnd, int err)
{
    const char *err_str = g_mpv.error_string(err);
    wchar_t msg[512];
    msg[0] = L'\0';
    if (err_str) {
        MultiByteToWideChar(CP_UTF8, 0, err_str, -1, msg, 512);
    } else {
        wcsncpy(msg, L"未知错误", 512);
    }
    MessageBoxW(hWnd, msg, L"解析/播放错误", MB_OK | MB_ICONERROR);
}

/* 弹出 mpv 的原始错误日志（UTF-8）。 */
static void ShowMpvErrorMessage(HWND hWnd, const char *utf8)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wlen <= 0) return;
    wchar_t *wmsg = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wmsg) return;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wmsg, wlen);
    MessageBoxW(hWnd, wmsg, L"播放错误", MB_OK | MB_ICONERROR);
    free(wmsg);
}

/* 累积 mpv 的 error 级日志，供加载/播放失败时原样展示。 */
static void AppendErrLog(PlaybackContext *ctx, const char *text)
{
    if (!text || !*text) return;
    size_t avail = sizeof(ctx->errLog) - 1 - ctx->errLogLen;
    if (avail == 0) return;
    size_t tlen = strlen(text);
    if (tlen > avail) tlen = avail;
    memcpy(ctx->errLog + ctx->errLogLen, text, tlen);
    ctx->errLogLen += tlen;
    ctx->errLog[ctx->errLogLen] = '\0';
}

/* mpv 内嵌在 wid 子窗口里，无法自行全屏；因此监听 mpv 的 fullscreen 属性，
   由我们对顶层窗口做无边框全屏切换。快捷键由用户在 input.conf 里自定义，
   我们只对属性变化作出反应，而不是写死某个按键。 */
static void SetPlaybackFullscreen(PlaybackContext *ctx, BOOL fullscreen)
{
    if (!ctx || !ctx->hWnd || fullscreen == ctx->isFullscreen) return;
    HWND hWnd = ctx->hWnd;
    if (fullscreen) {
        MONITORINFO mi = { sizeof(mi) };
        if (!GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
        ctx->savedStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
        ctx->savedPlacement.length = sizeof(ctx->savedPlacement);
        GetWindowPlacement(hWnd, &ctx->savedPlacement);
        SetWindowLongPtr(hWnd, GWL_STYLE, ctx->savedStyle & ~(LONG_PTR)WS_OVERLAPPEDWINDOW);
        SetWindowPos(hWnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        ctx->isFullscreen = TRUE;
    } else {
        SetWindowLongPtr(hWnd, GWL_STYLE, ctx->savedStyle);
        SetWindowPlacement(hWnd, &ctx->savedPlacement);
        SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        ctx->isFullscreen = FALSE;
    }
}

static void OnMpvEvent(PlaybackContext *ctx)
{
    if (!ctx || !ctx->mpv) return;
    while (1) {
        mpv_event *e = g_mpv.wait_event(ctx->mpv, 0);
        if (e->event_id == MPV_EVENT_NONE) break;

        if (e->event_id == MPV_EVENT_SHUTDOWN) {
            /* mpv 收到 quit（无论绑定到哪个键）-> 关闭整个播放窗口。
               DestroyWindow 会同步触发 WM_DESTROY 并释放 ctx，之后不可再用 ctx。 */
            DestroyWindow(ctx->hWnd);
            return;
        }

        if (e->event_id == MPV_EVENT_LOG_MESSAGE) {
            mpv_event_log_message *lm = (mpv_event_log_message *)e->data;
            if (lm) AppendErrLog(ctx, lm->text);
            continue;
        }

        if (e->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            mpv_event_property *pc = (mpv_event_property *)e->data;
            if (pc && pc->name && pc->data && pc->format == MPV_FORMAT_FLAG &&
                strcmp(pc->name, "fullscreen") == 0) {
                SetPlaybackFullscreen(ctx, (*(int *)pc->data) ? TRUE : FALSE);
            }
            continue;
        }

        if (e->event_id == MPV_EVENT_VIDEO_RECONFIG) {
            int64_t dwidth = 0;
            int64_t dheight = 0;
            int ok = g_mpv.get_property(ctx->mpv, "dwidth", MPV_FORMAT_INT64, &dwidth) >= 0 &&
                     g_mpv.get_property(ctx->mpv, "dheight", MPV_FORMAT_INT64, &dheight) >= 0;
            if (!ctx->sizeAdjusted && !ctx->isFullscreen && ok && dwidth > 0 && dheight > 0 &&
                dwidth <= 8192 && dheight <= 8192 &&
                ctx->hWnd && !IsIconic(ctx->hWnd) && !IsZoomed(ctx->hWnd)) {
                RECT rc = { 0, 0, (LONG)dwidth, (LONG)dheight };
                DWORD style = (DWORD)GetWindowLongPtr(ctx->hWnd, GWL_STYLE);
                DWORD exStyle = (DWORD)GetWindowLongPtr(ctx->hWnd, GWL_EXSTYLE);
                AdjustWindowRectEx(&rc, style, FALSE, exStyle);
                int w = rc.right - rc.left;
                int h = rc.bottom - rc.top;
                SetWindowPos(ctx->hWnd, NULL, 0, 0, w, h,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                ctx->sizeAdjusted = TRUE;
            }
        }

        if (e->event_id == MPV_EVENT_END_FILE) {
            mpv_event_end_file *ef = (mpv_event_end_file *)e->data;
            if (ef && (ef->reason == MPV_END_FILE_REASON_ERROR || ef->error < 0)) {
                if (ctx->errLogLen > 0) {
                    ShowMpvErrorMessage(ctx->hWnd, ctx->errLog);
                } else if (ef->error < 0) {
                    ShowMpvError(ctx->hWnd, ef->error);
                } else {
                    MessageBoxW(ctx->hWnd, L"视频播放结束或解析失败。",
                                L"解析/播放错误", MB_OK | MB_ICONERROR);
                }
                ctx->errLogLen = 0;
                ctx->errLog[0] = '\0';
            }
        }
    }
}

static void MpvWakeupCb(void *d)
{
    HWND hWnd = (HWND)d;
    if (hWnd && IsWindow(hWnd)) {
        PostMessageW(hWnd, WM_MPV_EVENT, 0, 0);
    }
}

static BOOL InitPlaybackMpv(PlaybackContext *ctx, HWND hVideo, const wchar_t *urlW)
{
    ctx->mpv = g_mpv.create();
    if (!ctx->mpv) return FALSE;

    /* Capture mpv's own error log so a failure shows the real reason
       (ytdl errors, network errors, ...) instead of a generic error string. */
    g_mpv.request_log_messages(ctx->mpv, "error");

    char widStr[32];
    snprintf(widStr, sizeof(widStr), "%lld", (long long)(intptr_t)hVideo);
    g_mpv.set_option_string(ctx->mpv, "wid", widStr);

    /* mpv owns the shortcut LOGIC (default bindings + input.conf); we only
       forward raw key events from the playback window (see WM_KEYDOWN below),
       because mpv's Win32 video window lives on its own thread and never holds
       our keyboard focus. In libmpv both the default key bindings AND
       config-file loading are OFF unless explicitly enabled here. */
    g_mpv.set_option_string(ctx->mpv, "input-default-bindings", "yes");
    g_mpv.set_option_string(ctx->mpv, "input-vo-keyboard", "yes");
    g_mpv.set_option_string(ctx->mpv, "config", "yes");

    /* Other options are read from mpv.conf in the executable directory. */

    wchar_t exeDir[MAX_PATH];
    GetExeDirectory(exeDir, MAX_PATH);
    if (exeDir[0] != L'\0') {
        char *configDir = WideToUtf8(exeDir);
        if (configDir) {
            g_mpv.set_option_string(ctx->mpv, "config-dir", configDir);
            free(configDir);
        }
    }

    wchar_t exePath[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        wchar_t *slash = wcsrchr(exePath, L'\\');
        if (slash) slash[1] = L'\0';
        size_t remain = MAX_PATH - wcslen(exePath) - 1;
        wcsncat(exePath, L"yt-dlp.exe", remain);
        char *ytdlPath = WideToUtf8(exePath);
        if (ytdlPath) {
            g_mpv.set_option_string(ctx->mpv, "ytdl-path", ytdlPath);
            free(ytdlPath);
        }
    }

    g_mpv.set_wakeup_callback(ctx->mpv, MpvWakeupCb, ctx->hWnd);

    if (g_mpv.initialize(ctx->mpv) < 0) {
        g_mpv.terminate_destroy(ctx->mpv);
        ctx->mpv = NULL;
        return FALSE;
    }

    /* React to mpv's fullscreen state (toggled by whatever key the user binds,
       or by fs=yes in mpv.conf) and apply it to our own top-level window. */
    g_mpv.observe_property(ctx->mpv, 0, "fullscreen", MPV_FORMAT_FLAG);

    char *url = WideToUtf8(urlW);
    if (!url) {
        g_mpv.terminate_destroy(ctx->mpv);
        ctx->mpv = NULL;
        return FALSE;
    }
    const char *cmd[] = { "loadfile", url, NULL };
    g_mpv.command_async(ctx->mpv, 0, cmd);
    free(url);

    return TRUE;
}

/* 播放窗口 */

/* 把 Win32 虚拟键转换成 mpv 的按键名（仅做键名映射，具体快捷键行为由 mpv
   依据内置默认绑定 / input.conf 决定）。 */
static void VkToMpvKeyName(UINT vk, BOOL shift, BOOL ctrl, BOOL alt, char *out, size_t outSize)
{
    out[0] = '\0';
    if (outSize == 0) return;

    char key[32] = {0};
    if (vk >= '0' && vk <= '9') {
        snprintf(key, sizeof(key), "%c", (char)vk);
    } else if (vk >= 'A' && vk <= 'Z') {
        if (shift && !ctrl && !alt) {
            snprintf(key, sizeof(key), "%c", (char)vk);
        } else {
            snprintf(key, sizeof(key), "%c", (char)(vk + 32));
        }
    } else {
        switch (vk) {
        case VK_SPACE:  strncpy(key, "SPACE", sizeof(key) - 1); break;
        case VK_RETURN: strncpy(key, "ENTER", sizeof(key) - 1); break;
        case VK_ESCAPE: strncpy(key, "ESC", sizeof(key) - 1); break;
        case VK_TAB:    strncpy(key, "TAB", sizeof(key) - 1); break;
        case VK_BACK:   strncpy(key, "BS", sizeof(key) - 1); break;
        case VK_DELETE: strncpy(key, "DEL", sizeof(key) - 1); break;
        case VK_LEFT:   strncpy(key, "LEFT", sizeof(key) - 1); break;
        case VK_RIGHT:  strncpy(key, "RIGHT", sizeof(key) - 1); break;
        case VK_UP:     strncpy(key, "UP", sizeof(key) - 1); break;
        case VK_DOWN:   strncpy(key, "DOWN", sizeof(key) - 1); break;
        case VK_HOME:   strncpy(key, "HOME", sizeof(key) - 1); break;
        case VK_END:    strncpy(key, "END", sizeof(key) - 1); break;
        case VK_PRIOR:  strncpy(key, "PGUP", sizeof(key) - 1); break;
        case VK_NEXT:   strncpy(key, "PGDWN", sizeof(key) - 1); break;
        default:
            if (vk >= VK_F1 && vk <= VK_F12) {
                snprintf(key, sizeof(key), "F%d", (int)(vk - VK_F1 + 1));
            }
            break;
        }
    }

    if (key[0] == '\0') return;

    char mods[32] = {0};
    if (ctrl)  strncat(mods, "ctrl+", sizeof(mods) - 1);
    if (alt)   strncat(mods, "alt+", sizeof(mods) - 1);
    if (shift) strncat(mods, "shift+", sizeof(mods) - 1);

    snprintf(out, outSize, "%s%s", mods, key);
}

static LRESULT CALLBACK PlaybackWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
    {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        const wchar_t *url = (const wchar_t *)cs->lpCreateParams;

        PlaybackContext *ctx = (PlaybackContext *)calloc(1, sizeof(PlaybackContext));
        if (!ctx) return -1;
        ctx->hWnd = hWnd;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)ctx);

        ctx->hVideo = CreateWindowW(L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
            0, 0, cs->cx, cs->cy, hWnd, NULL, g_hInst, NULL);

        if (!InitPlaybackMpv(ctx, ctx->hVideo, url)) {
            MessageBoxW(hWnd, L"初始化播放器失败。", L"错误", MB_OK | MB_ICONERROR);
        }

        PlaybackWindowAdd(hWnd);
        break;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        /* The playback top-level window holds keyboard focus (mpv's video
           window is on a separate thread and never takes our focus), so we
           translate the key and hand it to mpv, which decides what it does. */
        PlaybackContext *ctx = (PlaybackContext *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (ctx && ctx->mpv) {
            UINT vk = (UINT)wParam;
            BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            BOOL ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            BOOL alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;
            char keyName[64];
            VkToMpvKeyName(vk, shift, ctrl, alt, keyName, sizeof(keyName));
            if (keyName[0] != '\0') {
                const char *cmd[] = { "keypress", keyName, NULL };
                g_mpv.command_async(ctx->mpv, 0, cmd);
            }
        }
        if (message == WM_SYSKEYDOWN) {
            return DefWindowProcW(hWnd, message, wParam, lParam);
        }
        break;
    }
    case WM_SIZE:
    {
        PlaybackContext *ctx = (PlaybackContext *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (ctx && ctx->hVideo) {
            MoveWindow(ctx->hVideo, 0, 0, w, h, TRUE);
        }
        break;
    }
    case WM_MPV_EVENT:
    {
        PlaybackContext *ctx = (PlaybackContext *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        OnMpvEvent(ctx);
        break;
    }
    case WM_DESTROY:
    {
        PlaybackContext *ctx = (PlaybackContext *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (ctx) {
            if (ctx->mpv) {
                g_mpv.set_wakeup_callback(ctx->mpv, NULL, NULL);
                g_mpv.terminate_destroy(ctx->mpv);
            }
            SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
            free(ctx);
        }
        PlaybackWindowRemove(hWnd);
        break;
    }
    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

static void OpenPlaybackWindow(const wchar_t *name, const wchar_t *url)
{
    wchar_t title[512];
    _snwprintf(title, 512, L"LivePlayer - %s", name);

    HWND hWnd = CreateWindowW(L"LivePlayerVideoClass", title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 960, 600,
        NULL, NULL, g_hInst, (LPVOID)url);

    if (hWnd) {
        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);
    }
}

/* 主窗口 */

static void PlaySelected(void)
{
    int idx = ListBox_GetCurSel(g_main.hList);
    if (idx == LB_ERR) return;
    const FavoriteItem *item = FavoriteGet(idx);
    if (!item) return;
    OpenPlaybackWindow(item->name, item->url);
}

static INT_PTR CALLBACK AddFavoriteDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static AddDlgData *data = NULL;
    switch (message) {
    case WM_INITDIALOG:
        data = (AddDlgData *)lParam;
        SetDlgItemTextW(hDlg, IDC_EDIT_NAME, L"");
        SetDlgItemTextW(hDlg, IDC_EDIT_URL, L"");
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            if (data) {
                GetDlgItemTextW(hDlg, IDC_EDIT_NAME, data->name, (int)data->nameLen);
                GetDlgItemTextW(hDlg, IDC_EDIT_URL, data->url, (int)data->urlLen);
            }
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

static void OnAddFavorite(void)
{
    wchar_t name[4096] = { 0 };
    wchar_t url[4096] = { 0 };
    AddDlgData data = { name, 4096, url, 4096 };

    if (DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_DIALOG_ADD), g_main.hWnd,
                        AddFavoriteDlgProc, (LPARAM)&data) != IDOK) {
        return;
    }

    TrimWhitespace(name);
    TrimWhitespace(url);

    if (name[0] == L'\0' || url[0] == L'\0') {
        MessageBoxW(g_main.hWnd, L"名称和 URL 都不能为空。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    FavoriteAdd(name, url);
    SaveFavorites();
}

static void OnDelFavorite(void)
{
    int idx = ListBox_GetCurSel(g_main.hList);
    if (idx == LB_ERR) return;
    if (MessageBoxW(g_main.hWnd, L"确定要删除选中的收藏吗？", L"确认", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    FavoriteDelete(idx);
    SaveFavorites();
}

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
    {
        g_main.hWnd = hWnd;
        GetConfigPath(g_main.configPath, MAX_PATH);

        CreateWindowW(L"BUTTON", L"添加",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 10, 60, 25, hWnd, (HMENU)ID_BTN_ADD, g_hInst, NULL);

        CreateWindowW(L"BUTTON", L"删除",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            80, 10, 60, 25, hWnd, (HMENU)ID_BTN_DEL, g_hInst, NULL);

        CreateWindowW(L"BUTTON", L"播放",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            150, 10, 60, 25, hWnd, (HMENU)ID_BTN_PLAY, g_hInst, NULL);

        g_main.hList = CreateWindowW(L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_STANDARD,
            10, 45, 440, 300, hWnd, (HMENU)ID_LISTBOX, g_hInst, NULL);

        HWND hChild = GetWindow(hWnd, GW_CHILD);
        while (hChild) {
            SetDefaultFont(hChild);
            hChild = GetWindow(hChild, GW_HWNDNEXT);
        }

        LoadFavorites();
        break;
    }
    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (g_main.hList) {
            MoveWindow(g_main.hList, 10, 45, w - 20, h - 55, TRUE);
        }
        break;
    }
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == ID_BTN_ADD) {
            OnAddFavorite();
        } else if (id == ID_BTN_DEL) {
            OnDelFavorite();
        } else if (id == ID_BTN_PLAY) {
            PlaySelected();
        } else if (id == ID_LISTBOX && HIWORD(wParam) == LBN_DBLCLK) {
            PlaySelected();
        }
        break;
    }
    case WM_DESTROY:
        FavoriteClear();
        PlaybackWindowCloseAll();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    g_hInst = hInstance;

    if (!MpvLoad()) {
        MessageBoxW(NULL, L"无法加载 libmpv-2.dll，请确认 mpv 已安装并在 PATH 中。", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    EnsureDefaultMpvConfig();

    INITCOMMONCONTROLSEX iccex = { sizeof(iccex), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&iccex);

    WNDCLASSEXW wcexMain = { 0 };
    wcexMain.cbSize = sizeof(wcexMain);
    wcexMain.style = CS_HREDRAW | CS_VREDRAW;
    wcexMain.lpfnWndProc = MainWndProc;
    wcexMain.hInstance = hInstance;
    wcexMain.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wcexMain.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wcexMain.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcexMain.lpszClassName = L"LivePlayerMainClass";
    wcexMain.hIconSm = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);

    if (!RegisterClassExW(&wcexMain)) {
        MessageBoxW(NULL, L"注册主窗口类失败。", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wcexVideo = { 0 };
    wcexVideo.cbSize = sizeof(wcexVideo);
    wcexVideo.style = CS_HREDRAW | CS_VREDRAW;
    wcexVideo.lpfnWndProc = PlaybackWndProc;
    wcexVideo.hInstance = hInstance;
    wcexVideo.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wcexVideo.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wcexVideo.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcexVideo.lpszClassName = L"LivePlayerVideoClass";
    wcexVideo.hIconSm = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);

    if (!RegisterClassExW(&wcexVideo)) {
        MessageBoxW(NULL, L"注册播放窗口类失败。", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindowW(L"LivePlayerMainClass", L"LivePlayer",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME,
        CW_USEDEFAULT, 0, 480, 400,
        NULL, NULL, hInstance, NULL);

    if (!hWnd) {
        MessageBoxW(NULL, L"创建窗口失败。", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    MpvUnload();
    free(g_playbackWindows.items);
    return (int)msg.wParam;
}

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

static void OnMpvEvent(PlaybackContext *ctx)
{
    if (!ctx || !ctx->mpv) return;
    while (1) {
        mpv_event *e = g_mpv.wait_event(ctx->mpv, 0);
        if (e->event_id == MPV_EVENT_NONE) break;

        if (e->event_id == MPV_EVENT_END_FILE) {
            mpv_event_end_file *ef = (mpv_event_end_file *)e->data;
            if (ef && (ef->reason == MPV_END_FILE_REASON_ERROR || ef->error < 0)) {
                if (ef->error < 0) {
                    ShowMpvError(ctx->hWnd, ef->error);
                } else {
                    MessageBoxW(ctx->hWnd, L"视频播放结束或解析失败。",
                                L"解析/播放错误", MB_OK | MB_ICONERROR);
                }
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

    char widStr[32];
    snprintf(widStr, sizeof(widStr), "%lld", (long long)(intptr_t)hVideo);
    g_mpv.set_option_string(ctx->mpv, "wid", widStr);

    g_mpv.set_option_string(ctx->mpv, "vo", "gpu");
    g_mpv.set_option_string(ctx->mpv, "hwdec", "no");
    g_mpv.set_option_string(ctx->mpv, "keep-open", "no");
    g_mpv.set_option_string(ctx->mpv, "ytdl", "yes");
    g_mpv.set_option_string(ctx->mpv, "ytdl-format", "best");

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

    char *url = WideToUtf8(urlW);
    if (!url) return FALSE;
    const char *cmd[] = { "loadfile", url, NULL };
    g_mpv.command_async(ctx->mpv, 0, cmd);
    free(url);

    return TRUE;
}

/* 播放窗口 */

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
            free(ctx);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
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
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME,
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

// ShutdownTimerV3.cpp — Modern Windows shutdown timer with modes, settings, and custom icon
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <dwmapi.h>
#include <powrprof.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "powrprof.lib")

#define IDC_MINUTES_INPUT   101
#define IDC_SECONDS_INPUT   102
#define IDC_START_BUTTON    103
#define IDC_CANCEL_BUTTON   104
#define IDC_PRESET_30MIN    105
#define IDC_PRESET_60MIN    106
#define IDC_PRESET_120MIN   107
#define IDC_PROGRESS_BAR    108
#define IDM_FILE_ABOUT      109
#define IDM_FILE_EXIT       110

#define IDM_SETTINGS_AUTOSTART     201
#define IDM_SETTINGS_SOUNDWARN     202
#define IDM_SETTINGS_CONFIRMSHUT   203

#define IDM_MODE_SHUTDOWN   301
#define IDM_MODE_REBOOT     302
#define IDM_MODE_SLEEP      303
#define IDM_MODE_HIBERNATE  304

#define IDI_APP_ICON 1 // placeholder for app icon (add to resources: IDI_APP_ICON ICON "app.ico")

HINSTANCE g_hInstance;
HWND g_hMainWindow = nullptr;
HWND g_hMinutesInput = nullptr, g_hSecondsInput = nullptr;
HWND g_hStartButton = nullptr, g_hCancelButton = nullptr, g_hProgressBar = nullptr;
HWND g_hWarningDialog = nullptr;
bool g_WarningActive = false;
int g_TotalSeconds = 0;
int g_RemainingSeconds = 0;
bool g_IsFinalWarningShown = false;
HWND g_hoverButton = nullptr;

bool g_AutoStart = false;
bool g_SoundWarning = true;
bool g_ConfirmShutdown = true;
int g_ActionMode = IDM_MODE_SHUTDOWN;

ATOM RegisterMainWindowClass(HINSTANCE);
LRESULT CALLBACK MainWindowProc(HWND, UINT, WPARAM, LPARAM);
void InitializeUIControls(HWND);
void ApplyModernVisual(HWND);
void DrawModernButton(LPDRAWITEMSTRUCT);
bool PerformSystemAction();
void StartShutdownTimer(HWND, int, int);
void ShowFinalWarningDialog(HWND);
HMENU BuildMenuBar();

void ApplyModernVisual(HWND hwnd)
{
    HDC hdc = GetDC(hwnd);
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(hwnd, hdc);

    HFONT hFont = CreateFontW(
        -MulDiv(10, dpiY, 72), 0, 0, 0, FW_MEDIUM,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    EnumChildWindows(hwnd, [](HWND child, LPARAM lParam) -> BOOL {
        SendMessageW(child, WM_SETFONT, (WPARAM)lParam, TRUE);
        return TRUE;
        }, (LPARAM)hFont);

    DWORD cornerPref = 2;
    DwmSetWindowAttribute(hwnd, 33, &cornerPref, sizeof(cornerPref));
}

bool PerformSystemAction()
{
    HANDLE hToken = nullptr;
    TOKEN_PRIVILEGES tkp = {};

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0);

    BOOL result = FALSE;

    switch (g_ActionMode)
    {
    case IDM_MODE_SHUTDOWN:
        result = ExitWindowsEx(EWX_POWEROFF | EWX_FORCE, SHTDN_REASON_MAJOR_APPLICATION);
        break;
    case IDM_MODE_REBOOT:
        result = ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_APPLICATION);
        break;
    case IDM_MODE_SLEEP:
        result = SetSuspendState(FALSE, TRUE, FALSE);
        break;
    case IDM_MODE_HIBERNATE:
        result = SetSuspendState(TRUE, TRUE, FALSE);
        break;
    }

    CloseHandle(hToken);
    return result ? true : false;
}

void DrawModernButton(LPDRAWITEMSTRUCT dis)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    bool isPressed = (dis->itemState & ODS_SELECTED) != 0;
    bool isHover = (dis->hwndItem == g_hoverButton);

    COLORREF bg = RGB(250, 250, 250);
    if (isHover) bg = RGB(230, 242, 255);
    if (isPressed) bg = RGB(205, 225, 245);

    HBRUSH br = CreateSolidBrush(bg);
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));

    wchar_t text[128] = { 0 };
    GetWindowTextW(dis->hwndItem, text, (int)std::size(text));
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void ShowFinalWarningDialog(HWND hwnd)
{
    if (g_WarningActive) return;
    g_WarningActive = true;

    // Воспроизводим звук только если включено предупреждение
    if (g_SoundWarning)
        MessageBeep(MB_ICONWARNING);

    g_hWarningDialog = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"#32770", L"Предупреждение",
        WS_CAPTION | WS_SYSMENU | DS_CENTER,
        CW_USEDEFAULT, CW_USEDEFAULT, 360, 160,
        hwnd, NULL, g_hInstance, NULL);

    CreateWindowW(L"STATIC",
        L"Осталась 1 минута до выполнения действия.\nНажмите \"Отмена\", чтобы остановить таймер.",
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        20, 20, 320, 60, g_hWarningDialog, NULL, g_hInstance, NULL);

    HWND hCancel = CreateWindowW(L"BUTTON", L"Отмена",
        WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        120, 90, 100, 28, g_hWarningDialog, (HMENU)IDCANCEL, g_hInstance, NULL);

    SendMessageW(hCancel, WM_SETFONT, (WPARAM)SendMessageW(hwnd, WM_GETFONT, 0, 0), TRUE);

    SetTimer(g_hWarningDialog, 2, 30000, NULL); // авто-закрытие через 30 сек

    ShowWindow(g_hWarningDialog, SW_SHOW);
    UpdateWindow(g_hWarningDialog);
}

void StartShutdownTimer(HWND hwnd, int minutes, int seconds)
{
    g_TotalSeconds = minutes * 60 + seconds;
    if (g_TotalSeconds <= 0) return;

    g_RemainingSeconds = g_TotalSeconds;
    g_IsFinalWarningShown = false;

    SendMessageW(g_hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, g_TotalSeconds));
    SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);

    std::wstring msg = L"Действие будет выполнено через ";
    if (minutes > 0) msg += std::to_wstring(minutes) + L" мин ";
    if (seconds > 0) msg += std::to_wstring(seconds) + L" сек.";

    // Звуковое уведомление только если включено
    UINT boxFlags = MB_OK | MB_ICONINFORMATION;
    if (!g_SoundWarning)
        boxFlags &= ~MB_ICONINFORMATION; // убираем звуковой сигнал

    MessageBoxW(hwnd, msg.c_str(), L"Таймер установлен", boxFlags);

    SetTimer(hwnd, 1, 1000, NULL);
}


void InitializeUIControls(HWND hwnd)
{
    const int totalWidth = 300;
    const int marginX = 14;
    const int marginY = 20;
    const int labelHeight = 18;
    const int editHeight = 26;
    const int buttonHeight = 30;
    const int buttonWidth = 75;
    const int spacingY = 10;
    const int spacingSection = 22;

    int y = marginY;

    HFONT hFont = CreateFontW(
        -MulDiv(10, GetDeviceCaps(GetDC(hwnd), LOGPIXELSY), 72),
        0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    HWND lblMin = CreateWindowW(L"STATIC", L"Время до действия (минуты):",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        marginX, y, totalWidth, labelHeight, hwnd, NULL, g_hInstance, NULL);
    SendMessageW(lblMin, WM_SETFONT, (WPARAM)hFont, TRUE);

    y += labelHeight + spacingY;

    g_hMinutesInput = CreateWindowW(L"EDIT", L"30",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER | ES_CENTER,
        marginX, y, totalWidth, editHeight,
        hwnd, (HMENU)IDC_MINUTES_INPUT, g_hInstance, NULL);
    SendMessageW(g_hMinutesInput, WM_SETFONT, (WPARAM)hFont, TRUE);

    y += editHeight + spacingSection;

    HWND lblSec = CreateWindowW(L"STATIC", L"Дополнительно (секунды):",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        marginX, y, totalWidth, labelHeight,
        hwnd, NULL, g_hInstance, NULL);
    SendMessageW(lblSec, WM_SETFONT, (WPARAM)hFont, TRUE);

    y += labelHeight + spacingY;

    g_hSecondsInput = CreateWindowW(L"EDIT", L"0",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER | ES_CENTER,
        marginX, y, totalWidth, editHeight,
        hwnd, (HMENU)IDC_SECONDS_INPUT, g_hInstance, NULL);
    SendMessageW(g_hSecondsInput, WM_SETFONT, (WPARAM)hFont, TRUE);

    y += editHeight + spacingSection;

    DWORD btnStyle = WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP;
    int btnSpacing = 10;
    int totalPresetWidth = buttonWidth * 3 + btnSpacing * 2;
    int startX = marginX + (totalWidth - totalPresetWidth) / 2;

    HWND b30 = CreateWindowW(L"BUTTON", L"30 мин", btnStyle,
        startX, y, buttonWidth, buttonHeight,
        hwnd, (HMENU)IDC_PRESET_30MIN, g_hInstance, NULL);
    HWND b60 = CreateWindowW(L"BUTTON", L"60 мин", btnStyle,
        startX + buttonWidth + btnSpacing, y, buttonWidth, buttonHeight,
        hwnd, (HMENU)IDC_PRESET_60MIN, g_hInstance, NULL);
    HWND b120 = CreateWindowW(L"BUTTON", L"120 мин", btnStyle,
        startX + (buttonWidth + btnSpacing) * 2, y, buttonWidth, buttonHeight,
        hwnd, (HMENU)IDC_PRESET_120MIN, g_hInstance, NULL);

    SendMessageW(b30, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(b60, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(b120, WM_SETFONT, (WPARAM)hFont, TRUE);

    y += buttonHeight + spacingSection;

    int wideBtnWidth = 120;
    int btnGap = 20;
    int totalWidth2 = wideBtnWidth * 2 + btnGap;
    startX = marginX + (totalWidth - totalWidth2) / 2;

    g_hStartButton = CreateWindowW(L"BUTTON", L"Запустить",
        btnStyle | BS_DEFPUSHBUTTON,
        startX, y, wideBtnWidth, buttonHeight + 4,
        hwnd, (HMENU)IDC_START_BUTTON, g_hInstance, NULL);

    g_hCancelButton = CreateWindowW(L"BUTTON", L"Отменить",
        btnStyle,
        startX + wideBtnWidth + btnGap, y, wideBtnWidth, buttonHeight + 4,
        hwnd, (HMENU)IDC_CANCEL_BUTTON, g_hInstance, NULL);

    SendMessageW(g_hStartButton, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(g_hCancelButton, WM_SETFONT, (WPARAM)hFont, TRUE);

    y += buttonHeight + spacingSection;

    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icex);

    g_hProgressBar = CreateWindowExW(0, PROGRESS_CLASS, NULL,
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH | WS_BORDER,
        marginX, y, totalWidth, 22, hwnd, (HMENU)IDC_PROGRESS_BAR, g_hInstance, NULL);
}

HMENU BuildMenuBar()
{
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU set = CreatePopupMenu();
    HMENU mode = CreatePopupMenu();

    AppendMenuW(file, MF_STRING, IDM_FILE_ABOUT, L"О программе");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, IDM_FILE_EXIT, L"Выход");

    AppendMenuW(set, MF_STRING | MF_UNCHECKED, IDM_SETTINGS_AUTOSTART, L"Автозапуск при старте Windows");
    AppendMenuW(set, MF_STRING | MF_CHECKED, IDM_SETTINGS_SOUNDWARN, L"Звуковое предупреждение");
    AppendMenuW(set, MF_STRING | MF_CHECKED, IDM_SETTINGS_CONFIRMSHUT, L"Подтверждение действия");

    AppendMenuW(mode, MF_STRING | MF_CHECKED, IDM_MODE_SHUTDOWN, L"Выключение");
    AppendMenuW(mode, MF_STRING, IDM_MODE_REBOOT, L"Перезагрузка");
    AppendMenuW(mode, MF_STRING, IDM_MODE_SLEEP, L"Сон");
    AppendMenuW(mode, MF_STRING, IDM_MODE_HIBERNATE, L"Гибернация");

    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"Файл");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)set, L"Настройки");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mode, L"Режим действия");

    return bar;
}

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TIMER:
        if (wParam == 2 && g_hWarningDialog)
        {
            DestroyWindow(g_hWarningDialog);
            g_hWarningDialog = nullptr;
            g_WarningActive = false;
            KillTimer(hwnd, 2);
            return 0;
        }

        if (wParam == 1 && --g_RemainingSeconds >= 0)
        {
            SendMessageW(g_hProgressBar, PBM_SETPOS, g_TotalSeconds - g_RemainingSeconds, 0);

            int mins = g_RemainingSeconds / 60;
            int secs = g_RemainingSeconds % 60;
            wchar_t bufM[8], bufS[8];
            swprintf(bufM, 8, L"%d", mins);
            swprintf(bufS, 8, L"%02d", secs);
            SetWindowTextW(g_hMinutesInput, bufM);
            SetWindowTextW(g_hSecondsInput, bufS);

            if (!g_IsFinalWarningShown && g_RemainingSeconds == 60)
            {
                g_IsFinalWarningShown = true;
                ShowFinalWarningDialog(hwnd);
            }

            if (g_RemainingSeconds == 0)
            {
                KillTimer(hwnd, 1);
                PerformSystemAction();
            }
        }
        break;

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        switch (id)
        {
        case IDC_START_BUTTON:
        {
            wchar_t buf1[16], buf2[16];
            GetWindowTextW(g_hMinutesInput, buf1, 16);
            GetWindowTextW(g_hSecondsInput, buf2, 16);
            int minutes = _wtoi(buf1);
            int seconds = _wtoi(buf2);
            KillTimer(hwnd, 1);
            StartShutdownTimer(hwnd, minutes, seconds);
            break;
        }

        case IDC_PRESET_30MIN: StartShutdownTimer(hwnd, 30, 0); break;
        case IDC_PRESET_60MIN: StartShutdownTimer(hwnd, 60, 0); break;
        case IDC_PRESET_120MIN: StartShutdownTimer(hwnd, 120, 0); break;

        case IDCANCEL:
            if (g_hWarningDialog && g_WarningActive)
            {
                DestroyWindow(g_hWarningDialog);
                g_hWarningDialog = nullptr;
                g_WarningActive = false;
                KillTimer(hwnd, 1);
                g_RemainingSeconds = 0;
                SetWindowTextW(g_hMinutesInput, L"0");
                SetWindowTextW(g_hSecondsInput, L"0");
                SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);
            }
            break;

        case IDC_CANCEL_BUTTON:
            KillTimer(hwnd, 1);
            g_RemainingSeconds = 0;
            SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);
            break;

            // Меню "Файл"
        case IDM_FILE_EXIT:
            PostQuitMessage(0);
            break;

        case IDM_FILE_ABOUT:
            MessageBoxW(hwnd, L"Shutdown Timer V3\n© 2025", L"О программе", MB_OK | MB_ICONINFORMATION);
            break;

            // Меню "Настройки" (чекбоксы)
        case IDM_SETTINGS_AUTOSTART:
            g_AutoStart = !g_AutoStart;
            CheckMenuItem(GetMenu(hwnd), IDM_SETTINGS_AUTOSTART,
                MF_BYCOMMAND | (g_AutoStart ? MF_CHECKED : MF_UNCHECKED));
            break;

        case IDM_SETTINGS_SOUNDWARN:
            g_SoundWarning = !g_SoundWarning;
            CheckMenuItem(GetMenu(hwnd), IDM_SETTINGS_SOUNDWARN,
                MF_BYCOMMAND | (g_SoundWarning ? MF_CHECKED : MF_UNCHECKED));
            break;

        case IDM_SETTINGS_CONFIRMSHUT:
            g_ConfirmShutdown = !g_ConfirmShutdown;
            CheckMenuItem(GetMenu(hwnd), IDM_SETTINGS_CONFIRMSHUT,
                MF_BYCOMMAND | (g_ConfirmShutdown ? MF_CHECKED : MF_UNCHECKED));
            break;

            // Меню "Режим действия" (радио-группа)
        case IDM_MODE_SHUTDOWN:
        case IDM_MODE_REBOOT:
        case IDM_MODE_SLEEP:
        case IDM_MODE_HIBERNATE:
        {
            g_ActionMode = id;
            HMENU hMenu = GetMenu(hwnd);
            CheckMenuRadioItem(hMenu, IDM_MODE_SHUTDOWN, IDM_MODE_HIBERNATE, id, MF_BYCOMMAND);
            break;
        }
        }
    }
    break;

    case WM_DRAWITEM:
        DrawModernButton((LPDRAWITEMSTRUCT)lParam);
        return TRUE;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


ATOM RegisterMainWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(wcex);
    wcex.lpfnWndProc = MainWindowProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"ShutdownTimerV3";
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    wcex.hIconSm = wcex.hIcon;
    return RegisterClassExW(&wcex);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    g_hInstance = hInstance;
    RegisterMainWindowClass(hInstance);

    int width = 340, height = 420;
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    HWND hwnd = CreateWindowExW(
        0, L"ShutdownTimerV3", L"Shutdown Timer V3",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, width, height,
        nullptr, BuildMenuBar(), hInstance, nullptr);

    if (!hwnd) return 0;

    g_hMainWindow = hwnd;
    InitializeUIControls(hwnd);
    ApplyModernVisual(hwnd);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}

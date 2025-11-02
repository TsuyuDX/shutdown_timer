// ShutdownTimerV2.cpp

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <dwmapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")

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

HINSTANCE g_hInstance;
HWND g_hMainWindow, g_hMinutesInput, g_hSecondsInput, g_hStartButton, g_hCancelButton, g_hProgressBar;
int g_TotalSeconds = 0;
int g_RemainingSeconds = 0;
bool g_IsFinalWarningShown = false;
HWND g_hoverButton = NULL;

// Forward declarations
ATOM RegisterMainWindowClass(HINSTANCE);
LRESULT CALLBACK MainWindowProc(HWND, UINT, WPARAM, LPARAM);
void InitializeUIControls(HWND);
void ApplyModernVisual(HWND);
void DrawModernButton(LPDRAWITEMSTRUCT);
bool PerformSystemShutdown();
void StartShutdownTimer(HWND, int, int);
void ShowFinalWarningDialog(HWND);
HMENU BuildMenuBar();

// Modern visual styling 
void ApplyModernVisual(HWND hwnd)
{
    HFONT hFont = CreateFontW(
        -MulDiv(9, GetDeviceCaps(GetDC(hwnd), LOGPIXELSY), 72),
        0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    EnumChildWindows(hwnd, [](HWND child, LPARAM lParam) -> BOOL {
        SendMessage(child, WM_SETFONT, (WPARAM)lParam, TRUE);
        return TRUE;
        }, (LPARAM)hFont);

    DWORD cornerPref = 2; 
    DwmSetWindowAttribute(hwnd, 33, &cornerPref, sizeof(cornerPref));
}

// System shutdown 
bool PerformSystemShutdown()
{
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp = {};

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0);

    BOOL result = ExitWindowsEx(EWX_POWEROFF | EWX_FORCE,
        SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_OTHER);

    CloseHandle(hToken);
    return result;
}

//  Draw button with soft hover like Win10 menu 
void DrawModernButton(LPDRAWITEMSTRUCT dis)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool isPressed = (dis->itemState & ODS_SELECTED);
    bool isHover = (dis->hwndItem == g_hoverButton);

    COLORREF bg = RGB(250, 250, 250);
    if (isHover)  bg = RGB(230, 240, 255);
    if (isPressed) bg = RGB(210, 230, 250);

    HBRUSH brush = CreateSolidBrush(bg);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));

    wchar_t text[128];
    GetWindowTextW(dis->hwndItem, text, 128);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// Final warning 
void ShowFinalWarningDialog(HWND hwnd)
{
    int result = MessageBoxW(hwnd,
        L"Компьютер будет выключен через 1 минуту!\n\n"
        L"Нажмите 'Отмена', чтобы прервать процесс.",
        L"Последнее предупреждение",
        MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2 | MB_TOPMOST);

    if (result == IDCANCEL)
    {
        KillTimer(hwnd, 1);
        g_RemainingSeconds = 0;
        g_IsFinalWarningShown = false;
        SetWindowTextW(g_hMinutesInput, L"0");
        SetWindowTextW(g_hSecondsInput, L"0");
        SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);
        MessageBoxW(hwnd, L"Выключение отменено!", L"Отмена", MB_OK | MB_ICONINFORMATION);
    }
}

// Timer start 
void StartShutdownTimer(HWND hwnd, int minutes, int seconds)
{
    g_TotalSeconds = minutes * 60 + seconds;
    if (g_TotalSeconds <= 0) return;

    g_RemainingSeconds = g_TotalSeconds;
    g_IsFinalWarningShown = false;

    SendMessageW(g_hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, g_TotalSeconds));
    SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);

    std::wstring msg = L"Компьютер будет выключен через ";
    if (minutes > 0) msg += std::to_wstring(minutes) + L" мин ";
    if (seconds > 0) msg += std::to_wstring(seconds) + L" сек.";

    MessageBoxW(hwnd, msg.c_str(), L"Таймер установлен", MB_OK | MB_ICONINFORMATION);
    SetTimer(hwnd, 1, 1000, NULL);
}

//  UI controls 
void InitializeUIControls(HWND hwnd)
{
    CreateWindowW(L"STATIC", L"Время до выключения (минуты):", WS_VISIBLE | WS_CHILD,
        20, 15, 220, 20, hwnd, NULL, NULL, NULL);

    g_hMinutesInput = CreateWindowW(L"EDIT", L"30", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER,
        20, 35, 220, 25, hwnd, (HMENU)IDC_MINUTES_INPUT, NULL, NULL);

    CreateWindowW(L"STATIC", L"Дополнительно (секунды):", WS_VISIBLE | WS_CHILD,
        20, 70, 220, 20, hwnd, NULL, NULL, NULL);

    g_hSecondsInput = CreateWindowW(L"EDIT", L"0", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER,
        20, 90, 220, 25, hwnd, (HMENU)IDC_SECONDS_INPUT, NULL, NULL);

    DWORD btnStyle = WS_VISIBLE | WS_CHILD | BS_OWNERDRAW;
    CreateWindowW(L"BUTTON", L"30 мин", btnStyle, 20, 125, 70, 25, hwnd, (HMENU)IDC_PRESET_30MIN, NULL, NULL);
    CreateWindowW(L"BUTTON", L"60 мин", btnStyle, 95, 125, 70, 25, hwnd, (HMENU)IDC_PRESET_60MIN, NULL, NULL);
    CreateWindowW(L"BUTTON", L"120 мин", btnStyle, 170, 125, 70, 25, hwnd, (HMENU)IDC_PRESET_120MIN, NULL, NULL);

    g_hStartButton = CreateWindowW(L"BUTTON", L"Запустить", btnStyle | BS_DEFPUSHBUTTON,
        20, 165, 105, 30, hwnd, (HMENU)IDC_START_BUTTON, NULL, NULL);

    g_hCancelButton = CreateWindowW(L"BUTTON", L"Отменить", btnStyle,
        135, 165, 105, 30, hwnd, (HMENU)IDC_CANCEL_BUTTON, NULL, NULL);

    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icex);

    g_hProgressBar = CreateWindowExW(0, PROGRESS_CLASS, NULL, WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
        20, 205, 220, 20, hwnd, (HMENU)IDC_PROGRESS_BAR, NULL, NULL);
}

// Menu
HMENU BuildMenuBar()
{
    HMENU hMenuBar = CreateMenu();
    HMENU hFileMenu = CreatePopupMenu();
    AppendMenuW(hFileMenu, MF_STRING, IDM_FILE_ABOUT, L"О программе");
    AppendMenuW(hFileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFileMenu, MF_STRING, IDM_FILE_EXIT, L"Выход");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hFileMenu, L"Файл");
    return hMenuBar;
}

// Window procedure
LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DRAWITEM:
        DrawModernButton((LPDRAWITEMSTRUCT)lParam);
        return TRUE;

    case WM_MOUSEMOVE:
    {
        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
        HWND hover = ChildWindowFromPoint(hwnd, pt);
        if (hover != g_hoverButton)
        {
            g_hoverButton = hover;
            InvalidateRect(hwnd, NULL, TRUE);
        }
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        break;
    }

    case WM_MOUSELEAVE:
        g_hoverButton = NULL;
        InvalidateRect(hwnd, NULL, TRUE);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_START_BUTTON:
        {
            wchar_t bufMin[10], bufSec[10];
            GetWindowTextW(g_hMinutesInput, bufMin, 10);
            GetWindowTextW(g_hSecondsInput, bufSec, 10);
            int minutes = _wtoi(bufMin);
            int seconds = _wtoi(bufSec);
            KillTimer(hwnd, 1);
            StartShutdownTimer(hwnd, minutes, seconds);
            break;
        }
        case IDC_PRESET_30MIN: StartShutdownTimer(hwnd, 30, 0); break;
        case IDC_PRESET_60MIN: StartShutdownTimer(hwnd, 60, 0); break;
        case IDC_PRESET_120MIN: StartShutdownTimer(hwnd, 120, 0); break;
        case IDC_CANCEL_BUTTON:
            KillTimer(hwnd, 1);
            g_RemainingSeconds = 0;
            g_IsFinalWarningShown = false;
            SetWindowTextW(g_hMinutesInput, L"0");
            SetWindowTextW(g_hSecondsInput, L"0");
            SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);
            MessageBoxW(hwnd, L"Таймер отменён.", L"Информация", MB_OK | MB_ICONINFORMATION);
            break;
        case IDM_FILE_ABOUT:
            MessageBoxW(hwnd, L"Shutdown Timer V2\n\nСовременный Win10 стиль.\nАвтор: Tsuyu™",
                L"О программе", MB_OK | MB_ICONINFORMATION);
            break;
        case IDM_FILE_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        break;

    case WM_TIMER:
        if (g_RemainingSeconds > 0)
        {
            g_RemainingSeconds--;
            if (g_RemainingSeconds == 60 && !g_IsFinalWarningShown)
            {
                g_IsFinalWarningShown = true;
                ShowFinalWarningDialog(hwnd);
            }

            int min = g_RemainingSeconds / 60;
            int sec = g_RemainingSeconds % 60;
            SetWindowTextW(g_hMinutesInput, std::to_wstring(min).c_str());
            SetWindowTextW(g_hSecondsInput, std::to_wstring(sec).c_str());
            SendMessageW(g_hProgressBar, PBM_SETPOS, g_TotalSeconds - g_RemainingSeconds, 0);
        }
        else if (g_RemainingSeconds == 0)
        {
            KillTimer(hwnd, 1);
            if (!PerformSystemShutdown())
                MessageBoxW(hwnd, L"Не удалось выключить компьютер.", L"Ошибка", MB_OK | MB_ICONERROR);
            PostQuitMessage(0);
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}
// Register window class 
ATOM RegisterMainWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = MainWindowProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"ShutdownTimerClass";
    wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    return RegisterClassExW(&wcex);
}

// Entry point 
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    RegisterMainWindowClass(hInstance);

    g_hInstance = hInstance;
    g_hMainWindow = CreateWindowW(L"ShutdownTimerClass", L"Таймер выключения ПК",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, 0, 270, 300, nullptr, nullptr, hInstance, nullptr);

    if (!g_hMainWindow)
        return FALSE;

    ApplyModernVisual(g_hMainWindow);
    SetMenu(g_hMainWindow, BuildMenuBar());
    InitializeUIControls(g_hMainWindow);

    RECT rc;
    GetWindowRect(g_hMainWindow, &rc);
    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(g_hMainWindow, HWND_TOP,
        (screenWidth - winWidth) / 2,
        (screenHeight - winHeight) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ShowWindow(g_hMainWindow, nCmdShow);
    UpdateWindow(g_hMainWindow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

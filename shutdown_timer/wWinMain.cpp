#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <dwmapi.h>        // Добавлено для DWM
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")   // Добавлено для DWM

#pragma comment(linker, \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' \
    version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define ID_TIMER_EDIT       101
#define ID_SECOND_EDIT      102
#define ID_START_BUTTON     103
#define ID_CANCEL_BUTTON    104
#define ID_PRESET_30        105
#define ID_PRESET_60        106
#define ID_PRESET_120       107

HWND hTimerEdit, hSecondEdit, hStartButton, hCancelButton;
HFONT hSegoeFont;
int remainingSeconds = 0;
bool lastChanceShown = false;

void RemoveFromAutostart() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, L"MyShutdownTimer");
        RegCloseKey(hKey);
    }

    PWSTR startupPath = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Startup, 0, NULL, &startupPath))) {
        wchar_t exeName[MAX_PATH];
        GetModuleFileNameW(NULL, exeName, MAX_PATH);
        wchar_t* baseName = wcsrchr(exeName, L'\\');
        baseName = baseName ? baseName + 1 : exeName;

        std::wstring shortcutPath = std::wstring(startupPath) + L"\\" + baseName;
        DeleteFileW(shortcutPath.c_str());
        CoTaskMemFree(startupPath);
    }
}

bool ShutdownComputerWithPrivilege() {
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp = { 0 };

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0);

    BOOL result = ExitWindowsEx(EWX_POWEROFF | EWX_FORCE,
        SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_OTHER);

    tkp.Privileges[0].Attributes = 0;
    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0);
    CloseHandle(hToken);

    return result != FALSE;
}

void ShowLastChanceDialog(HWND hwnd) {
    int result = MessageBoxW(hwnd,
        L"Компьютер будет выключен через 1 минуту!\n\n"
        L"У вас есть 30 секунд чтобы отменить выключение.\n"
        L"Нажмите 'Отмена' чтобы прервать процесс.",
        L"Последнее предупреждение",
        MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2 | MB_TOPMOST);

    if (result == IDCANCEL) {
        KillTimer(hwnd, 1);
        remainingSeconds = 0;
        lastChanceShown = false;
        SetWindowText(hTimerEdit, L"0");
        SetWindowText(hSecondEdit, L"0");
        MessageBoxW(hwnd, L"Выключение отменено!", L"Отмена", MB_OK | MB_ICONINFORMATION);
    }
}

void SetTimerAndShowMessage(HWND hwnd, int minutes, int seconds = 0) {
    int totalSeconds = minutes * 60 + seconds;
    if (totalSeconds > 0) {
        remainingSeconds = totalSeconds;
        lastChanceShown = false;

        std::wstring msg = L"Компьютер будет выключен через ";
        if (minutes > 0) msg += std::to_wstring(minutes) + L" мин ";
        if (seconds > 0) msg += std::to_wstring(seconds) + L" сек.";
        MessageBoxW(hwnd, msg.c_str(), L"Таймер установлен", MB_OK | MB_ICONINFORMATION);

        SetTimer(hwnd, 1, 1000, NULL);
    }
}

// Функция для установки закругления углов Windows 11
void SetWindowRoundedCorners(HWND hwnd) {
    enum DWMWINDOWCORNERPREFERENCE {
        DWMWCP_DEFAULT = 0,
        DWMWCP_DONOTROUND = 1,
        DWMWCP_ROUND = 2,
        DWMWCP_ROUNDSMALL = 3
    };

    DWMWINDOWCORNERPREFERENCE preference = DWMWCP_ROUND;
    // 33 — DWMWA_WINDOW_CORNER_PREFERENCE
    DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference));
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
    {
        hSegoeFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        CreateWindowW(L"STATIC", L"Время до выключения (минуты):",
            WS_VISIBLE | WS_CHILD, 20, 15, 220, 20, hwnd, NULL, NULL, NULL);

        hTimerEdit = CreateWindowW(L"EDIT", L"30",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER,
            20, 35, 220, 25, hwnd, (HMENU)ID_TIMER_EDIT, NULL, NULL);

        CreateWindowW(L"STATIC", L"Дополнительно (секунды):",
            WS_VISIBLE | WS_CHILD, 20, 70, 220, 20, hwnd, NULL, NULL, NULL);

        hSecondEdit = CreateWindowW(L"EDIT", L"0",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER,
            20, 90, 220, 25, hwnd, (HMENU)ID_SECOND_EDIT, NULL, NULL);

        CreateWindowW(L"BUTTON", L"30 мин", WS_VISIBLE | WS_CHILD, 20, 125, 70, 25,
            hwnd, (HMENU)ID_PRESET_30, NULL, NULL);
        CreateWindowW(L"BUTTON", L"60 мин", WS_VISIBLE | WS_CHILD, 95, 125, 70, 25,
            hwnd, (HMENU)ID_PRESET_60, NULL, NULL);
        CreateWindowW(L"BUTTON", L"120 мин", WS_VISIBLE | WS_CHILD, 170, 125, 70, 25,
            hwnd, (HMENU)ID_PRESET_120, NULL, NULL);

        hStartButton = CreateWindowW(L"BUTTON", L"Запустить",
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            20, 165, 105, 30, hwnd, (HMENU)ID_START_BUTTON, NULL, NULL);

        hCancelButton = CreateWindowW(L"BUTTON", L"Отменить",
            WS_VISIBLE | WS_CHILD,
            135, 165, 105, 30, hwnd, (HMENU)ID_CANCEL_BUTTON, NULL, NULL);

        HWND ctrls[] = { hTimerEdit, hSecondEdit, hStartButton, hCancelButton };
        for (HWND ctrl : ctrls)
            SendMessageW(ctrl, WM_SETFONT, (WPARAM)hSegoeFont, TRUE);

        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_START_BUTTON:
        {
            wchar_t bufMin[10], bufSec[10];
            GetWindowTextW(hTimerEdit, bufMin, 10);
            GetWindowTextW(hSecondEdit, bufSec, 10);
            int minutes = _wtoi(bufMin);
            int seconds = _wtoi(bufSec);
            KillTimer(hwnd, 1);
            SetTimerAndShowMessage(hwnd, minutes, seconds);
            break;
        }
        case ID_PRESET_30:
            KillTimer(hwnd, 1);
            SetWindowText(hTimerEdit, L"30");
            SetWindowText(hSecondEdit, L"0");
            SetTimerAndShowMessage(hwnd, 30, 0);
            break;
        case ID_PRESET_60:
            KillTimer(hwnd, 1);
            SetWindowText(hTimerEdit, L"60");
            SetWindowText(hSecondEdit, L"0");
            SetTimerAndShowMessage(hwnd, 60, 0);
            break;
        case ID_PRESET_120:
            KillTimer(hwnd, 1);
            SetWindowText(hTimerEdit, L"120");
            SetWindowText(hSecondEdit, L"0");
            SetTimerAndShowMessage(hwnd, 120, 0);
            break;
        case ID_CANCEL_BUTTON:
            KillTimer(hwnd, 1);
            remainingSeconds = 0;
            lastChanceShown = false;
            SetWindowText(hTimerEdit, L"0");
            SetWindowText(hSecondEdit, L"0");
            MessageBoxW(hwnd, L"Таймер отменен.", L"Информация", MB_OK | MB_ICONINFORMATION);
            break;
        }
        break;

    case WM_TIMER:
        if (remainingSeconds > 0) {
            remainingSeconds--;
            if (remainingSeconds == 60 && !lastChanceShown) {
                lastChanceShown = true;
                ShowLastChanceDialog(hwnd);
            }

            int min = remainingSeconds / 60;
            int sec = remainingSeconds % 60;
            SetWindowText(hTimerEdit, std::to_wstring(min).c_str());
            SetWindowText(hSecondEdit, std::to_wstring(sec).c_str());
        }
        else {
            KillTimer(hwnd, 1);
            if (!ShutdownComputerWithPrivilege())
                MessageBoxW(hwnd, L"Не удалось выключить компьютер.", L"Ошибка", MB_OK | MB_ICONERROR);
            DestroyWindow(hwnd);
        }
        break;

    case WM_DESTROY:
        DeleteObject(hSegoeFont);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    RemoveFromAutostart();

    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icex);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ShutdownTimerClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIcon(NULL, IDI_INFORMATION);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"ShutdownTimerClass", L"Таймер выключения ПК",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 270, 250, NULL, NULL, hInstance, NULL);

    SetWindowRoundedCorners(hwnd);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

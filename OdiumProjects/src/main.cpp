#include <windows.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <wrl.h>
#include <WebView2.h>

#include "backend.h"

using namespace Microsoft::WRL;

namespace {

HWND g_hwnd = nullptr;
fs::path g_exeDir;
Backend *g_backend = nullptr;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;

void resizeWebView() {
    if (!g_controller) return;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    g_controller->put_Bounds(rc);
}

void fail(const wchar_t *text) {
    MessageBoxW(g_hwnd, text, L"Odium Projects", MB_ICONERROR | MB_OK);
    PostQuitMessage(1);
}

HRESULT onWebMessage(ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) {
    LPWSTR raw = nullptr;
    if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
        const std::wstring msg(raw);
        CoTaskMemFree(raw);
        // protocol: id \x1f command \x1f arg1 \x1f arg2 ...
        auto parts = split(msg, L'\x1f');
        if (parts.size() >= 2) {
            const int id = _wtoi(parts[0].c_str());
            std::vector<std::wstring> a(parts.begin() + 2, parts.end());
            const std::wstring reply = g_backend->handle(id, parts[1], a, g_hwnd);
            g_webview->PostWebMessageAsJson(reply.c_str());
        }
    }
    return S_OK;
}

HRESULT onControllerCreated(HRESULT hr, ICoreWebView2Controller *controller) {
    if (FAILED(hr) || !controller) { fail(L"Couldn't start the WebView2 control."); return hr; }
    g_controller = controller;
    g_controller->get_CoreWebView2(&g_webview);

    ComPtr<ICoreWebView2Settings> settings;
    g_webview->get_Settings(&settings);
    settings->put_IsStatusBarEnabled(FALSE);
    settings->put_IsZoomControlEnabled(FALSE);
#ifndef _DEBUG
    settings->put_AreDefaultContextMenusEnabled(FALSE);
    settings->put_AreDevToolsEnabled(FALSE);
    ComPtr<ICoreWebView2Settings3> settings3;
    if (SUCCEEDED(settings.As(&settings3))) settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
#endif

    ComPtr<ICoreWebView2Controller2> controller2;
    if (SUCCEEDED(g_controller.As(&controller2))) {
        COREWEBVIEW2_COLOR bg{255, 8, 7, 11};
        controller2->put_DefaultBackgroundColor(bg);
    }

    ComPtr<ICoreWebView2_3> wv3;
    if (FAILED(g_webview.As(&wv3))) { fail(L"This version of the WebView2 runtime is too old. Please update Microsoft Edge WebView2."); return E_FAIL; }
    wv3->SetVirtualHostNameToFolderMapping(L"odium.projects", (g_exeDir / L"ui").c_str(),
                                           COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);

    EventRegistrationToken token;
    g_webview->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
        [](ICoreWebView2 *s, ICoreWebView2WebMessageReceivedEventArgs *a) -> HRESULT { return onWebMessage(s, a); }).Get(), &token);

    resizeWebView();
    g_webview->Navigate(L"https://odium.projects/index.html");
    return S_OK;
}

HRESULT onEnvironmentCreated(HRESULT hr, ICoreWebView2Environment *env) {
    if (FAILED(hr) || !env) {
        fail(L"The Microsoft Edge WebView2 Runtime isn't installed.\n\nInstall it from https://developer.microsoft.com/microsoft-edge/webview2/ and start the app again.");
        return hr;
    }
    return env->CreateCoreWebView2Controller(g_hwnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [](HRESULT hr, ICoreWebView2Controller *c) -> HRESULT { return onControllerCreated(hr, c); }).Get());
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        resizeWebView();
        return 0;
    case WM_GETMINMAXINFO: {
        const UINT dpi = GetDpiForWindow(hwnd);
        auto *mmi = reinterpret_cast<MINMAXINFO *>(lp);
        mmi->ptMinTrackSize.x = MulDiv(900, dpi, 96);
        mmi->ptMinTrackSize.y = MulDiv(600, dpi, 96);
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT *r = reinterpret_cast<RECT *>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SETFOCUS:
        if (g_controller) g_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    g_exeDir = fs::path(exePath).parent_path();

    Backend backend(g_exeDir);
    g_backend = &backend;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(8, 7, 11));
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = L"OdiumProjectsWindow";
    RegisterClassExW(&wc);

    const UINT dpi = GetDpiForSystem();
    RECT rc{0, 0, MulDiv(1160, dpi, 96), MulDiv(760, dpi, 96)};
    AdjustWindowRectExForDpi(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Odium Projects", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInst, nullptr);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, 20, &dark, sizeof(dark));
    COLORREF caption = RGB(8, 7, 11);
    DwmSetWindowAttribute(g_hwnd, 35, &caption, sizeof(caption));

    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);

    const std::wstring local = envVar(L"LOCALAPPDATA");
    const std::wstring userData = (fs::path(local.empty() ? g_exeDir.wstring() : local) / L"OdiumProjects" / L"WebView2").wstring();
    CreateCoreWebView2EnvironmentWithOptions(nullptr, userData.c_str(), nullptr,
                                             Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT hr, ICoreWebView2Environment *e) -> HRESULT { return onEnvironmentCreated(hr, e); }).Get());

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return (int)msg.wParam;
}

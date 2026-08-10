/**
 * @file         main.cpp
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-100
 * @version      1.0.0
 */

#include <Windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cassert>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

#include "Common/AgentConfig.h"
#include "Agent/CAgentCapture.h"
#include "Agent/CAgentEncoder.h"
#include "Agent/CAgentServer.h"
#include "UI/CAgentWindow.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 트레이 알림 메시지 ID
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT IDM_TRAY_OPEN = 100;
constexpr UINT IDM_TRAY_SETTINGS = 101;
constexpr UINT IDM_TRAY_EXIT = 102;
constexpr UINT TRAY_ICON_ID = 1;

// 앱 전역 상태 (WndProc와 공유)
static HWND g_hWnd = nullptr;
static CAgentWindow* g_pAgentWin  = nullptr;
static bool g_bExit = false;

/**
 * @brief 트레이 아이콘 등록
 */
static void AddTrayIcon(HWND hWnd)
{
    NOTIFYICONDATAW stNid{};
    {
        stNid.cbSize = sizeof(NOTIFYICONDATAW);
        stNid.hWnd = hWnd;
        stNid.uID = TRAY_ICON_ID;
        stNid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        stNid.uCallbackMessage = WM_TRAYICON;
        stNid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wcscpy_s(stNid.szTip, L"WinScrCap Agent");
    Shell_NotifyIconW(NIM_ADD, &stNid);
}

/**
 * @brief 트레이 아이콘 제거
 */
static void RemoveTrayIcon(HWND hWnd)
{
    NOTIFYICONDATAW stNid{};
    {
        stNid.cbSize = sizeof(NOTIFYICONDATAW);
        stNid.hWnd = hWnd;
        stNid.uID = TRAY_ICON_ID;
    }
    Shell_NotifyIconW(NIM_DELETE, &stNid);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    {
        return 1;
    }

    switch (msg)
    {
        case WM_CLOSE:
            // X 버튼 → 창 숨기고 트레이 유지 (종료 아님)
            ShowWindow(hWnd, SW_HIDE);
            return 0;
        case WM_TRAYICON:
        {
            if (lParam == WM_RBUTTONUP)
            {
                // 트레이 우클릭 메뉴
                POINT stPt{};
                GetCursorPos(&stPt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDM_TRAY_OPEN, L"Open");
                AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SETTINGS, L"Settings");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Exit");

                SetForegroundWindow(hWnd);
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, stPt.x, stPt.y, 0, hWnd, nullptr);
                DestroyMenu(hMenu);
            }
            else if (lParam == WM_LBUTTONDBLCLK)
            {
                ShowWindow(hWnd, SW_SHOW);
                SetForegroundWindow(hWnd);
            }
            return 0;
        }
        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
                case IDM_TRAY_OPEN:
                    ShowWindow(hWnd, SW_SHOW);
                    SetForegroundWindow(hWnd);
                    break;
                case IDM_TRAY_SETTINGS:
                    if (g_pAgentWin != nullptr)
                    {
                        ShowWindow(hWnd, SW_SHOW);
                        SetForegroundWindow(hWnd);
                        g_pAgentWin->RequestOpenSettings();
                    }
                    break;
                case IDM_TRAY_EXIT:
                    g_bExit = true;
                    PostQuitMessage(0);
                    break;
                default:
                    break;
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE,
    _In_     LPWSTR,
    _In_     int
)
{
#if defined(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#else
    SetProcessDPIAware();
#endif

    wchar_t szIniPath[MAX_PATH] = {};
    GetAgentIniPath(szIniPath, ARRAYSIZE(szIniPath));

    SAgentConfig stConfig{};
    LoadAgentConfig(szIniPath, &stConfig);

    constexpr wchar_t CLASS_NAME[] = L"WinScrCapAgentWnd";
    WNDCLASSEXW stWc{};
    {
        stWc.cbSize = sizeof(WNDCLASSEXW);
        stWc.style = CS_CLASSDC;
        stWc.lpfnWndProc = WndProc;
        stWc.hInstance = hInstance;
        stWc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        stWc.lpszClassName = CLASS_NAME;
    }
    RegisterClassExW(&stWc);

    g_hWnd = CreateWindowExW(0, CLASS_NAME, L"WinScrCap Agent",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 380, 280,
        nullptr, nullptr, hInstance, nullptr);
    if (g_hWnd == nullptr) { return -1; }

    DXGI_SWAP_CHAIN_DESC stSCDesc{};
    {
        stSCDesc.BufferCount = 2;
        stSCDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        stSCDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        stSCDesc.OutputWindow = g_hWnd;
        stSCDesc.SampleDesc.Count = 1;
        stSCDesc.Windowed = TRUE;
        stSCDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        stSCDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    }

    UINT nCreateFlags = 0;
#ifdef _DEBUG
    nCreateFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pDeviceContext = nullptr;
    IDXGISwapChain* pSwapChain = nullptr;
    D3D_FEATURE_LEVEL eFeatureLevel{};

    constexpr D3D_FEATURE_LEVEL ARR_FL[] = { D3D_FEATURE_LEVEL_11_0 };
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, nCreateFlags,
        ARR_FL, ARRAYSIZE(ARR_FL), D3D11_SDK_VERSION,
        &stSCDesc, &pSwapChain, &pDevice, &eFeatureLevel, &pDeviceContext);

    // 멀티스레드 보호 활성화
    ID3D10Multithread* pThread = nullptr;
    if (SUCCEEDED(pDeviceContext->QueryInterface(__uuidof(ID3D10Multithread), reinterpret_cast<void**>(&pThread))))
    {
        pThread->SetMultithreadProtected(TRUE);
        pThread->Release();
        pThread = nullptr;
    }

    if (FAILED(hr))
    {
        MessageBoxW(nullptr, L"DirectX 11 초기화 실패.", L"오류", MB_ICONERROR | MB_OK);
        return -1;
    }

    CAgentCapture cCapture(pDevice, pDeviceContext);
    if (!cCapture.Initialize(stConfig.MonitorIndex))
    {
        MessageBoxW(nullptr, L"캡처 초기화 실패 (모니터 인덱스를 확인하세요).", L"오류",
            MB_ICONERROR | MB_OK);
        pSwapChain->Release();
        pDeviceContext->Release();
        pDevice->Release();
        return -1;
    }

    CAgentEncoder cEncoder;
    if (!cEncoder.Open(cCapture.GetWidth(), cCapture.GetHeight(), stConfig.Fps, stConfig.Crf))
    {
        MessageBoxW(nullptr, L"인코더 초기화 실패.", L"오류", MB_ICONERROR | MB_OK);
        pSwapChain->Release();
        pDeviceContext->Release();
        pDevice->Release();
        return -1;
    }

    CAgentServer cServer;
    if (!cServer.Initialize(stConfig.Port, stConfig.szPassword, &cCapture, &cEncoder))
    {
        MessageBoxW(nullptr, L"서버 소켓 바인딩 실패.", L"오류", MB_ICONERROR | MB_OK);
        pSwapChain->Release();
        pDeviceContext->Release();
        pDevice->Release();
        return -1;
    }

    CAgentWindow cAgentWin(pDevice, pDeviceContext, pSwapChain, g_hWnd, &cServer);
    g_pAgentWin = &cAgentWin;

    if (!cAgentWin.Initialize())
    {
        pSwapChain->Release();
        pDeviceContext->Release();
        pDevice->Release();
        return -1;
    }

    AddTrayIcon(g_hWnd);
    ShowWindow(g_hWnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hWnd);

    MSG stMsg = {};
    while (stMsg.message != WM_QUIT && !g_bExit)
    {
        if (PeekMessage(&stMsg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&stMsg);
            DispatchMessage(&stMsg);
            continue;
        }

        // 창이 숨겨진 상태에서는 렌더링 스킵 (CPU 절약)
        if (!IsWindowVisible(g_hWnd))
        {
            Sleep(50);
            continue;
        }

        if (!cAgentWin.Render(stConfig, szIniPath))
        {
            PostQuitMessage(0);
        }
    }

    RemoveTrayIcon(g_hWnd);
    g_pAgentWin = nullptr;

    cAgentWin.Shutdown();
    cServer.Shutdown();
    cEncoder.Close();
    cCapture.Shutdown();

    pSwapChain->Release();
    pDeviceContext->Release();
    pDevice->Release();

    DestroyWindow(g_hWnd);
    UnregisterClassW(CLASS_NAME, hInstance);

    return 0;
}

/**
 * @file         CApplication.cpp
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-10
 * @version      1.0.0
 */

#include <Windows.h>
#include <d3d11.h>
#include <cassert>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

#include "CApplication.h"
#include "UI/CMainWindow.h"
#include "Capture/CCaptureManager.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

CApplication::CApplication()
    : m_hWnd(nullptr)
    , m_pDevice(nullptr)
    , m_pDeviceContext(nullptr)
    , m_pSwapChain(nullptr)
    , m_pRenderTargetView(nullptr)
    , m_cMainWindow(nullptr)
    , m_cCaptureManager(nullptr)
    , m_bImGuiInitialized(false)
{
}

CApplication::~CApplication()
{
    Shutdown();
}

bool CApplication::Initialize(HINSTANCE hInstance)
{
    if (!InitWindow(hInstance))
    {
        return false;
    }
    if (!InitDirectX())
    {
        return false;
    }
    if (!InitImGui())
    {
        return false;
    }

    m_cCaptureManager = std::make_unique<CCaptureManager>(m_pDevice, m_pDeviceContext);
    if (!m_cCaptureManager->Initialize())
    {
        return false;
    }

    m_cMainWindow = std::make_unique<CMainWindow>(m_cCaptureManager.get());

    return true;
}

int CApplication::Run()
{
    MSG stMsg = {};

    while (stMsg.message != WM_QUIT)
    {
        if (PeekMessage(&stMsg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&stMsg);
            DispatchMessage(&stMsg);
            continue;
        }

        BeginFrame();
        m_cCaptureManager->Update();
        m_cMainWindow->Render();
        EndFrame();
    }

    return static_cast<int>(stMsg.wParam);
}

void CApplication::Shutdown()
{
    m_cMainWindow.reset();
    m_cCaptureManager.reset();   // DX 리소스 해제 전에 반드시 먼저 정리

    if (m_bImGuiInitialized)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_bImGuiInitialized = false;
    }

    CleanupRenderTarget();

    if (m_pSwapChain != nullptr)
    {
        m_pSwapChain->Release();
        m_pSwapChain = nullptr;
    }
    if (m_pDeviceContext != nullptr)
    {
        m_pDeviceContext->Release();
        m_pDeviceContext = nullptr;
    }
    if (m_pDevice != nullptr)
    {
#ifdef _DEBUG
        ID3D11Debug* pD3dDebug = nullptr;
        if (SUCCEEDED(m_pDevice->QueryInterface(IID_PPV_ARGS(&pD3dDebug))))
        {
            pD3dDebug->ReportLiveDeviceObjects(
                static_cast<D3D11_RLDO_FLAGS>(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL)
            );
            pD3dDebug->Release();
        }
#endif
        m_pDevice->Release();
        m_pDevice = nullptr;
    }

    if (m_hWnd != nullptr)
    {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }

    UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
}

bool CApplication::InitWindow(HINSTANCE hInstance)
{
    WNDCLASSEXW stWc{};
    {
        stWc.cbSize = sizeof(WNDCLASSEXW);
        stWc.style = CS_CLASSDC;
        stWc.lpfnWndProc = WndProc;
        stWc.hInstance = hInstance;
        stWc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        stWc.lpszClassName = WINDOW_CLASS_NAME;
    }

    if (!RegisterClassExW(&stWc))
    {
        return false;
    }

    // this를 lpCreateParams로 전달 → WM_NCCREATE에서 GWLP_USERDATA에 저장
    m_hWnd = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        nullptr, nullptr,
        hInstance,
        this
    );

    if (m_hWnd == nullptr)
    {
        return false;
    }

    ShowWindow(m_hWnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hWnd);

    return true;
}

bool CApplication::InitDirectX()
{
    DXGI_SWAP_CHAIN_DESC stSwapDesc{};
    {
        stSwapDesc.BufferCount = 2;
        stSwapDesc.BufferDesc.Width = 0;
        stSwapDesc.BufferDesc.Height = 0;
        stSwapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        stSwapDesc.BufferDesc.RefreshRate.Numerator = 60;
        stSwapDesc.BufferDesc.RefreshRate.Denominator = 1;
        stSwapDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        stSwapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        stSwapDesc.OutputWindow = m_hWnd;
        stSwapDesc.SampleDesc.Count = 1;
        stSwapDesc.SampleDesc.Quality = 0;
        stSwapDesc.Windowed = TRUE;
        stSwapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    }

    UINT nCreateFlags = 0;
#ifdef _DEBUG
    nCreateFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    constexpr D3D_FEATURE_LEVEL ARR_FEATURE_LEVELS[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL eFeatureLevel{};
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        nCreateFlags,
        ARR_FEATURE_LEVELS,
        ARRAYSIZE(ARR_FEATURE_LEVELS),
        D3D11_SDK_VERSION,
        &stSwapDesc,
        &m_pSwapChain,
        &m_pDevice,
        &eFeatureLevel,
        &m_pDeviceContext
    );
    if (FAILED(hr))
    {
        return false;
    }
    // 멀티스레드 보호 활성화
    ID3D10Multithread* pThread = nullptr;
    if (SUCCEEDED(m_pDeviceContext->QueryInterface(__uuidof(ID3D10Multithread), reinterpret_cast<void**>(&pThread))))
    {
        pThread->SetMultithreadProtected(TRUE);
        pThread->Release();
        pThread = nullptr;
    }

    CreateRenderTarget();
    return true;
}

bool CApplication::InitImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& refIo = ImGui::GetIO();
    refIo.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    refIo.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(m_hWnd))
    {
        return false;
    }

    if (!ImGui_ImplDX11_Init(m_pDevice, m_pDeviceContext))
    {
        return false;
    }

    m_bImGuiInitialized = true;
    return true;
}

void CApplication::BeginFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void CApplication::EndFrame()
{
    ImGui::Render();

    constexpr float CLEAR_COLOR[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, nullptr);
    m_pDeviceContext->ClearRenderTargetView(m_pRenderTargetView, CLEAR_COLOR);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_pSwapChain->Present(1, 0);
}

void CApplication::CleanupRenderTarget()
{
    if (m_pRenderTargetView != nullptr)
    {
        m_pRenderTargetView->Release();
        m_pRenderTargetView = nullptr;
    }
}

void CApplication::CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    const HRESULT hr = m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));

    if (SUCCEEDED(hr) && pBackBuffer != nullptr)
    {
        m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pRenderTargetView);
        pBackBuffer->Release();
    }
}

LRESULT CALLBACK CApplication::WndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam)
{
    if (nMsg == WM_NCCREATE)
    {
        const CREATESTRUCTW* pCreate = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreate->lpCreateParams));
    }

    if (ImGui_ImplWin32_WndProcHandler(hWnd, nMsg, wParam, lParam))
    {
        return TRUE;
    }

    CApplication* pApp = reinterpret_cast<CApplication*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (nMsg)
    {
        case WM_SIZE:
        {
            if (pApp != nullptr &&
                pApp->m_pSwapChain != nullptr &&
                wParam != static_cast<WPARAM>(SIZE_MINIMIZED))
            {
                pApp->CleanupRenderTarget();
                pApp->m_pDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
                pApp->m_pSwapChain->ResizeBuffers(
                    0,
                    static_cast<UINT>(LOWORD(lParam)),
                    static_cast<UINT>(HIWORD(lParam)),
                    DXGI_FORMAT_UNKNOWN,
                    0
                );
                pApp->CreateRenderTarget();
            }
            break;
        }
        case WM_DPICHANGED:
        {
            // DPI 변경 시 시스템이 권장하는 크기로 창 조정
            const RECT* pSuggest = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(
                hWnd, nullptr,
                pSuggest->left,
                pSuggest->top,
                pSuggest->right  - pSuggest->left,
                pSuggest->bottom - pSuggest->top,
                SWP_NOZORDER | SWP_NOACTIVATE
            );
            break;
        }
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            break;
        }
        default:
            break;
    }

    return DefWindowProcW(hWnd, nMsg, wParam, lParam);
}

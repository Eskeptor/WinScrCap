/**
 * @file         CAgentWindow.cpp
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-10
 * @version      1.0.0
 */

#include <Windows.h>
#include <d3d11.h>
#include <cassert>
#include <cstring>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

#include "CAgentWindow.h"
#include "Agent/CAgentServer.h"

CAgentWindow::CAgentWindow(ID3D11Device* pDevice,
    ID3D11DeviceContext* pDeviceContext,
    IDXGISwapChain* pSwapChain,
    HWND hWnd,
    CAgentServer* pServer)
    : m_hWnd(hWnd)
    , m_pDevice(pDevice)
    , m_pDeviceContext(pDeviceContext)
    , m_pSwapChain(pSwapChain)
    , m_pRTV(nullptr)
    , m_pServer(pServer)
    , m_bImGuiInitialized(false)
    , m_bOpenSettings(false)
    , m_bExitRequested(false)
    , m_stEditConfig{}
    , m_szEditPassword{}
{
    assert(pDevice != nullptr);
    assert(pServer != nullptr);
}

CAgentWindow::~CAgentWindow()
{
    Shutdown();
}

bool CAgentWindow::Initialize()
{
    // RTV 생성
    ID3D11Texture2D* pBackBuf = nullptr;
    HRESULT hr = m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuf));
    if (FAILED(hr))
    {
        return false;
    }
    hr = m_pDevice->CreateRenderTargetView(pBackBuf, nullptr, &m_pRTV);
    pBackBuf->Release();
    if (FAILED(hr))
    {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& refIo = ImGui::GetIO();
    refIo.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const char* pszFonts[] =
    {
        "C:\\Windows\\Fonts\\NanumGothic.ttf",
        "C:\\Windows\\Fonts\\malgun.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
    };
    for (int i = 0; i < ARRAYSIZE(pszFonts); ++i)
    {
        ImFont* pFont = refIo.Fonts->AddFontFromFileTTF(pszFonts[i], 14.0f, nullptr, refIo.Fonts->GetGlyphRangesKorean());
        if (pFont != nullptr)
        {
            break;
        }
    }

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

void CAgentWindow::Shutdown()
{
    if (m_bImGuiInitialized)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_bImGuiInitialized = false;
    }
    if (m_pRTV != nullptr)
    {
        m_pRTV->Release();
        m_pRTV = nullptr;
    }
}

bool CAgentWindow::Render(SAgentConfig& stConfig, const wchar_t* lpszIniPath)
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* pVP = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(pVP->WorkPos);
    ImGui::SetNextWindowSize(pVP->WorkSize);
    constexpr ImGuiWindowFlags WIN_FLAGS =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_MenuBar;

    ImGui::Begin("##Main", nullptr, WIN_FLAGS);
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("Settings"))
            {
                if (ImGui::MenuItem("Configure..."))
                {
                    m_stEditConfig = stConfig;
                    strncpy_s(m_szEditPassword, stConfig.szPassword, _TRUNCATE);
                    m_bOpenSettings = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Exit"))
            {
                if (ImGui::MenuItem("Exit Application"))
                {
                    m_bExitRequested = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        RenderMainBody(stConfig);
        RenderSettingsDialog(stConfig, lpszIniPath);
    }
    ImGui::End();

    ImGui::Render();

    constexpr float ARR_CLEAR[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    m_pDeviceContext->OMSetRenderTargets(1, &m_pRTV, nullptr);
    m_pDeviceContext->ClearRenderTargetView(m_pRTV, ARR_CLEAR);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_pSwapChain->Present(1, 0);

    return !m_bExitRequested;
}

void CAgentWindow::HideToTray()
{
    ShowWindow(m_hWnd, SW_HIDE);
}

void CAgentWindow::RenderMainBody(const SAgentConfig& stConfig)
{
    ImGui::Spacing();
    ImGui::SetWindowFontScale(1.2f);
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "WinScrCap Agent");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Port         : %u",    static_cast<uint32_t>(stConfig.Port));
    ImGui::Text("FPS          : %d",    stConfig.Fps);
    ImGui::Text("Quality(CRF) : %d",    stConfig.Crf);
    ImGui::Text("Monitor      : %d",    stConfig.MonitorIndex);
    ImGui::Text("Password     : %s",    stConfig.szPassword[0] != '\0' ? "***" : "(none)");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const bool bConnected = m_pServer->IsConnected();
    if (bConnected)
    {
        const std::string strIP = m_pServer->GetClientIP();
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Status: Connected  [%s]", strIP.c_str());
    }
    else if (m_pServer->IsRunning())
    {
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.2f, 1.0f), "Status: Waiting for connection...");
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Status: Not running");
    }
}

void CAgentWindow::RenderSettingsDialog(SAgentConfig& stConfig, const wchar_t* lpszIniPath)
{
    if (m_bOpenSettings)
    {
        ImGui::OpenPopup("Settings##Modal");
        m_bOpenSettings = false;
    }

    const ImVec2 vCenter = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(vCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f));

    if (ImGui::BeginPopupModal("Settings##Modal", nullptr, ImGuiWindowFlags_NoResize))
    {
        int32_t nPort = static_cast<int32_t>(m_stEditConfig.Port);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Port    "); ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputInt("##Port", &nPort))
        {
            nPort = (nPort < 1024) ? 1024 : nPort;
            nPort = (nPort > 65535) ? 65535 : nPort;
            m_stEditConfig.Port = static_cast<uint16_t>(nPort);
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Password"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##Pwd", m_szEditPassword, sizeof(m_szEditPassword), ImGuiInputTextFlags_Password);

        ImGui::AlignTextToFramePadding();
        ImGui::Text("FPS     "); ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputInt("##Fps", &m_stEditConfig.Fps))
        {
            m_stEditConfig.Fps = (m_stEditConfig.Fps < 1) ? 1 : m_stEditConfig.Fps;
            m_stEditConfig.Fps = (m_stEditConfig.Fps > 60) ? 60 : m_stEditConfig.Fps;
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("CRF     "); ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputInt("##Crf", &m_stEditConfig.Crf))
        {
            m_stEditConfig.Crf = (m_stEditConfig.Crf < 0) ? 0 : m_stEditConfig.Crf;
            m_stEditConfig.Crf = (m_stEditConfig.Crf > 51) ? 51 : m_stEditConfig.Crf;
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Monitor "); ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputInt("##Mon", &m_stEditConfig.MonitorIndex))
        {
            if (m_stEditConfig.MonitorIndex < 0)
            {
                m_stEditConfig.MonitorIndex = 0;
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("* Restart required for Port/Monitor changes.");
        ImGui::Spacing();

        const float fBtnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Save", ImVec2(fBtnW, 0.0f)))
        {
            strncpy_s(m_stEditConfig.szPassword, m_szEditPassword, _TRUNCATE);
            stConfig = m_stEditConfig;
            if (lpszIniPath != nullptr)
            {
                SaveAgentConfig(lpszIniPath, stConfig);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(-1.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

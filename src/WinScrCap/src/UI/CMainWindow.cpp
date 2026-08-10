/**
 * @file         CMainWindow.cpp
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-10
 * @version      1.0.0
 */

#include <Windows.h>
#include <d3d11.h>
#include <string>
#include <cassert>
#include <commdlg.h>

#include <imgui.h>
#include <imgui_internal.h>    // DockBuilderXxx API

#include "CMainWindow.h"
#include "Capture/CCaptureManager.h"
#include "Capture/CScreenCapture.h"
#include "Capture/CWindowCapture.h"

namespace
{
    // UTF-16 → UTF-8 변환 (ImGui는 UTF-8 사용)
    std::string WStrToUtf8(const std::wstring& strWide)
    {
        if (strWide.empty())
        {
            return {};
        }
        const int32_t nLen = WideCharToMultiByte(CP_UTF8, 0, strWide.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string strResult(static_cast<size_t>(nLen), '\0');
        WideCharToMultiByte(CP_UTF8, 0, strWide.c_str(), -1, strResult.data(), nLen, nullptr, nullptr);
        if (!strResult.empty() &&
            strResult.back() == '\0')
        {
            strResult.pop_back();
        }
        return strResult;
    }

    // UTF-8 → UTF-16 변환 (파일 다이얼로그 등 시스템 API 사용 시)
    std::wstring Utf8ToWStr(const char* pszUtf8)
    {
        if (pszUtf8 == nullptr ||
            *pszUtf8 == '\0')
        {
            return {};
        }
        const int32_t nLen = MultiByteToWideChar(CP_UTF8, 0, pszUtf8, -1, nullptr, 0);
        std::wstring strResult(static_cast<size_t>(nLen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, pszUtf8, -1, strResult.data(), nLen);
        if (!strResult.empty() &&
            strResult.back() == L'\0')
        {
            strResult.pop_back();
        }
        return strResult;
    }

    // eEncoderType → 표시 문자열
    const char* EncoderTypeToStr(eEncoderType eType)
    {
        switch (eType)
        {
            case eEncoderType::NVENC:
                return "NVENC (NVIDIA)";
            case eEncoderType::AMF:
                return "AMF (AMD)";
            case eEncoderType::QSV:
                return "QSV (Intel)";
            case eEncoderType::MediaFoundation:
                return "Media Foundation";
            default:
                break;
        }
        return "Unknown";
    }
}

CMainWindow::CMainWindow(CCaptureManager* pCaptureManager)
    : m_pCaptureManager(pCaptureManager)
    , m_eCaptureType(eCaptureType::Monitor)
    , m_nSelectedMonitor(0)
    , m_nSelectedWindow(-1)
    , m_bShowImGuiDemo(false)
    , m_nTargetFps(30)
    , m_nBitrateMbps(8)
    , m_arrOutputPath{ "output.mp4" }
    , m_strLastError("")
    , m_bShowErrorPopup(false)
    , m_nMainDockID(0)
    , m_bSplitEnabled(false)
    , m_nSplitMinutes(60)
    , m_arrRemoteHost{ "192.168.0.1" }
    , m_nRemotePort(9201)
    , m_arrRemotePassword{}
{
    assert(pCaptureManager != nullptr);
}

void CMainWindow::Render()
{
    RenderDockSpace();
    RenderSourcePanel();
    RenderPreviewPanel();
    RenderControlPanel();
    RenderErrorPopup();   // 오류 팝업은 매 프레임 호출

#ifdef _IMGUI_DEMO
    if (m_bShowImGuiDemo)
    {
        ImGui::ShowDemoWindow(&m_bShowImGuiDemo);
    }
#endif
}

void CMainWindow::RenderDockSpace()
{
    const ImGuiViewport* pViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(pViewport->WorkPos);
    ImGui::SetNextWindowSize(pViewport->WorkSize);
    ImGui::SetNextWindowViewport(pViewport->ID);

    constexpr ImGuiWindowFlags DOCKSPACE_FLAGS =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##DockSpaceRoot", nullptr, DOCKSPACE_FLAGS);
    ImGui::PopStyleVar(3);

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        m_nMainDockID = ImGui::GetID("MainDockSpace");

        // imgui.ini가 없거나 DockSpace가 미초기화 상태일 때만 초기 레이아웃 적용
        if (ImGui::DockBuilderGetNode(m_nMainDockID) == nullptr)
        {
            SetupInitialLayout(m_nMainDockID);
        }

        ImGui::DockSpace(m_nMainDockID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    }

    ImGui::End();
}

void CMainWindow::SetupInitialLayout(ImGuiID nDockID)
{
    const ImGuiViewport* pViewport = ImGui::GetMainViewport();

    ImGui::DockBuilderRemoveNode(nDockID);
    ImGui::DockBuilderAddNode(nDockID, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(nDockID, pViewport->WorkSize);

    // 메인을 왼쪽 25%(Control+Source) / 오른쪽 75%(Preview)로 분할
    ImGuiID nDockLeft = 0;
    ImGuiID nDockRight = 0;
    ImGui::DockBuilderSplitNode(nDockID, ImGuiDir_Left, 0.25f, &nDockLeft, &nDockRight);

    // 왼쪽을 위(Control) / 아래(Source)로 분할
    ImGuiID nDockControl = 0;
    ImGuiID nDockSource = 0;
    ImGui::DockBuilderSplitNode(nDockLeft, ImGuiDir_Up, 0.5f, &nDockControl, &nDockSource);

    // 윈도우를 해당 노드에 고정
    ImGui::DockBuilderDockWindow("Control", nDockControl);
    ImGui::DockBuilderDockWindow("Source",  nDockSource);
    ImGui::DockBuilderDockWindow("Preview", nDockRight);

    ImGui::DockBuilderFinish(nDockID);
}

void CMainWindow::RenderSourcePanel()
{
    if (ImGui::Begin("Source"))
    {
        ImGui::SeparatorText("Capture Type");

        int32_t nType = static_cast<int32_t>(m_eCaptureType);
        ImGui::RadioButton("Monitor", &nType, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Window",  &nType, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Remote",  &nType, 2);

        if (nType != static_cast<int32_t>(m_eCaptureType))
        {
            m_eCaptureType = static_cast<eCaptureType>(nType);
            m_pCaptureManager->StopCapture();
        }

        ImGui::Spacing();

        if (m_eCaptureType == eCaptureType::Monitor)
        {
            RenderMonitorList();
        }
        else if (m_eCaptureType == eCaptureType::Window)
        {
            RenderWindowList();
        }
        else
        {
            RenderRemotePanel();
        }
    }
    ImGui::End();
}

void CMainWindow::RenderRemotePanel()
{
    ImGui::SeparatorText("Agent Connection");

    ImGui::Text("Host");
    ImGui::SameLine(80.0f);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##RemoteHost", m_arrRemoteHost, sizeof(m_arrRemoteHost));

    ImGui::Text("Port");
    ImGui::SameLine(80.0f);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputInt("##RemotePort", &m_nRemotePort, 0, 0);
    m_nRemotePort = (m_nRemotePort < 1) ? 1 : (m_nRemotePort > 65535) ? 65535 : m_nRemotePort;

    ImGui::Text("Password");
    ImGui::SameLine(80.0f);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##RemotePwd", m_arrRemotePassword, sizeof(m_arrRemotePassword),
        ImGuiInputTextFlags_Password);

    ImGui::Spacing();

    const bool bConnected = m_pCaptureManager->IsNetworkConnected();
    if (bConnected)
    {
        if (ImGui::Button("Disconnect", ImVec2(-1.0f, 0.0f)))
        {
            m_pCaptureManager->StopCapture();
        }
    }
    else
    {
        if (ImGui::Button("Connect", ImVec2(-1.0f, 0.0f)))
        {
            SRemoteTarget stTarget;
            strncpy_s(stTarget.szHost, m_arrRemoteHost, _TRUNCATE);
            stTarget.Port = static_cast<uint16_t>(m_nRemotePort);
            strncpy_s(stTarget.szPassword, m_arrRemotePassword, _TRUNCATE);

            if (!m_pCaptureManager->StartNetworkCapture(stTarget))
            {
                ShowError("Failed to start remote capture.\nCheck agent address and try again.");
            }
        }
    }

    ImGui::Spacing();

    std::string strStatus = m_pCaptureManager->GetNetworkCaptureStatus();
    if (bConnected)
    {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "%s", strStatus.c_str());
    }
    else
    {
        ImGui::TextDisabled("%s", strStatus.c_str());
    }
}

void CMainWindow::RenderPreviewPanel()
{
    if (ImGui::Begin("Preview"))
    {
        ID3D11ShaderResourceView* pSRV = m_pCaptureManager->GetPreviewSRV();

        if (pSRV != nullptr)
        {
            const int32_t nSrcW = m_pCaptureManager->GetPreviewWidth();
            const int32_t nSrcH = m_pCaptureManager->GetPreviewHeight();

            if (nSrcW > 0 &&
                nSrcH > 0)
            {
                const ImVec2 vAvail = ImGui::GetContentRegionAvail();
                const float  fSrcAspect = static_cast<float>(nSrcW) / static_cast<float>(nSrcH);

                float fDispW = vAvail.x;
                float fDispH = vAvail.x / fSrcAspect;

                if (fDispH > vAvail.y)
                {
                    fDispH = vAvail.y;
                    fDispW = vAvail.y * fSrcAspect;
                }

                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (vAvail.x - fDispW) * 0.5f);
                ImGui::Image(reinterpret_cast<ImTextureID>(pSRV), ImVec2(fDispW, fDispH));
            }
        }
        else
        {
            ImGui::TextDisabled("Select a capture source from the Source panel.");
        }
    }
    ImGui::End();
}

void CMainWindow::RenderControlPanel()
{
    if (ImGui::Begin("Control"))
    {
        ImGui::SeparatorText("Capture");

        const bool bCapturing = m_pCaptureManager->IsCapturing();
        const eRecordState eState = m_pCaptureManager->GetRecordState();
        const bool bRecActive = (eState != eRecordState::Idle);

        if (bCapturing)
        {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Capture: Active");
        }
        else
        {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Capture: Stopped");
        }
        ImGui::SameLine();
        // 녹화 중에는 캡처 중지 비활성화 (안전 보호)
        ImGui::BeginDisabled(!bCapturing || bRecActive);
        if (ImGui::Button("Stop Capture"))
        {
            m_pCaptureManager->StopCapture();
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        RenderRecordingSettings();

        ImGui::Spacing();
        ImGui::Separator();
#ifdef _IMGUI_DEMO
        ImGui::Checkbox("Show ImGui Demo", &m_bShowImGuiDemo);
#endif
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset Layout"))
        {
            SetupInitialLayout(m_nMainDockID);
        }
    }
    ImGui::End();
}

void CMainWindow::RenderRecordingSettings()
{
    const eRecordState eState = m_pCaptureManager->GetRecordState();
    const bool bRecording = (eState == eRecordState::Recording);
    const bool bPaused = (eState == eRecordState::Paused);
    const bool bActive = bRecording || bPaused;
    const bool bCapturing = m_pCaptureManager->IsCapturing();
 
    ImGui::SeparatorText("Recording");

    // ---- 녹화 중 상태 표시 ----
    if (bActive)
    {
        if (bRecording)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[REC]");
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "[PAUSED]");
        }

        // 경과 시간 (HH:MM:SS)
        const double fSec = m_pCaptureManager->GetElapsedSeconds();
        const int32_t nH = static_cast<int32_t>(fSec) / 3600;
        const int32_t nM = (static_cast<int32_t>(fSec) % 3600) / 60;
        const int32_t nS = static_cast<int32_t>(fSec) % 60;
        ImGui::SameLine();
        ImGui::Text("  %02d:%02d:%02d", nH, nM, nS);

        // 파일 크기
        const int64_t nKB = m_pCaptureManager->GetOutputFileSizeKB();
        ImGui::SameLine();
        if (nKB < 1024LL)
        {
            ImGui::Text("  |  %lld KB", nKB);
        }
        else
        {
            ImGui::Text("  |  %.1f MB", static_cast<float>(nKB) / 1024.0f);
        }

        // 프레임 수 + 활성 인코더
        ImGui::Text("Frames: %lld   Encoder: %s",
            m_pCaptureManager->GetRecordedFrames(),
            EncoderTypeToStr(m_pCaptureManager->GetActiveEncoderType()));

        // 분할 녹화 중이면 현재 파일명 표시
        if (m_bSplitEnabled)
        {
            const std::wstring& strPath = m_pCaptureManager->GetCurrentOutputPath();
            if (!strPath.empty())
            {
                // 경로에서 파일명만 추출
                const size_t nSlash = strPath.find_last_of(L"\\/");
                const std::wstring strFilename = (nSlash != std::wstring::npos)
                    ? strPath.substr(nSlash + 1)
                    : strPath;
                const std::string strUtf8 = WStrToUtf8(strFilename);
                ImGui::TextDisabled("File: %s", strUtf8.c_str());
            }
        }

        ImGui::Spacing();
    }

    // ---- 설정 (녹화 중 비활성화) ----
    ImGui::BeginDisabled(bActive);

    // 출력 경로 + Browse 버튼
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Output:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 72.0f);
    ImGui::InputText("##OutPath", m_arrOutputPath, sizeof(m_arrOutputPath));
    ImGui::SameLine();
    if (ImGui::Button("Browse", ImVec2(68.0f, 0.0f)))
    {
        OpenSaveFileDialog();
    }

    // FPS
    ImGui::AlignTextToFramePadding();
    ImGui::Text("FPS:    ");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::InputInt("##FPS", &m_nTargetFps))
    {
        m_nTargetFps = (m_nTargetFps < 1) ? 1 : m_nTargetFps;
        m_nTargetFps = (m_nTargetFps > 120) ? 120 : m_nTargetFps;
    }

    // Bitrate
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Bitrate:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::InputInt("##Mbps", &m_nBitrateMbps))
    {
        m_nBitrateMbps = (m_nBitrateMbps < 1) ? 1 : m_nBitrateMbps;
        m_nBitrateMbps = (m_nBitrateMbps > 200) ? 200 : m_nBitrateMbps;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Mbps");

    // 분할 녹화 설정
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Split:  ");
    ImGui::SameLine();
    ImGui::Checkbox("##SplitEn", &m_bSplitEnabled);
    if (m_bSplitEnabled)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        if (ImGui::InputInt("##SplitMin", &m_nSplitMinutes))
        {
            m_nSplitMinutes = (m_nSplitMinutes < 1) ? 1 : m_nSplitMinutes;
            m_nSplitMinutes = (m_nSplitMinutes > 1440) ? 1440 : m_nSplitMinutes;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("min");
    }

    ImGui::EndDisabled();

    ImGui::Spacing();

    // ---- 버튼 행 ----
    if (!bActive)
    {
        // 프리뷰가 쓰일 때만 녹화 시작 가능 (해상도가 확정되어야 함)
        const bool bCanRecord = bCapturing && (m_pCaptureManager->GetPreviewWidth() > 0);
        ImGui::BeginDisabled(!bCanRecord);
        if (ImGui::Button("Start Recording", ImVec2(-1.0f, 0.0f)))
        {
            SEncodeConfig stConfig;
            {
                stConfig.OutputPath = Utf8ToWStr(m_arrOutputPath);
                stConfig.Width = m_pCaptureManager->GetPreviewWidth();
                stConfig.Height = m_pCaptureManager->GetPreviewHeight();
                stConfig.FrameRate = m_nTargetFps;
                stConfig.BitRate = m_nBitrateMbps * 1000000;
                stConfig.bSplitEnabled = m_bSplitEnabled;
                stConfig.nSplitMinutes = m_nSplitMinutes;
            }
            if (!m_pCaptureManager->StartRecording(stConfig))
            {
                ShowError(
                    "Failed to start recording.\n\n"
                    "Possible causes:\n"
                    "  - FFmpeg DLL missing (third_party\\ffmpeg\\bin)\n"
                    "  - No write permission for the output path\n"
                    "  - No supported encoder available (including h264_mf)"
                );
            }
        }
        ImGui::EndDisabled();
    }
    else
    {
        // Pause/Resume + Stop 버튼 상하로 배치
        const float fHalf = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        if (bRecording)
        {
            if (ImGui::Button("Pause", ImVec2(fHalf, 0.0f)))
            {
                m_pCaptureManager->PauseRecording();
            }
        }
        else
        {
            if (ImGui::Button("Resume", ImVec2(fHalf, 0.0f)))
            {
                m_pCaptureManager->ResumeRecording();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop Recording", ImVec2(-1.0f, 0.0f)))
        {
            m_pCaptureManager->StopRecording();
        }
    }
}

void CMainWindow::OpenSaveFileDialog()
{
    wchar_t szPath[512]{0, };
    // 현재 경로를 대화상자 초기값으로 표시
    MultiByteToWideChar(CP_UTF8, 0, m_arrOutputPath, -1, szPath, ARRAYSIZE(szPath));

    OPENFILENAMEW stOfn{};
    {
        stOfn.lStructSize = sizeof(OPENFILENAMEW);
        stOfn.hwndOwner = nullptr;
        stOfn.lpstrFilter = L"MP4 Video (*.mp4)\0*.mp4\0All Files (*.*)\0*.*\0";
        stOfn.lpstrFile = szPath;
        stOfn.nMaxFile = ARRAYSIZE(szPath);
        stOfn.lpstrDefExt = L"mp4";
        stOfn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    }

    if (GetSaveFileNameW(&stOfn))
    {
        WideCharToMultiByte(
            CP_UTF8, 0,
            szPath, -1,
            m_arrOutputPath, static_cast<int32_t>(sizeof(m_arrOutputPath)),
            nullptr, nullptr);
    }
}

void CMainWindow::ShowError(const char* pszMsg)
{
    m_strLastError = (pszMsg != nullptr) ? pszMsg : "";
    m_bShowErrorPopup = true;
}

void CMainWindow::RenderErrorPopup()
{
    if (m_bShowErrorPopup)
    {
        ImGui::OpenPopup("Error##Modal");
        m_bShowErrorPopup = false;
    }

    // 매 프레임 화면 중앙에 배치
    const ImVec2 vCenter = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(vCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f));

    if (ImGui::BeginPopupModal("Error##Modal", nullptr, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextWrapped("%s", m_strLastError.c_str());
        ImGui::Spacing();

        const float fBtnX = (ImGui::GetContentRegionAvail().x - 80.0f) * 0.5f + ImGui::GetCursorPosX();
        ImGui::SetCursorPosX(fBtnX);
        if (ImGui::Button("OK", ImVec2(80.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void CMainWindow::RenderMonitorList()
{
    ImGui::SeparatorText("Monitor List");

    CScreenCapture* pScreenCapture = m_pCaptureManager->GetScreenCapture();
    if (pScreenCapture == nullptr)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Screen capture unavailable.");
        return;
    }

    const std::vector<SMonitorDesc>& arrMonitors = pScreenCapture->GetMonitorList();
    if (arrMonitors.empty())
    {
        ImGui::TextDisabled("No monitors found.");
        return;
    }

    for (const SMonitorDesc& stMon : arrMonitors)
    {
        const std::string strName = WStrToUtf8(stMon.Name);
        const std::string strLabel = std::format("{}: {}  ({}x{}){}",
            stMon.Index, strName, stMon.Width, stMon.Height, stMon.IsPrimary ? "  [Primary]" : "");

        if (ImGui::Selectable(strLabel.c_str(), m_nSelectedMonitor == stMon.Index))
        {
            m_nSelectedMonitor = stMon.Index;
            m_pCaptureManager->StartScreenCapture(m_nSelectedMonitor);
        }
    }
}

void CMainWindow::RenderWindowList()
{
    CWindowCapture* pWindowCapture = m_pCaptureManager->GetWindowCapture();

    if (pWindowCapture == nullptr)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "WGC unavailable (Windows 10 1803+ required)");
        return;
    }

    ImGui::SeparatorText("Window List");

    if (ImGui::Button("Refresh"))
    {
        m_pCaptureManager->RefreshWindowList();
        m_nSelectedWindow = -1;
    }

    ImGui::Spacing();

    const std::vector<SWindowDesc>& arrWindows = pWindowCapture->GetWindowList();
    if (arrWindows.empty())
    {
        ImGui::TextDisabled("No windows found. Click Refresh.");
        return;
    }

    for (int32_t nIdx = 0; nIdx < static_cast<int32_t>(arrWindows.size()); ++nIdx)
    {
        const SWindowDesc& stWin = arrWindows[nIdx];
        const std::string strTitle = WStrToUtf8(stWin.Title);

        if (ImGui::Selectable(strTitle.c_str(), m_nSelectedWindow == nIdx))
        {
            m_nSelectedWindow = nIdx;
            m_pCaptureManager->StartWindowCapture(stWin.hWnd);
        }
    }
}

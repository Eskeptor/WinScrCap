/**
 * @file         CCaptureManager.cpp
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 */

#include <Windows.h>
#include <d3d11.h>
#include <cassert>
#include <chrono>

#include "CCaptureManager.h"
#include "CScreenCapture.h"
#include "CWindowCapture.h"
#include "../Encoder/CVideoEncoder.h"

CCaptureManager::CCaptureManager(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext)
    : m_pDevice(pDevice)
    , m_pDeviceContext(pDeviceContext)
    , m_pActiveCapture(nullptr)
    , m_pPreviewTexture(nullptr)
    , m_pPreviewSRV(nullptr)
    , m_nPreviewWidth(0)
    , m_nPreviewHeight(0)
    , m_bFirstEncodeFrame(true)
    , m_eRecordState(eRecordState::Idle)
{
    assert(pDevice != nullptr);
    assert(pDeviceContext != nullptr);
}

CCaptureManager::~CCaptureManager()
{
    StopRecording();
    StopCapture();
    ReleasePreviewResources();
}

bool CCaptureManager::Initialize()
{
    m_cScreenCapture = std::make_unique<CScreenCapture>(m_pDevice);
    if (!m_cScreenCapture->Initialize())
    {
        return false;
    }

    // WGC 초기화 실패는 치명적이지 않음 (Windows 버전 등 환경 문제)
    m_cWindowCapture = std::make_unique<CWindowCapture>(m_pDevice);
    if (!m_cWindowCapture->Initialize())
    {
        m_cWindowCapture.reset();
    }

    return true;
}

bool CCaptureManager::StartScreenCapture(int32_t nMonitorIndex)
{
    StopCapture();

    if (!m_cScreenCapture->StartCapture(nMonitorIndex))
    {
        return false;
    }

    m_pActiveCapture = m_cScreenCapture.get();
    return true;
}

bool CCaptureManager::StartWindowCapture(HWND hWnd)
{
    if (m_cWindowCapture == nullptr)
    {
        return false;
    }

    StopCapture();

    if (!m_cWindowCapture->StartCapture(hWnd))
    {
        return false;
    }

    m_pActiveCapture = m_cWindowCapture.get();
    return true;
}

void CCaptureManager::StopCapture()
{
    StopRecording();   // 캐제 중지 전에 녹화도 안전하게 종료
    if (m_pActiveCapture != nullptr)
    {
        m_pActiveCapture->StopCapture();
        m_pActiveCapture = nullptr;
    }
}

void CCaptureManager::Update()
{
    if (m_pActiveCapture == nullptr || !m_pActiveCapture->IsCapturing())
    {
        return;
    }

    // ── 1. 캡처: 새 프레임이 있을 때만 프리뷰 텍스처 갱신 ──────────────
    // 화면 변화가 없으면 AcquireFrame()은 false 반환 → 건너뜀
    // m_pPreviewTexture에는 항상 마지막 유효 프레임이 유지됨
    {
        SFrameData stFrame{};
        if (m_pActiveCapture->AcquireFrame(&stFrame) && stFrame.pTexture != nullptr)
        {
            if (stFrame.Width != m_nPreviewWidth || stFrame.Height != m_nPreviewHeight)
            {
                CreatePreviewResources(stFrame.Width, stFrame.Height);
            }

            if (m_pPreviewTexture != nullptr)
            {
                m_pDeviceContext->CopyResource(m_pPreviewTexture, stFrame.pTexture);
            }

            m_pActiveCapture->ReleaseFrame();
        }
    }

    // ── 2. 인코딩: FPS 타이밍 기준으로 독립 실행 ──────────────────────
    // 화면 변화가 없어도 m_pPreviewTexture(마지막 프레임)를 반복 제출
    // → 영상 재생 시간이 실제 녹화 시간과 일치
    if (m_cVideoEncoder != nullptr &&
        m_cVideoEncoder->IsOpen() &&
        m_pPreviewTexture != nullptr &&
        m_eRecordState == eRecordState::Recording)
    {
        const std::chrono::steady_clock::time_point tNow = std::chrono::steady_clock::now();

        if (m_bFirstEncodeFrame)
        {
            m_tLastEncodeTime   = tNow;
            m_bFirstEncodeFrame = false;
            m_cVideoEncoder->SubmitFrame(m_pPreviewTexture);
        }
        else
        {
            const int64_t TARGET_US  = 1000000LL / static_cast<int64_t>(m_stEncodeConfig.FrameRate);
            const int64_t nElapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                tNow - m_tLastEncodeTime
            ).count();

            if (nElapsedUs >= TARGET_US)
            {
                m_cVideoEncoder->SubmitFrame(m_pPreviewTexture);
                m_tLastEncodeTime = tNow;
            }
        }

        // 분할 체크 (프레임 수 기준)
        if (m_stEncodeConfig.bSplitEnabled && m_cVideoEncoder != nullptr && m_cVideoEncoder->IsOpen())
        {
            const int64_t SPLIT_TARGET =
                static_cast<int64_t>(m_stEncodeConfig.FrameRate)
                * static_cast<int64_t>(m_stEncodeConfig.nSplitMinutes)
                * 60LL;

            if (m_cVideoEncoder->GetFrameCount() >= SPLIT_TARGET)
            {
                PerformSplit();
            }
        }
    }
}

void CCaptureManager::RefreshWindowList()
{
    if (m_cWindowCapture != nullptr)
    {
        m_cWindowCapture->RefreshWindowList();
    }
}

bool CCaptureManager::StartRecording(const SEncodeConfig& stConfig)
{
    if (!IsCapturing())
    {
        return false;
    }

    StopRecording();

    m_stEncodeConfig = stConfig;
    m_bFirstEncodeFrame = true;

    // 분할 록로딩을 위한 기본 경로 / 확장자 분리
    const size_t nDotPos = stConfig.OutputPath.rfind(L'.');
    if (nDotPos != std::wstring::npos)
    {
        m_strSplitBasePath  = stConfig.OutputPath.substr(0, nDotPos);
        m_strSplitExtension = stConfig.OutputPath.substr(nDotPos);
    }
    else
    {
        m_strSplitBasePath = stConfig.OutputPath;
        m_strSplitExtension = L".mp4";
    }

    // 분할 모드일 때 첫 번째 파일에도 타임스탬프 적용
    SEncodeConfig stFirstConfig = stConfig;
    if (stConfig.bSplitEnabled)
    {
        stFirstConfig.OutputPath = GenerateSplitPath(m_strSplitBasePath, m_strSplitExtension);
    }

    m_cVideoEncoder = std::make_unique<CVideoEncoder>(m_pDevice, m_pDeviceContext);
    if (!m_cVideoEncoder->Open(stFirstConfig))
    {
        m_cVideoEncoder.reset();
        return false;
    }

    m_eRecordState = eRecordState::Recording;
    m_tRecordStart = std::chrono::steady_clock::now();
    m_dPausedDuration = {};
    m_strOutputPath = stFirstConfig.OutputPath;
    return true;
}

void CCaptureManager::StopRecording()
{
    if (m_cVideoEncoder != nullptr)
    {
        m_cVideoEncoder->Close();
        m_cVideoEncoder.reset();
    }
    m_eRecordState = eRecordState::Idle;
    m_bFirstEncodeFrame = true;
    m_dPausedDuration = {};
    m_strOutputPath.clear();
}

bool CCaptureManager::IsRecording() const
{
    return m_cVideoEncoder != nullptr &&
        m_cVideoEncoder->IsOpen();
}

int64_t CCaptureManager::GetRecordedFrames() const
{
    if (m_cVideoEncoder == nullptr)
    {
        return 0;
    }
    return m_cVideoEncoder->GetFrameCount();
}

void CCaptureManager::PauseRecording()
{
    if (m_eRecordState != eRecordState::Recording)
    {
        return;
    }
    m_tPauseStart = std::chrono::steady_clock::now();
    m_eRecordState = eRecordState::Paused;
}

void CCaptureManager::ResumeRecording()
{
    if (m_eRecordState != eRecordState::Paused)
    {
        return;
    }
    m_dPausedDuration += std::chrono::steady_clock::now() - m_tPauseStart;
    m_eRecordState = eRecordState::Recording;
}

double CCaptureManager::GetElapsedSeconds() const
{
    if (m_eRecordState == eRecordState::Idle)
    {
        return 0.0;
    }

    const std::chrono::steady_clock::time_point tNow = std::chrono::steady_clock::now();
    std::chrono::steady_clock::duration dElapsed = tNow - m_tRecordStart - m_dPausedDuration;

    if (m_eRecordState == eRecordState::Paused)
    {
        dElapsed -= (tNow - m_tPauseStart);
    }

    return std::chrono::duration<double>(dElapsed).count();
}

int64_t CCaptureManager::GetOutputFileSizeKB() const
{
    if (m_strOutputPath.empty())
    {
        return 0;
    }

    WIN32_FILE_ATTRIBUTE_DATA stAttr{};
    if (!GetFileAttributesExW(m_strOutputPath.c_str(), GetFileExInfoStandard, &stAttr))
    {
        return 0;
    }

    LARGE_INTEGER liSize{};
    {
        liSize.LowPart = stAttr.nFileSizeLow;
        liSize.HighPart = static_cast<LONG>(stAttr.nFileSizeHigh);
    }
    return liSize.QuadPart / 1024LL;
}

eEncoderType CCaptureManager::GetActiveEncoderType() const
{
    if (m_cVideoEncoder == nullptr)
    {
        return eEncoderType::MediaFoundation;
    }
    return m_cVideoEncoder->GetActiveEncoder();
}

void CCaptureManager::PerformSplit()
{
    // 현재 파일 닫기 (av_write_trailer 포함)
    m_cVideoEncoder->Close();

    const std::wstring strNewPath = GenerateSplitPath(m_strSplitBasePath, m_strSplitExtension);
    m_strOutputPath = strNewPath;

    SEncodeConfig stNewConfig = m_stEncodeConfig;
    stNewConfig.OutputPath = strNewPath;

    if (!m_cVideoEncoder->Open(stNewConfig))
    {
        StopRecording();
        return;
    }

    m_bFirstEncodeFrame = true;
}

std::wstring CCaptureManager::GenerateSplitPath(
    const std::wstring& strBase,
    const std::wstring& strExt)
{
    SYSTEMTIME stTime{};
    GetLocalTime(&stTime);

    wchar_t szTimestamp[32]{};
    swprintf_s(szTimestamp, ARRAYSIZE(szTimestamp),
        L"%04d%02d%02d_%02d%02d",
        stTime.wYear, stTime.wMonth, stTime.wDay,
        stTime.wHour, stTime.wMinute);

    return strBase + L"_" + szTimestamp + strExt;
}

bool CCaptureManager::IsCapturing() const
{
    return m_pActiveCapture != nullptr &&
        m_pActiveCapture->IsCapturing();
}

void CCaptureManager::CreatePreviewResources(int32_t nWidth, int32_t nHeight)
{
    ReleasePreviewResources();

    D3D11_TEXTURE2D_DESC stDesc{};
    {
        stDesc.Width = static_cast<UINT>(nWidth);
        stDesc.Height = static_cast<UINT>(nHeight);
        stDesc.MipLevels = 1;
        stDesc.ArraySize = 1;
        stDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stDesc.SampleDesc.Count = 1;
        stDesc.SampleDesc.Quality = 0;
        stDesc.Usage = D3D11_USAGE_DEFAULT;
        stDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        stDesc.CPUAccessFlags = 0;
        stDesc.MiscFlags = 0;
    }

    HRESULT hr = m_pDevice->CreateTexture2D(&stDesc, nullptr, &m_pPreviewTexture);
    if (FAILED(hr))
    {
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC stSRVDesc{};
    {
        stSRVDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        stSRVDesc.Texture2D.MipLevels = 1;
        stSRVDesc.Texture2D.MostDetailedMip = 0;
    }

    hr = m_pDevice->CreateShaderResourceView(m_pPreviewTexture, &stSRVDesc, &m_pPreviewSRV);
    if (FAILED(hr))
    {
        m_pPreviewTexture->Release();
        m_pPreviewTexture = nullptr;
        return;
    }

    m_nPreviewWidth = nWidth;
    m_nPreviewHeight = nHeight;
}

void CCaptureManager::ReleasePreviewResources()
{
    if (m_pPreviewSRV != nullptr)
    {
        m_pPreviewSRV->Release();
        m_pPreviewSRV = nullptr;
    }
    if (m_pPreviewTexture != nullptr)
    {
        m_pPreviewTexture->Release();
        m_pPreviewTexture = nullptr;
    }

    m_nPreviewWidth = 0;
    m_nPreviewHeight = 0;
}

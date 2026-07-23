/**
 * @file         Types.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-07-23
 * @version      1.0.0
 * @brief        공통 타입 정의
 */

#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>

// Forward declaration - avoids including d3d11.h in this common header
struct ID3D11Texture2D;

/**
 * @struct   SFrameData
 * @brief    캡처된 단일 프레임 정보를 담는 구조체
 */
struct SFrameData
{
    ID3D11Texture2D* pTexture = nullptr;    ///< 캡처된 VRAM 텍스처 포인터 (소유권 없음)
    int64_t Timestamp = 0;                  ///< 프레임 획득 시각 (마이크로초)
    int32_t Width = 0;                      ///< 텍스처 너비 (픽셀)
    int32_t Height = 0;                     ///< 텍스처 높이 (픽셀)
};

/**
 * @struct   SMonitorDesc
 * @brief    열거된 모니터 정보를 담는 구조체
 */
struct SMonitorDesc
{
    int32_t Index = 0;                      ///< DXGI 어댑터 기준 모니터 인덱스
    std::wstring Name;                      ///< 모니터 장치 이름 (예: L"\\\\.\\DISPLAY1")
    int32_t Width = 0;                      ///< 모니터 해상도 너비 (픽셀)
    int32_t Height = 0;                     ///< 모니터 해상도 높이 (픽셀)
    bool IsPrimary = false;                 ///< 주 모니터 여부
};

/**
 * @struct   SWindowDesc
 * @brief    캡처 가능한 창 정보를 담는 구조체
 */
struct SWindowDesc
{
    HWND hWnd = nullptr;                    ///< 창 핸들
    std::wstring Title;                     ///< 창 제목 문자열
};

/**
 * @struct   SEncodeConfig
 * @brief    FFmpeg 인코더 및 출력 파일 설정 구조체
 */
struct SEncodeConfig
{
    std::wstring OutputPath;                ///< 출력 MP4 파일 경로
    int32_t Width = 1920;                   ///< 출력 영상 너비 (픽셀)
    int32_t Height = 1080;                  ///< 출력 영상 높이 (픽셀)
    int32_t FrameRate = 30;                 ///< 목표 프레임 레이트 (fps)
    int32_t BitRate = 8000000;              ///< 목표 비트레이트 (bps, 기본 8 Mbps)

    // ---- 분할 녹화 설정 ----
    bool bSplitEnabled = false;             ///< 분할 녹화 활성화 여부
    int32_t nSplitMinutes = 60;             ///< 분할 간격 (분, 유효 범위 1 ~ 1440)
};

/**
 * @enum     eCaptureType
 * @brief    캡처 소스 유형
 */
enum class eCaptureType
{
    Monitor,                                ///< 모니터 전체 캡처 (DXGI Desktop Duplication)
    Window,                                 ///< 특정 창 캡처 (Windows Graphics Capture API)
};

/**
 * @enum     eEncoderType
 * @brief    H.264 인코더 유형
 */
enum class eEncoderType
{
    NVENC,                                  ///< NVIDIA GPU 인코더 (h264_nvenc)
    AMF,                                    ///< AMD GPU 인코더 (h264_amf)
    QSV,                                    ///< Intel Quick Sync 인코더 (h264_qsv)
    MediaFoundation,                        ///< Windows MF 소프트웨어 인코더 (h264_mf, 폴백)
};

/**
 * @enum     eRecordState
 * @brief    녹화 상태 머신 상태값
 */
enum class eRecordState
{
    Idle,                                   ///< 녹화 대기 (미시작)
    Recording,                              ///< 녹화 진행 중
    Paused,                                 ///< 녹화 일시 정지
    Stopping,                               ///< 녹화 종료 처리 중
};

/**
 * @file         CNetworkCapture.h
 * @author       eskeptor (yc.jeon)
 * @date         2026-08-08
 * @version      1.0.0
 */

#pragma once

#include <winsock2.h>
#include <Windows.h>
#include <d3d11.h>
#include <cstdint>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <vector>

#include "ICapture.h"
#include "Common/Types.h"

class CNetworkCapture final : public ICapture
{
public:
    CNetworkCapture(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
    ~CNetworkCapture() override;

    bool Initialize() override;
    bool StartCapture() override;
    void StopCapture() override;
    bool AcquireFrame(SFrameData* pOutFrame) override;
    void ReleaseFrame() override;
    bool IsCapturing() const override;

    /**
     * @brief       원격 캡처 대상 초기화
     * @param[in]   stTarget 원격 캡처 대상 정보 (IP, 포트, 패스워드)
     */
    bool InitializeRemote(const SRemoteTarget& stTarget);

    // 핸드셰이크 후 확인된 원격 해상도/FPS
    int32_t GetRemoteWidth() const
    {
        return m_nRemoteWidth;
    }
    int32_t GetRemoteHeight() const
    {
        return m_nRemoteHeight;
    }
    int32_t GetRemoteFps() const
    {
        return m_nRemoteFps;
    }

    /**
     * @brief       연결 상태 문자열을 반환한다.
     * @return      연결 상태 문자열
     */
    std::string GetStatusString() const
    {
        std::lock_guard<std::mutex> lk(m_mtxStatus);
        return m_strStatus;
    }

private:
    void ThrReceive();

    // SHA256(password_bytes + nonce_16bytes) → hash_32bytes
    static bool ComputeAuthHash(const char* pszPassword, const uint8_t pNonce[16], uint8_t pOutHash[32]);
    static bool SendAll(SOCKET hSock, const uint8_t* pData, int32_t nLen);
    static bool RecvAll(SOCKET hSock, uint8_t* pData, int32_t nLen);

    bool ConnectAndHandshake();
    void SetStatus(const char* pszMsg);

    bool CreateGpuTexture(int32_t nWidth, int32_t nHeight);
    bool UploadBGRAToTexture(const uint8_t* pBGRA, int32_t nWidth, int32_t nHeight);

private:
    ID3D11Device* m_pDevice;
    ID3D11DeviceContext* m_pDeviceContext;

    SRemoteTarget m_stTarget;

    SOCKET m_hSocket;

    std::thread m_thrReceive;
    std::atomic<bool> m_bCapturing;
    std::atomic<bool> m_bConnected;

    // 디코딩된 프레임 큐 (최신 1장만 유지)
    mutable std::mutex m_mtxFrame;
    ID3D11Texture2D* m_pGpuTex;
    ID3D11Texture2D* m_pStagingTex;
    bool m_bFrameReady;
    int32_t m_nGpuTexWidth;
    int32_t m_nGpuTexHeight;
 
    // 원격 정보
    int32_t m_nRemoteWidth;
    int32_t m_nRemoteHeight;
    int32_t m_nRemoteFps;

    mutable std::mutex m_mtxStatus;
    std::string m_strStatus;
};

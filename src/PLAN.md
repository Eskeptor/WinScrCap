# Plan: WinScrCap - C++ Windows Screen Recorder

## TL;DR
DirectX 11 기반 ImGui UI + DXGI Desktop Duplication(모니터) + Windows Graphics Capture API(윈도우) + FFmpeg HW 인코딩(NVENC/AMF/QSV → libx264 폴백) 구조의 화면 녹화 프로그램. GPU 제로카피 파이프라인으로 시스템 리소스 최소화.

---

## 기술 스택 확정
- **언어**: C++20, MSVC
- **UI**: ImGui + DirectX 11 backend
- **화면 캡처**: DXGI Desktop Duplication (모니터) + WGC API (특정 윈도우)
- **인코딩**: FFmpeg LGPL 빌드 (h264_nvenc / h264_amf / h264_qsv → h264_mf 폴백)
- **출력**: MP4 (H.264)
- **최소 Windows**: Windows 10 (1803+)

---

## 아키텍처

### GPU 제로카피 파이프라인
```
DXGI Duplication → ID3D11Texture2D (VRAM) → FFmpeg D3D11VA Context → NVENC (GPU 인코딩)
                                                          ↓ 폴백
                                             Staging Buffer → CPU → h264_mf (Windows MF)
```
- CPU 복사 없이 GPU 메모리에서 직접 인코딩
- 폴백: GPU Texture → Staging Buffer → CPU → `h264_mf` (Windows Media Foundation 내장 H.264 인코더)

### 스레딩 모델
- **메인 스레드**: DX11 렌더링 + ImGui UI
- **캡처 스레드**: DXGI/WGC 프레임 획득 (높은 우선순위)
- **인코딩 스레드**: FFmpeg 인코딩 + MP4 먹싱

---

## 프로젝트 구조
```
WinScrCap/
├── WinScrCap.sln
└── WinScrCap/
    ├── WinScrCap.vcxproj
    ├── src/
    │   ├── main.cpp
    │   ├── Application/
    │   │   ├── CApplication.h/.cpp    ← DX11 장치 + 메인 루프
    │   ├── Capture/
    │   │   ├── ICapture.h             ← 순수 가상 인터페이스
    │   │   ├── CScreenCapture.h/.cpp  ← DXGI Desktop Duplication
    │   │   ├── CWindowCapture.h/.cpp  ← WGC API
    │   │   ├── CCaptureManager.h/.cpp ← 캡처 조율 + 프레임 큐
    │   ├── Encoder/
    │   │   ├── CVideoEncoder.h/.cpp   ← FFmpeg HW/SW 인코더
    │   ├── UI/
    │   │   ├── CMainWindow.h/.cpp     ← ImGui 메인 창
    │   └── Common/
    │       └── Types.h               ← SFrameData, eXxx 정의
    ├── third_party/
    │   ├── imgui/                    ← ImGui 소스 직접 포함
    │   └── ffmpeg/                   ← 사전 빌드 바이너리 (include/lib/bin)
    └── res/
        └── (아이콘 등)
```

---

## 핵심 클래스 설계 (C++ 코딩 지침 준수)

### Common/Types.h
```
struct SFrameData      ← ID3D11Texture2D 포인터, 타임스탬프, 너비/높이
enum class eCaptureType  ← Monitor, Window
enum class eEncoderType  ← NVENC, AMF, QSV, Software
enum class eRecordState  ← Idle, Recording, Paused, Stopping
```

### ICapture (인터페이스)
```
pure virtual: Initialize(), StartCapture(), StopCapture(), AcquireFrame(), ReleaseFrame()
```

### CScreenCapture : ICapture
- `IDXGIOutputDuplication` 래핑
- 멀티 모니터 지원 (`IDXGIFactory → IDXGIAdapter → IDXGIOutput` 열거)
- `AcquireNextFrame()` → `SFrameData` 반환

### CWindowCapture : ICapture
- Windows.Graphics.Capture (WinRT COM)
- `GraphicsCaptureItem` 생성 (HWND 기반)
- `Direct3D11CaptureFramePool` 사용

### CCaptureManager
- `ICapture*` 인터페이스를 통해 소스 전환 (모니터 ↔ 창)
- 캡처 스레드 관리 (`std::thread`)
- 프레임 큐 (`std::queue` + `std::mutex`) 또는 lock-free 링 버퍼
- FPS 제어 (`std::chrono` 기반 타이밍)

### CVideoEncoder
- `AVCodecContext` 래핑 (FFmpeg)
- `InitHardwareEncoder()`: nvenc → amf → qsv → h264_mf 순서로 시도
- D3D11VA 컨텍스트 설정 (`av_hwdevice_ctx_create`)
- `EncodeFrame(SFrameData&)`: `AV_PIX_FMT_D3D11` → 인코딩
- MP4 먹서 (`AVFormatContext`, `avformat_write_header` 등)

### CApplication
- DX11 `Device` / `DeviceContext` / `SwapChain` 소유
- ImGui 초기화 및 렌더 루프
- `CCaptureManager`, `CVideoEncoder`, `CMainWindow` 소유
- Win32 메시지 루프

### CMainWindow
- ImGui 레이아웃:
  - 소스 선택 패널 (모니터 목록 / 창 목록)
  - 인코더 설정 (FPS, 품질, 파일 경로)
  - 미리보기 패널 (`ImGui::Image` + `ID3D11ShaderResourceView`)
  - 녹화 제어 버튼 + 경과 시간

---

## Phase별 구현 계획

### Phase 1: 프로젝트 설정
1. VS 솔루션(`.sln`) + 프로젝트(`.vcxproj`) 생성
2. C++20 표준 설정, 경고 레벨 W4, SDL 체크
3. ImGui 소스 파일 `third_party/imgui/` 에 포함, 빌드 설정
4. FFmpeg 사전 빌드 바이너리(BtbN) 다운로드 → `third_party/ffmpeg/`
   - **반드시 `lgpl` 태그 빌드 사용** (GPL 코덱 미포함, 상용/클로즈드소스 배포 가능)
   - `include/`, `lib/`, `bin/` (dll) 구조
5. `.vcxproj`에 include/lib 경로, 링커 설정 추가
6. 기본 `WinMain` + 빈 창 + DX11 초기화 확인

### Phase 2: DX11 + ImGui 기반 구축
1. `CApplication`: DX11 Device, DeviceContext, SwapChain, RTV 초기화
2. Win32 창 생성, WndProc 등록
3. ImGui 초기화 (`imgui_impl_win32`, `imgui_impl_dx11`)
4. 메인 루프: 메시지 처리 → ImGui 프레임 → 렌더 → Present
5. `CMainWindow` 스켈레톤 UI (빈 레이아웃만)

### Phase 3: 화면 캡처 구현
1. `Types.h`에 `SFrameData`, 관련 enum 정의
2. `ICapture` 인터페이스 정의
3. `CScreenCapture`: `IDXGIOutputDuplication`
   - 모니터 열거 (`IDXGIFactory → IDXGIAdapter → IDXGIOutput`)
   - `AcquireNextFrame()` → `ID3D11Texture2D` 반환
   - 프레임 해제 `ReleaseFrame()`
4. `CWindowCapture`: Windows.Graphics.Capture
   - HWND → `CreateForWindow()`
   - `D3D11CaptureFramePool` 설정
5. `CCaptureManager`: 스레드 + 프레임 큐 + FPS 조율

### Phase 4: ImGui 미리보기 연동
1. 캡처된 `ID3D11Texture2D`로 SRV 생성
2. `CMainWindow`에 `ImGui::Image()`로 실시간 프리뷰 표시
3. 소스 전환 시 SRV 재생성

### Phase 5: FFmpeg 인코더 구현
1. `CVideoEncoder`: `AVFormatContext` + `AVCodecContext` 초기화
2. `InitHardwareEncoder()`: nvenc → amf → qsv → h264_mf 순서
3. D3D11VA 하드웨어 컨텍스트 (`av_hwdevice_ctx_create` + D3D11VA)
4. `EncodeFrame()`: D3D11 텍스처 → `AVFrame` (`AV_PIX_FMT_D3D11`) → 인코딩
5. MP4 컨테이너 (`avformat_write_header`, `av_write_trailer`)
6. 인코딩 스레드 분리

### Phase 6: UI 완성 + 상태 머신
1. `eRecordState` 상태 머신 (Idle / Recording / Paused / Stopping)
2. 녹화 시작/중지/일시정지 버튼 연동
3. 출력 파일 경로 입력 (`GetSaveFileName` 파일 대화상자)
4. 인코더 선택 UI (감지된 HW 인코더 자동 표시)
5. 경과 시간, 파일 크기 실시간 표시
6. 설정값 (FPS, 품질 - CRF / 비트레이트)

### Phase 7: 안정화 및 마무리
1. 모든 COM/FFmpeg 리소스 안전 해제 확인 (RAII)
2. 오류 발생 시 ImGui 팝업 표시
3. 컴파일러 경고 W4 전부 해결
4. 멀티모니터 전환, DPI 변화 처리
5. DX11 디버그 레이어로 리소스 누수 점검

### Phase 추가: 분할 녹화 (Split Recording)
녹화 중 일정 시간 간격으로 자동으로 파일을 나누는 기능.

**동작 방식:**
- `SEncodeConfig::bSplitEnabled` + `nSplitMinutes` 설정
- 인코딩 프레임 수 기준 (`FPS × 분 × 60`)으로 분할 → 일시정지 중 자동 제외
- 분할 시 현재 파일 종료(`av_write_trailer`) → 새 타임스탬프 파일 자동 생성

**파일명 규칙:**
```
입력: ABC.mp4          설정: 1시간
출력: ABC_20260722_2000.mp4  (녹화 시작 시각)
      ABC_20260722_2100.mp4  (1시간 후 분할)
      ABC_20260722_2200.mp4  (2시간 후 분할)  ...
```

**구현 위치:**
- `SEncodeConfig` — `bSplitEnabled`, `nSplitMinutes` 추가
- `CCaptureManager::PerformSplit()` — 파일 교체 로직
- `CCaptureManager::GenerateSplitPath()` — 타임스탬프 파일명 생성 (`YYYYMMDD_HHMM`)
- `CMainWindow` — Split 체크박스 + 분 입력 UI, 분할 중 현재 파일명 표시

---

## 의존성 설정 상세

### ImGui
- https://github.com/ocornut/imgui 에서 다운로드
- 포함 파일: `imgui.h/cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`
- 백엔드: `imgui_impl_win32.h/cpp`, `imgui_impl_dx11.h/cpp`

### FFmpeg (사전 빌드 바이너리)
- https://github.com/BtbN/FFmpeg-Builds/releases
- **`ffmpeg-master-latest-win64-lgpl-shared.zip` 빌드 선택** (GPL 코덱 미포함)
  - `lgpl` 빌드: libx264/libx265 등 GPL 코덱 미포함 → 클로즈드소스 배포 허용
  - `gpl` 빌드: libx264 포함 → **사용 금지**
- 필요 lib: `avcodec.lib`, `avformat.lib`, `avutil.lib`, `swscale.lib`, `swresample.lib`
- 필요 DLL: `avcodec-*.dll`, `avformat-*.dll`, `avutil-*.dll` 등

### DirectX 11 / WGC API
- Windows SDK 포함 (별도 설치 불필요)
- WGC API 사용 시 `windowsapp.lib` 링크 필요
- WinRT 헤더 (`#include <winrt/...>`) + `/await` 컴파일 플래그 설정

### h264_mf (소프트웨어 폴백)
- FFmpeg의 `h264_mf` 인코더 = Windows Media Foundation H.264 인코더 래핑
- Windows 내장 (`mfplat.dll`, `mfreadwrite.dll`) → 별도 설치/배포 불필요
- Microsoft가 MPEG LA H.264 특허료 납부 → 사용자 부담 없음
- 라이선스: Windows SDK 약관 (GPL 없음)

---

## 검증 체크리스트
1. [x] 솔루션 빌드 성공 (W4 경고 0개)
2. [x] ImGui 창 정상 표시 (DX11 렌더링 확인)
3. [x] 모니터 목록 정상 열거
4. [x] 미리보기 패널에서 실시간 화면 표시 (≥ 15fps)
5. [x] 모니터 캡처 → MP4 파일 정상 생성 + 재생 가능
6. [x] 특정 윈도우 캡처 → MP4 파일 정상 생성
7. [x] HW 인코더 감지 및 SW 폴백 동작 확인
8. [x] CPU 사용률 ≤ 15% (HW 인코딩 시 @1080p 30fps)
9. [x] 녹화 중 일시정지/재개 동작
10. [x] 프로그램 종료 시 리소스 누수 없음 (DX11 디버그 레이어 확인)

검증 체크리스트 항목 완료시 [] 사이에 체크([x])하여 진행할 것.

---

## 라이선스 요약

| 구성 요소 | 라이선스 | 비고 |
|---|---|---|
| FFmpeg (LGPL 빌드) | LGPL 2.1+ | DLL 동적 링킹 시 클로즈드소스 허용 |
| h264_nvenc | LGPL FFmpeg + NVIDIA 독점 SDK | GPL 없음 |
| h264_amf | LGPL FFmpeg + MIT (AMD AMF) | GPL 없음 |
| h264_qsv | LGPL FFmpeg + Intel 독점 | GPL 없음 |
| h264_mf | LGPL FFmpeg + Windows SDK 약관 | GPL 없음, MS가 특허료 납부 |
| ImGui | MIT | 제약 없음 |
| DirectX 11 / WGC | Windows SDK 약관 | 제약 없음 |
| ~~libx264~~ | ~~GPL v2~~ | **사용 금지** |

> **주의**: BtbN 빌드 다운로드 시 반드시 `lgpl` 태그 파일을 선택할 것. `gpl` 빌드 사용 시 libx264가 포함되어 배포물 전체가 GPL 오염됨.

---

## 범위 외 (Out of Scope)
- 오디오 캡처/녹음 (추후 추가 가능)
- 화면 자르기(크롭) 영역 선택 UI
- 실시간 스트리밍 (RTMP)
- H.265/HEVC 인코딩 (추후 확장 가능)

---

## 원격 캡처 기능 (Remote Capture — 방안 A: 에이전트 직접 통신)

### 개요
원격 PC에 경량 에이전트(`WinScrCapAgent.exe`)를 설치하고, 기존 WinScrCap에서 네트워크로 직접 연결해 원격 화면을 녹화한다.  
기존 `ICapture` 인터페이스에 `CNetworkCapture`를 추가하는 방식으로 확장 — 기존 로컬 캡처 코드는 무변경.

---

### 전체 시스템 구성도
```
[원격 PC]  WinScrCapAgent.exe
              DXGI Duplication
                    │ ID3D11Texture2D → NV12 변환 (CPU Staging)
                    │ FFmpeg h264_mf / HW 인코딩 (저지연, 고품질 CRF)
                    │ H.264 NAL 유닛
                    ▼
              CAgentServer (TCP Listen, Winsock2)
                    │ 커스텀 바이너리 프로토콜
                    ▼ (LAN / VPN)
[이 PC]    WinScrCap.exe
              CNetworkCapture : ICapture
                    │ TCP 수신 → FFmpeg H.264 디코딩
                    │ AVFrame(NV12) → ID3D11Texture2D CPU 업로드
                    ▼
              CCaptureManager → CVideoEncoder → MP4 저장
```

> **GPU zero-copy 불가**: 원격 프레임은 네트워크를 거치므로 수신 측에서 CPU→D3D11 업로드가 불가피.  
> 이중 인코딩(에이전트 인코딩 → 전송 → 수신 측 디코딩 → 재인코딩)으로 화질 손실 최소화를 위해 에이전트 측은 고품질(CRF ≤ 18) 설정.

---

### 커스텀 TCP 프로토콜

#### 핸드셰이크
```
Client → Server: [4B 매직 "WSCR"] [2B 버전] [32B 인증 토큰(PBKDF2-HMAC-SHA256)]
Server → Client: [1B 결과 코드] [4B 너비] [4B 높이] [4B FPS]
```

#### 프레임 패킷 (핸드셰이크 완료 후 반복)
```
[4B 매직 "FRAM"] [4B 페이로드 크기] [8B 타임스탬프(μs)] [페이로드: H.264 NAL 유닛]
```

#### 제어 패킷
```
[4B 매직 "CTRL"] [1B 타입: 0x01=Ping, 0x02=Pong, 0x03=Stop]
```

- **전송 계층**: TCP (Winsock2, IPv4/IPv6)  
- **보안**: PBKDF2-HMAC-SHA256 인증 토큰 (Windows CNG API — `BCryptDeriveKeyPBKDF2`)  
- **TLS 없음**: LAN / VPN 전용으로 명시, WAN 사용 시 VPN 필수  
- **연결 제한**: 에이전트는 동시에 1개 클라이언트만 허용

---

### WinScrCapAgent 설계 (신규 프로젝트)

**프로젝트 타입**: Win32 앱 (ImGui + DX11 UI, 트레이 아이콘 병행)  
**솔루션**: 기존 `WinScrCap.sln`에 새 프로젝트로 추가

#### 프로젝트 구조
```
WinScrCapAgent/
├── WinScrCapAgent.vcxproj
└── src/
    ├── main.cpp                       ← WinMain, 트레이 아이콘 + 메시지 루프
    ├── Agent/
    │   ├── CAgentServer.h/.cpp        ← TCP 서버 (Winsock2 Listen/Accept)
    │   ├── CAgentCapture.h/.cpp       ← DXGI 캡처 (CScreenCapture 로직 재활용)
    │   └── CAgentEncoder.h/.cpp       ← FFmpeg 경량 인코더 (H.264 고품질)
    ├── UI/
    │   └── CAgentWindow.h/.cpp        ← ImGui 메인 창 (메뉴바 + 상태 표시 + 설정 다이얼로그)
    └── Common/
        └── AgentConfig.h              ← SAgentConfig (포트, 비밀번호 해시, FPS 등)
```

#### UI 설계 (`CAgentWindow`)

**창 동작**
- 창 닫기(X) 버튼 → 창 숨김 + 트레이 아이콘 유지 (종료 아님)
- 트레이 아이콘 우클릭 메뉴: **열기 / 설정 / 종료** 3가지
- 트레이 아이콘 더블클릭 → 창 복원

**메인 화면 레이아웃**
```
┌─────────────────────────────────────┐
│ [메뉴] 설정(S)   종료(X)            │  ← ImGui BeginMainMenuBar
├─────────────────────────────────────┤
│  WinScrCap Agent  v1.0              │
│                                     │
│  ● 포트        : 9201               │
│  ● FPS         : 30                 │
│  ● 품질 (CRF)  : 18                 │
│  ● 모니터      : 0번                │
│  ● 연결 상태   : 대기 중 / 연결됨   │
└─────────────────────────────────────┘
```
- 현재 INI에서 읽어온 설정값을 읽기 전용으로 표시
- 연결 상태는 실시간 갱신 (클라이언트 없음 / 연결됨 + 클라이언트 IP 표시)

**설정 다이얼로그** (`ImGui::OpenPopup` 모달)
- 메뉴 > 설정 또는 트레이 > 설정 클릭 시 오픈
- 편집 가능한 항목: 포트, 비밀번호(평문 입력 → 저장 시 PBKDF2 해시 변환), FPS, CRF, 모니터 인덱스
- **저장** 버튼: INI 파일에 기록 + 설정 즉시 반영 (서버 재기동 필요 항목은 안내)
- **취소** 버튼: 변경 내용 폐기

#### 핵심 클래스

**`SAgentConfig`**
```
uint16_t nPort          ← 기본값 9201
char     szPasswordHash[65]  ← PBKDF2-SHA256 hex (INI 저장)
int      nFps           ← 전송 FPS (기본 30)
int      nCrf           ← 인코딩 품질 (기본 18, 낮을수록 고품질)
int      nMonitorIndex  ← 캡처할 모니터 인덱스
```

**`CAgentCapture`**
- `CScreenCapture`와 동일한 DXGI 로직 (코드 공유 또는 복사)
- `AcquireFrame()` → Staging Buffer → CPU → NV12 변환 → `CAgentEncoder`에 전달
- 별도 캡처 스레드 (높은 우선순위)

**`CAgentEncoder`**
- FFmpeg `h264_mf` or HW 인코더 (nvenc → amf → qsv → h264_mf)
- `EncodeFrame(AVFrame*)` → H.264 NAL 유닛 → `CAgentServer`의 송신 큐에 push
- 인코딩 스레드 분리

**`CAgentServer`**
- `WSAStartup` → `socket` → `bind(port)` → `listen` → `accept`
- 클라이언트 연결 시 핸드셰이크 처리 및 인증 검증
- 송신 스레드: 큐에서 프레임 패킷 꺼내 `send()`
- Ping/Pong 워치독 (10초 무응답 시 연결 종료)

#### 설정 파일
```ini
; WinScrCapAgent.ini (실행 파일과 같은 디렉터리)
[Agent]
Port=9201
PasswordHash=<PBKDF2-SHA256-hex>
FPS=30
CRF=18
MonitorIndex=0
```

---

### WinScrCap 변경 사항

#### Types.h 추가
```cpp
// eCaptureType에 Remote 추가
enum class eCaptureType { Monitor, Window, Remote };

struct SRemoteTarget {
    char  szHost[256];   // IP 또는 호스트명
    uint16_t nPort;      // 기본 9201
    char  szPassword[128];
};
```

#### 신규 클래스: `CNetworkCapture : ICapture`
```
Capture/
├── CNetworkCapture.h/.cpp   ← TCP 클라이언트 + H.264 디코딩
```

- `Initialize(SRemoteTarget&)`: Winsock2 connect + 핸드셰이크 + FFmpeg 디코더 초기화
- `AcquireFrame()`: 수신 큐에서 `AVFrame` 꺼내기 → NV12 → `ID3D11Texture2D` CPU 업로드 → `SFrameData` 반환
- `ReleaseFrame()`: 텍스처 반환
- 수신 스레드: TCP 패킷 파싱 → FFmpeg 디코딩 → 프레임 큐 push
- 재연결 로직: 연결 끊길 경우 3초 간격 자동 재시도

#### UI 변경 (`CMainWindow`)

**Capture Type 라디오 버튼에 Remote 항목 추가**
```
Capture Type:  ○ Monitor   ○ Window   ○ Remote
```
- 기존 Monitor / Window 선택 시 동작 무변경
- **Remote 선택 시** 아래 Agent 연결 설정 영역이 활성화됨

**Remote 선택 시 활성화되는 연결 설정 UI**
```
┌─ Agent Connection ──────────────────────────┐
│  Host     [ 192.168.0.10        ]           │
│  Port     [ 9201 ]                          │
│  Password [ ************        ]           │
│                                             │
│  [  연결  ]   상태: ● 대기 중               │
│  (연결 후) 해상도: 1920×1080  FPS: 30       │
└─────────────────────────────────────────────┘
```
- 연결 상태: **대기 중 / 연결 중... / 연결됨 (초록) / 오류 (빨강 + 오류 메시지)**
- 연결됨 상태에서만 녹화 시작 버튼 활성화
- Remote 선택 시 미리보기 패널 하단에 `"원격 캡처: GPU zero-copy 비활성화"` 안내 문구 표시

---

### Phase별 구현 계획 (원격 캡처)

#### Phase R1: WinScrCapAgent 기반 구축
1. `WinScrCap.sln`에 `WinScrCapAgent` 프로젝트 추가 (Win32 앱, x64)
2. FFmpeg/ImGui third_party 공유 참조 설정
3. `AgentConfig.h` + INI 파일 파서 (`GetPrivateProfileString` 기반)
4. `CAgentCapture`: DXGI Duplication + Staging Buffer + NV12 변환
5. `CAgentEncoder`: FFmpeg h264_mf/HW 초기화, NAL 유닛 출력
6. `CAgentServer`: Winsock2 TCP 서버, 핸드셰이크, 송신 큐
7. 트레이 아이콘 + 우클릭 메뉴 (시작/중지/종료)
8. **로컬 테스트**: 127.0.0.1 연결로 WinScrCap ↔ Agent 동작 확인

#### Phase R2: WinScrCap 클라이언트 측 구현
1. `eCaptureType::Remote` + `SRemoteTarget` 추가 (`Types.h`)
2. `CNetworkCapture` 구현
   - Winsock2 TCP connect + 핸드셰이크
   - 수신 스레드 + FFmpeg H.264 디코더 초기화
   - `AVFrame` → `ID3D11Texture2D` CPU 업로드
3. `CCaptureManager`에 Remote 소스 분기 추가

#### Phase R3: UI 연동
1. `CMainWindow` Remote 탭 (호스트/포트/비밀번호 입력)
2. 연결 상태 표시 + 재연결 처리
3. 원격 소스에서 캡처 → 미리보기 → 녹화 → MP4 저장 전체 흐름 검증

#### Phase R4: 안정화
1. 네트워크 단절 시 자동 재연결 및 녹화 재개 처리
2. Ping/Pong 워치독 + 타임아웃 처리
3. 에이전트 측 리소스 누수 점검 (DX11 디버그 레이어)
4. 패킷 헤더 매직 검증으로 스트림 오염 방어

---

### 원격 캡처 검증 체크리스트
11. [x] WinScrCapAgent 빌드 성공 (W4 경고 0개)
12. [x] 에이전트 ↔ WinScrCap 127.0.0.1 연결 및 핸드셰이크 성공
13. [x] 원격 화면 미리보기 정상 표시 (≥ 15fps, 같은 LAN)
14. [x] 원격 소스 → MP4 파일 정상 생성 + 재생 가능
15. [x] 잘못된 비밀번호 입력 시 연결 거부 동작
16. [x] 네트워크 단절 후 자동 재연결 동작
17. [x] 에이전트 종료 시 WinScrCap 측 오류 팝업 표시
18. [ ] CPU 사용률 (에이전트 측 h264_mf 인코딩 @1080p 30fps) ≤ 30%

---

### 원격 캡처 의존성 추가

| 항목 | 내용 |
|---|---|
| 네트워크 | Winsock2 (`ws2_32.lib`) — Windows SDK 포함 |
| 인증 해시 | Windows CNG (`bcrypt.lib`) — Windows SDK 포함, PBKDF2-HMAC-SHA256 |
| 에이전트 인코딩 | 기존 FFmpeg LGPL 빌드 재사용 |
| 설정 파일 | `GetPrivateProfileString` (kernel32) — 별도 라이브러리 불필요 |

> 추가 서드파티 라이브러리 없음 — 기존 의존성만으로 구현 가능.

---

## 결정 사항 요약
| 항목 | 결정 |
|---|---|
| ImGui Backend | DirectX 11 |
| 화면 캡처 (모니터) | DXGI Desktop Duplication |
| 화면 캡처 (창) | Windows Graphics Capture API (WGC) |
| 인코더 | FFmpeg LGPL HW: NVENC → AMF → QSV → h264_mf(Windows MF) 폴백 |
| 출력 포맷 | MP4 (H.264) |
| 최소 OS | Windows 10 1803+ |

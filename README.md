# WinScrCap

A GPU-accelerated Windows screen recorder built with C++20, DirectX 11, and FFmpeg.  
Captures monitors or individual windows and encodes to MP4 (H.264) with hardware acceleration.

> **Built with AI assistance** — developed using [Visual Studio Code](https://code.visualstudio.com/) + [GitHub Copilot](https://github.com/features/copilot) + MSVC (Visual Studio 2026).

---

## Features

- **Dual capture sources** — full monitor via DXGI Desktop Duplication, or specific window via Windows Graphics Capture API
- **GPU zero-copy pipeline** — captured `ID3D11Texture2D` frames go directly to the hardware encoder without touching the CPU
- **Hardware encoding with automatic fallback**
  - NVENC (NVIDIA) → AMF (AMD) → QuickSync (Intel) → Windows Media Foundation H.264 (software fallback)
- **Real-time preview** — live ImGui preview panel via `ID3D11ShaderResourceView`
- **Split recording** — automatically splits output files at a configured interval (e.g. every hour), named by timestamp (`ABC_20260722_2000.mp4`, `ABC_20260722_2100.mp4`, …)
- **Configurable encoding** — FPS, CRF / bitrate, output path, encoder selection
- **Minimal resource usage** — separate threads for capture, encoding, and UI

---

## Tech Stack

| Layer | Technology |
|---|---|
| Language | C++20, MSVC |
| UI | [Dear ImGui](https://github.com/ocornut/imgui) + DirectX 11 backend |
| Monitor capture | DXGI Desktop Duplication (`IDXGIOutputDuplication`) |
| Window capture | Windows Graphics Capture API (WinRT COM) |
| Encoding / Muxing | [FFmpeg](https://ffmpeg.org/) LGPL build (H.264, MP4) |
| Hardware encoding | NVENC / AMF / QuickSync / h264_mf fallback |
| Minimum Windows | Windows 10 1803+ |

---

## Architecture

### GPU Zero-Copy Pipeline

```
DXGI Duplication ──► ID3D11Texture2D (VRAM) ──► D3D11VA Context ──► NVENC / AMF / QSV
                                                                          │
                                                              (fallback)  ▼
                                                         Staging Buffer ──► CPU ──► h264_mf
```

### Threading Model

| Thread | Responsibility |
|---|---|
| Main thread | DX11 rendering + ImGui UI |
| Capture thread | Frame acquisition (high priority) |
| Encode thread | FFmpeg encoding + MP4 muxing |

### Project Structure

```
WinScrCap/
├── WinScrCap.sln
└── WinScrCap/
    ├── src/
    │   ├── main.cpp
    │   ├── Application/
    │   │   └── CApplication.h/.cpp      ← DX11 device + main loop
    │   ├── Capture/
    │   │   ├── ICapture.h               ← Pure virtual interface
    │   │   ├── CScreenCapture.h/.cpp    ← DXGI Desktop Duplication
    │   │   ├── CWindowCapture.h/.cpp    ← Windows Graphics Capture API
    │   │   └── CCaptureManager.h/.cpp   ← Thread + frame queue + FPS control
    │   ├── Encoder/
    │   │   └── CVideoEncoder.h/.cpp     ← FFmpeg HW/SW encoder + MP4 muxer
    │   ├── UI/
    │   │   └── CMainWindow.h/.cpp       ← ImGui layout
    │   └── Common/
    │       └── Types.h                  ← SFrameData, enums
    └── third_party/
        ├── imgui/                        ← ImGui source (included directly)
        └── ffmpeg/                       ← Pre-built LGPL binaries
```

---

## Building

### Prerequisites

- Visual Studio 2026 (MSVC v145, C++20)
- Windows SDK 10.0.19041.0+
- FFmpeg LGPL pre-built binaries already included under `third_party/ffmpeg/`

### Steps

1. Clone the repository
   ```
   git clone https://github.com/your-username/WinScrCap.git
   ```
2. Open `src/WinScrCap.sln` in Visual Studio 2026
3. Select the **x64 | Debug** or **x64 | Release** configuration
4. Build (`Ctrl+Shift+B`)
5. Copy the FFmpeg DLLs from `third_party/ffmpeg/bin/` next to the built executable (or add the path to `PATH`)

---

## Usage

1. Launch `WinScrCap.exe`
2. Select a capture source — **Monitor** (from the detected list) or **Window** (by HWND)
3. Configure encoding settings: FPS, quality (CRF / bitrate), output file path
4. Optionally enable **Split Recording** and set the interval (minutes)
5. Click **Record** — a live preview appears while recording
6. Click **Stop** to finalize and write the MP4 file

---

## License

This project is licensed under the [MIT License](LICENSE).

FFmpeg is used under the **LGPL v2.1** license. Only LGPL-compatible components are linked; no GPL codecs are included. See [FFmpeg License](https://ffmpeg.org/legal.html) for details.

Dear ImGui is licensed under the MIT License.

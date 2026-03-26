// =========================================================
// BASTION RECORDER - DIMENSION X (V41 FINAL SEAMLESS)
// =========================================================
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <commdlg.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <AudioClient.h>
#include <gdiplus.h>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")

// --- BASTION CORE MACROS ---
#define _108 class       //
#define _126 public      //
#define _50  void        //
#define _15  if          //
#define _41  else        //
#define _42  else if     //
#define _114 while       //
#define _39  for         //
#define _96  return      //
#define _82  switch      //
#define _28  case        //
#define _37  break       //
#define _184 uint8_t     //
#define _89  uint32_t    //
#define _94  size_t      //
#define _43  int         //
#define _30  char        //
#define _87  std::string //
#define _128 true        //
#define _86  false       //
#define EQ   ==          //

using namespace Gdiplus;

HWND hStartBtn, hStopBtn, hStatus, hMainWnd;
_30 globalFilePath[260] = "capture_v39.mp4";

// =========================================================
// BASTION ENGINE - NULL-BYTE OPTIMIZER
// =========================================================
_108 BastionEngine {
_126:
    _50 PostProcessTBA(_87 inP) {
        std::ifstream in(inP, std::ios::binary | std::ios::ate);
        _15 (!in.is_open()) _96;
        _94 totalSize = in.tellg();
        
        _94 zC = 0; // Zero-Count
        _15 (totalSize > 0) {
            _94 sc = std::min((_94)65535, totalSize);
            in.seekg(-((std::streamoff)sc), std::ios::end);
            std::vector<_30> tl(sc); in.read(tl.data(), sc);
            int idx = sc - 1; _114 (idx >= 0 && tl[idx] EQ 0) { zC++; idx--; }
        }

        _94 effSz = totalSize - zC;
        in.seekg(0, std::ios::beg);
        
        _87 tmpP = inP + ".tmp";
        std::ofstream out(tmpP, std::ios::binary);
        
        _184 fl = 0; _15 (zC > 0) fl |= (1<<6); // Flags
        out.put((_30)fl);
        _15 (zC > 0) { 
            _15 (zC < 255) out.put((_30)zC); 
            _41 { out.put((_30)255); out.put((_30)((zC>>8)&0xFF)); out.put((_30)(zC&0xFF)); } 
        }

        std::vector<_30> buf(1024 * 1024); // 1MB Buffer
        _94 copied = 0;
        _114 (copied < effSz) {
            _94 chk = std::min((_94)1024 * 1024, effSz - copied);
            in.read(buf.data(), chk); out.write(buf.data(), chk);
            copied += chk;
        }
        
        in.close(); out.close();
        DeleteFileA(inP.c_str());             // Original weg
        MoveFileA(tmpP.c_str(), inP.c_str());  // .tmp -> .mp4
    }
};

// =========================================================
// BASTION RECORDER ENGINE
// =========================================================
_108 BastionRecorder {
_126:
    bool isRunning = _86;
    IMFSinkWriter* pWriter = NULL;
    _89 vidStream = 0;
    _89 audStream = 1;
    IAudioCaptureClient* pAudioCapture = NULL;
    IAudioClient* pAudioClient = NULL;

    bool SelectOutputFile(HWND hwnd) {
        OPENFILENAMEA ofn = {0};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = "Video Files (*.mp4)\0*.mp4\0";
        ofn.lpstrFile = globalFilePath;
        ofn.nMaxFile = 260;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        ofn.lpstrDefExt = "mp4";
        _96 GetSaveFileNameA(&ofn);
    }

    _50 RecordingLoop() {
        MFStartup(MF_VERSION);
        _43 w = GetSystemMetrics(SM_CXSCREEN);
        _43 h = GetSystemMetrics(SM_CYSCREEN);
        
        wchar_t wPath[260];
        MultiByteToWideChar(CP_ACP, 0, globalFilePath, -1, wPath, 260);
        HRESULT hr = MFCreateSinkWriterFromURL(wPath, NULL, NULL, &pWriter);
        _15 (FAILED(hr)) { isRunning = _86; _96; }

        IMFMediaType* pVidOut = NULL;
        MFCreateMediaType(&pVidOut);
        pVidOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pVidOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        pVidOut->SetUINT32(MF_MT_AVG_BITRATE, 2500000);
        MFSetAttributeSize(pVidOut, MF_MT_FRAME_SIZE, w, h);
        MFSetAttributeRatio(pVidOut, MF_MT_FRAME_RATE, 30, 1);
        pWriter->AddStream(pVidOut, (DWORD*)&vidStream);

        IMFMediaType* pVidIn = NULL;
        MFCreateMediaType(&pVidIn);
        pVidIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pVidIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        MFSetAttributeSize(pVidIn, MF_MT_FRAME_SIZE, w, h);
        pWriter->SetInputMediaType(vidStream, pVidIn, NULL);
        pVidOut->Release(); pVidIn->Release();

        // AUDIO
        IMMDeviceEnumerator* pEnum = NULL; IMMDevice* pDev = NULL;
        CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnum);
        pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDev);
        pDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
        WAVEFORMATEX* pWfx = NULL; pAudioClient->GetMixFormat(&pWfx);
        pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, pWfx, NULL);
        pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pAudioCapture);

        IMFMediaType* pAudOut = NULL; MFCreateMediaType(&pAudOut);
        pAudOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        pAudOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        pAudOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, pWfx->nSamplesPerSec);
        pAudOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, pWfx->nChannels);
        pWriter->AddStream(pAudOut, (DWORD*)&audStream);
        pAudOut->Release();

        pAudioClient->Start();
        pWriter->BeginWriting();
        auto startClock = std::chrono::high_resolution_clock::now();
        _89 frameIdx = 0;

        _114 (isRunning) {
            HDC hdcS = GetDC(NULL); HDC hdcM = CreateCompatibleDC(hdcS);
            HBITMAP hbm = CreateCompatibleBitmap(hdcS, w, h); SelectObject(hdcM, hbm);
            BitBlt(hdcM, 0, 0, w, h, hdcS, 0, 0, SRCCOPY);

            BITMAPINFO bmi = {0}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = w; bmi.bmiHeader.biHeight = -h; // Flip Fix
            bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;

            _89 bSz = w * h * 4; IMFSample* pSmp = NULL; IMFMediaBuffer* pBuf = NULL;
            MFCreateMemoryBuffer(bSz, &pBuf); BYTE* pD = NULL; pBuf->Lock(&pD, NULL, NULL);
            GetDIBits(hdcM, hbm, 0, h, pD, &bmi, DIB_RGB_COLORS);
            pBuf->Unlock(); pBuf->SetCurrentLength(bSz);
            MFCreateSample(&pSmp); pSmp->AddBuffer(pBuf);
            
            _94 ts = (frameIdx * 10000000) / 30;
            pSmp->SetSampleTime(ts); pSmp->SetSampleDuration(10000000 / 30);
            pWriter->WriteSample(vidStream, pSmp);
            pSmp->Release(); pBuf->Release();
            DeleteObject(hbm); DeleteDC(hdcM); ReleaseDC(NULL, hdcS);

            // AUDIO
            _89 pLen = 0; _184* pAudD = NULL; _89 flg = 0;
            pAudioCapture->GetNextPacketSize(&pLen);
            _114 (pLen > 0) {
                pAudioCapture->GetBuffer(&pAudD, &pLen, (DWORD*)&flg, NULL, NULL);
                IMFSample* pASmp = NULL; IMFMediaBuffer* pABuf = NULL;
                _89 bLen = pLen * pWfx->nBlockAlign;
                MFCreateMemoryBuffer(bLen, &pABuf); BYTE* pADest = NULL; pABuf->Lock(&pADest, NULL, NULL);
                memcpy(pADest, pAudD, bLen);
                pABuf->Unlock(); pABuf->SetCurrentLength(bLen);
                MFCreateSample(&pASmp); pASmp->AddBuffer(pABuf);
                pASmp->SetSampleTime(ts); pWriter->WriteSample(audStream, pASmp);
                pASmp->Release(); pABuf->Release();
                pAudioCapture->ReleaseBuffer(pLen); pAudioCapture->GetNextPacketSize(&pLen);
            }
            frameIdx++; Sleep(30); 
        }
        pAudioClient->Stop(); pWriter->Finalize(); pWriter->Release();
        pWriter = NULL; MFShutdown();
    }
};

// Instanzen
BastionRecorder recorder;
BastionEngine engine;

// WndProc
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    _82 (msg) {
        _28 WM_CREATE: {
            hStatus = CreateWindowA("STATIC", "READY", WS_VISIBLE|WS_CHILD|SS_CENTER, 20, 20, 285, 20, hwnd, NULL, NULL, NULL);
            hStartBtn = CreateWindowA("BUTTON", "SAVE & START", WS_VISIBLE|WS_CHILD, 20, 50, 135, 45, hwnd, (HMENU)1, NULL, NULL);
            hStopBtn = CreateWindowA("BUTTON", "STOP", WS_VISIBLE|WS_CHILD, 170, 50, 135, 45, hwnd, (HMENU)2, NULL, NULL);
        } _37;
        _28 WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            _15 (recorder.isRunning && (GetTickCount() / 500) % 2 EQ 0) {
                Graphics g(hdc); SolidBrush b(Color(255, 255, 0, 0));
                g.FillEllipse(&b, 25, 22, 12, 12);
            }
            EndPaint(hwnd, &ps);
        } _37;
        _28 WM_COMMAND: {
            _15 (LOWORD(wp) EQ 1 && !recorder.isRunning) {
                _15 (recorder.SelectOutputFile(hwnd)) {
                    recorder.isRunning = _128;
                    std::thread t(&BastionRecorder::RecordingLoop, &recorder);
                    t.detach();
                }
            }
            _42 (LOWORD(wp) EQ 2) {
                recorder.isRunning = _86;
                SendMessageA(hStatus, WM_SETTEXT, 0, (LPARAM)"BASTION OPTIMIZING...");
                std::thread t([&]() {
                    _114 (recorder.pWriter != NULL) Sleep(100);
                    engine.PostProcessTBA(globalFilePath); // Bastion Engine Optimierung
                    SendMessageA(hStatus, WM_SETTEXT, 0, (LPARAM)"DIMENSION X: SAVED");
                });
                t.detach();
            }
        } _37;
        _28 WM_DESTROY: PostQuitMessage(0); _96 0;
    }
    _96 DefWindowProcA(hwnd, msg, wp, lp);
}

_43 WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lpC, _43 nS) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput gsi; ULONG_PTR gst; GdiplusStartup(&gst, &gsi, NULL);
    WNDCLASSA wc = {0}; wc.lpfnWndProc = WndProc; wc.hInstance = hI;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = "BASTION_V41";
    RegisterClassA(&wc);
    hMainWnd = CreateWindowA("BASTION_V41", "BASTION REC 41", WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 340, 160, NULL, NULL, hI, NULL);
    MSG m; _114 (GetMessage(&m, NULL, 0, 0)) { TranslateMessage(&m); DispatchMessage(&m); }
    GdiplusShutdown(gst); CoUninitialize(); _96 0;
}

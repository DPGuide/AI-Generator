#define _WIN32_WINNT 0x0601 
#include <windows.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <thread>
#include <string>
#include <vector>
#include <atomic>
#include <filesystem>
#include <fstream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

using namespace Gdiplus;
namespace fs = std::filesystem;

typedef void (*ThunderDecompressFunc)(uint16_t*, uint8_t*, int);

HWND hMainWnd, hTextEdit, hNegTextEdit, hSeedEdit, hCountEdit, hWidthEdit, hHeightEdit, hVideoCheck, hStrengthEdit;
std::string currentStatusText = "STATUS: BEREIT"; 
std::string exeDir = "";
char globalModelPath[512] = "";
char globalImgPath[512] = ""; 
Image* gpPreviewImage = nullptr;
std::atomic<bool> isRunning(false);
std::atomic<bool> stopRequested(false);

void UpdatePreview(std::string path) {
    if (!fs::exists(path)) return;
    std::wstring wPath(path.begin(), path.end());
    Bitmap* temp = Bitmap::FromFile(wPath.c_str());
    if (temp && temp->GetLastStatus() == Ok) {
        Bitmap* copy = new Bitmap(temp->GetWidth(), temp->GetHeight(), PixelFormat32bppARGB);
        Graphics g(copy);
        g.DrawImage(temp, 0, 0, temp->GetWidth(), temp->GetHeight());
        Image* old = gpPreviewImage;
        gpPreviewImage = copy; 
        if (old) delete old;
    }
    if (temp) delete temp; 
    InvalidateRect(hMainWnd, nullptr, false);
}

void PurgeSystem() {
    if (gpPreviewImage) { 
        delete gpPreviewImage; 
        gpPreviewImage = nullptr; 
    }
    currentStatusText = "STATUS: RAM GEREINIGT.";
    InvalidateRect(hMainWnd, nullptr, true);
    MessageBoxA(hMainWnd, "VRAM-Vorschau geleert.", "Info", MB_OK);
}

void BakeVideoMP4() {
    currentStatusText = "STATUS: BACKE MP4...";
    InvalidateRect(hMainWnd, nullptr, true);

    std::string ffmpegPath = exeDir + "\\ffmpeg.exe";
    
    // FIX 1: Wir suchen exakt nach %03d (slide_000, slide_001 etc.)
    std::string inputPattern = exeDir + "\\slideshow_output\\slide_%03d.png";
    std::string outputPath = exeDir + "\\B_AI_VIDEO_RESULT.mp4";

    std::string cmd = "cmd /c \"\"" + ffmpegPath + 
                      "\" -y -framerate 10 -i \"" + inputPattern + 
                      "\" -c:v libx264 -pix_fmt yuv420p \"" + outputPath + "\"\"";

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    
    if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        currentStatusText = "STATUS: VIDEO FERTIG!";
        MessageBoxA(hMainWnd, "Video wurde auf der SSD erstellt!", "ERFOLG", MB_OK);
    } else {
        MessageBoxA(hMainWnd, "Fehler: ffmpeg.exe nicht im Ordner gefunden!", "FFMPEG FEHLT", MB_OK | MB_ICONERROR);
    }
    
    InvalidateRect(hMainWnd, nullptr, true);
}


void StableSlideshowLoop(std::string prompt, std::string negPrompt, int startSeed, int totalCount, std::string widthStr, std::string heightStr, bool isVideoMode, std::string strengthStr) {
    isRunning = true;
    stopRequested = false;

    std::string modelToExecute = std::string(globalModelPath);
    std::string exePath = exeDir + "\\sd-cli.exe";
    std::string outDir = exeDir + "\\slideshow_output";
    fs::create_directories(outDir);

    // ==========================================
    // 1. ASSISTENTEN-CHECK FÜR WAN2
    // ==========================================
    std::string extraArgs = "";
    std::string fileName = fs::path(modelToExecute).filename().string();
    bool isWan = (fileName.find("Wan") != std::string::npos || fileName.find("wan") != std::string::npos);

    if (isWan) {
        std::string vaePath = exeDir + "\\wan_2.1_vae.safetensors"; 
        std::string t5Path = exeDir + "\\umt5_xxl_fp8_e4m3fn_scaled.safetensors";
        
        if (fs::exists(vaePath) && fs::exists(t5Path)) {
            extraArgs += " --vae \"" + vaePath + "\"";
            extraArgs += " --t5xxl \"" + t5Path + "\"";
            
            // Nur die absolut nötigen VRAM-Sicherheits-Flags für die GTX 1060
            extraArgs += " --fa --vae-tiling"; 
        }
    }

    // ==========================================
    // 2. DER NATIVE VIDEO-MODUS
    // ==========================================
    if (isVideoMode) {
        currentStatusText = "RENDERE NATIVES VIDEO...";
        InvalidateRect(hMainWnd, nullptr, true);

        std::string outPath = outDir + "\\slide_%03d.png"; 
        
        std::string cmdLine = "cmd /k \"\"" + exePath + "\" -M vid_gen";
        
        // HIER IST DER MAGISCHE FIX: --diffusion-model statt -m für Wan!
        if (isWan) {
            cmdLine += " --diffusion-model \"" + modelToExecute + "\"";
        } else {
            cmdLine += " -m \"" + modelToExecute + "\"";
        }
        
        cmdLine += extraArgs;
        cmdLine += " --prompt \"" + prompt + "\" --negative-prompt \"" + negPrompt + "\"";
        cmdLine += " --seed " + std::to_string(startSeed) + " -W " + widthStr + " -H " + heightStr;
        cmdLine += " --steps 20 --video-frames " + std::to_string(totalCount);
        
        // VRAM Sicherheits-Flags (Zwingend für 1060)
        cmdLine += " --fa --offload-to-cpu --vae-tiling"; 
        
        std::string lastImagePath = std::string(globalImgPath);
        if (!lastImagePath.empty()) {
            cmdLine += " --init-img \"" + lastImagePath + "\"";
            cmdLine += " --strength " + strengthStr; 
        } else if (isWan && fileName.find("TI2V") != std::string::npos) {
            MessageBoxA(hMainWnd, "ACHTUNG: TI2V Modell ohne Bild geladen! Das könnte abstürzen.", "Hinweis", MB_OK);
        }

        cmdLine += " -o \"" + outPath + "\"\"";

        STARTUPINFOA si = { sizeof(si) }; PROCESS_INFORMATION pi = { 0 };
        if (CreateProcessA(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, exeDir.c_str(), &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        }
        
    // ==========================================
    // 3. DER BILDER-MODUS (SD 1.5) - FÜR DEINE DIASHOW
    // ==========================================
    } else {
        // Wir merken uns das allererste Bild, das du per Button geladen hast:
        std::string originalLoadedImage = std::string(globalImgPath); 
        std::string currentInitImage = originalLoadedImage; 
        
        for (int i = 0; i < totalCount; ++i) {
            if (stopRequested) break;
            currentStatusText = "RENDERE FRAME: " + std::to_string(i + 1) + " / " + std::to_string(totalCount);
            InvalidateRect(hMainWnd, nullptr, true);
            
            std::string outPath = outDir + "\\slide_" + std::to_string(i) + ".png";
            std::string cmdLine = "cmd /c \"\"" + exePath + "\" -M img_gen";
            
            if (isWan) {
                cmdLine += " --diffusion-model \"" + modelToExecute + "\"";
            } else {
                cmdLine += " -m \"" + modelToExecute + "\"";
            }
            
            cmdLine += extraArgs;
            cmdLine += " --prompt \"" + prompt + "\" --negative-prompt \"" + negPrompt + "\"";
            cmdLine += " -W " + widthStr + " -H " + heightStr + " --steps 20";
            cmdLine += " --fa --offload-to-cpu --vae-tiling"; 
            
            // Wir nutzen immer unser "currentInitImage" (Das ändert sich je nach Seed-Schalter)
            if (i == 0 && currentInitImage.empty()) {
                cmdLine += " --seed " + std::to_string(startSeed + i);
            } else {
                cmdLine += " --init-img \"" + currentInitImage + "\" --strength " + strengthStr + " --seed " + std::to_string(startSeed + i);
            }
            
            cmdLine += " -o \"" + outPath + "\"\"";
            
            STARTUPINFOA si = { sizeof(si) }; PROCESS_INFORMATION pi = { 0 };
            if (CreateProcessA(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, exeDir.c_str(), &si, &pi)) {
                WaitForSingleObject(pi.hProcess, INFINITE);
                CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                UpdatePreview(outPath); 
                
                // ==========================================
                // HIER IST DEIN NEUER REGIE-SCHALTER!
                // ==========================================
                if (startSeed == 2) {
                    // SEED 2-MODUS: Die fließende Diashow. 
                    // Das frisch gerenderte Bild wird die Vorlage für das nächste!
                    currentInitImage = outPath; 
                } else {
                    // SEED 1-MODUS (und alle anderen Seeds): Der Anker.
                    // Wir werfen das neue Bild weg und springen immer wieder zum Originalbild zurück!
                    currentInitImage = originalLoadedImage;
                }
            }
        }
    }
    
    isRunning = false;
    currentStatusText = "STATUS: BEREIT";
    InvalidateRect(hMainWnd, nullptr, true);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            hMainWnd = hwnd;
            
            int yOffset = 20;
            CreateWindowA("BUTTON", "MODEL WÄHLEN", WS_VISIBLE|WS_CHILD, 20, yOffset, 120, 30, hwnd, (HMENU)1, nullptr, nullptr);
            CreateWindowA("BUTTON", "VORLAGE BILD", WS_VISIBLE|WS_CHILD, 150, yOffset, 120, 30, hwnd, (HMENU)6, nullptr, nullptr);
            CreateWindowA("BUTTON", "PURGE RAM", WS_VISIBLE|WS_CHILD, 400, yOffset, 100, 30, hwnd, (HMENU)9, nullptr, nullptr);
            
            yOffset += 40;
            hVideoCheck = CreateWindowA("BUTTON", "Als Video animieren (vid_gen)", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX, 20, yOffset, 250, 20, hwnd, (HMENU)30, nullptr, nullptr);

            yOffset += 30;
            CreateWindowA("STATIC", "POSITIV PROMPT:", WS_VISIBLE|WS_CHILD, 20, yOffset, 150, 20, hwnd, nullptr, nullptr, nullptr);
            hTextEdit = CreateWindowA("EDIT", "Snoopy dog playing with a small yellow canary bird, 3d pixar style, bright colors, highly detailed", WS_VISIBLE|WS_CHILD|WS_BORDER|ES_MULTILINE|WS_VSCROLL, 20, yOffset + 20, 480, 45, hwnd, nullptr, nullptr, nullptr);
            
            yOffset += 75;
            CreateWindowA("STATIC", "NEGATIV PROMPT:", WS_VISIBLE|WS_CHILD, 20, yOffset, 250, 20, hwnd, nullptr, nullptr, nullptr);
            hNegTextEdit = CreateWindowA("EDIT", "ugly, deformed, blurry, melted, low quality, bad anatomy, bad proportions, watermark, text", WS_VISIBLE|WS_CHILD|WS_BORDER|ES_MULTILINE|WS_VSCROLL, 20, yOffset + 20, 480, 45, hwnd, nullptr, nullptr, nullptr);
            
            yOffset += 75;
            int xPos = 20;
            CreateWindowA("STATIC", "SEED:", WS_VISIBLE|WS_CHILD, xPos, yOffset+3, 40, 20, hwnd, nullptr, nullptr, nullptr);
            hSeedEdit = CreateWindowA("EDIT", "1", WS_VISIBLE|WS_CHILD|WS_BORDER|ES_NUMBER, xPos+45, yOffset, 50, 25, hwnd, nullptr, nullptr, nullptr); xPos += 110;
            
            CreateWindowA("STATIC", "ANZ:", WS_VISIBLE|WS_CHILD, xPos, yOffset+3, 35, 20, hwnd, nullptr, nullptr, nullptr);
            hCountEdit = CreateWindowA("EDIT", "30", WS_VISIBLE|WS_CHILD|WS_BORDER|ES_NUMBER, xPos+40, yOffset, 40, 25, hwnd, nullptr, nullptr, nullptr); xPos += 100;
            
            CreateWindowA("STATIC", "W:", WS_VISIBLE|WS_CHILD, xPos, yOffset+3, 20, 20, hwnd, nullptr, nullptr, nullptr);
            hWidthEdit = CreateWindowA("EDIT", "512", WS_VISIBLE|WS_CHILD|WS_BORDER|ES_NUMBER, xPos+25, yOffset, 45, 25, hwnd, nullptr, nullptr, nullptr); xPos += 80;
            
            CreateWindowA("STATIC", "H:", WS_VISIBLE|WS_CHILD, xPos, yOffset+3, 20, 20, hwnd, nullptr, nullptr, nullptr);
            hHeightEdit = CreateWindowA("EDIT", "512", WS_VISIBLE|WS_CHILD|WS_BORDER|ES_NUMBER, xPos+25, yOffset, 45, 25, hwnd, nullptr, nullptr, nullptr); 
            
            xPos += 80;
            CreateWindowA("STATIC", "STR:", WS_VISIBLE|WS_CHILD, xPos, yOffset+3, 35, 20, hwnd, nullptr, nullptr, nullptr);
            hStrengthEdit = CreateWindowA("EDIT", "0.35", WS_VISIBLE|WS_CHILD|WS_BORDER, xPos+35, yOffset, 40, 25, hwnd, nullptr, nullptr, nullptr);

            yOffset += 40;
            CreateWindowA("BUTTON", "START GENERIERUNG", WS_VISIBLE|WS_CHILD, 20, yOffset, 230, 40, hwnd, (HMENU)10, nullptr, nullptr);
            CreateWindowA("BUTTON", "STOP", WS_VISIBLE|WS_CHILD, 260, yOffset, 230, 40, hwnd, (HMENU)11, nullptr, nullptr);
            
            yOffset += 50;
            CreateWindowA("BUTTON", "BAKE MP4", WS_VISIBLE|WS_CHILD, 20, yOffset, 230, 30, hwnd, (HMENU)20, nullptr, nullptr);
            CreateWindowA("BUTTON", "BAKE GIF", WS_VISIBLE|WS_CHILD, 260, yOffset, 230, 30, hwnd, (HMENU)21, nullptr, nullptr);

        } break;
        
        case WM_COMMAND: {
            int id = LOWORD(wp);
            
            if (id == 1) { 
                OPENFILENAMEA ofn = {0}; 
                ofn.lStructSize = sizeof(ofn); 
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = "AI Models (*.safetensors; *.tba)\0*.safetensors;*.tba\0All Files\0*.*\0"; 
                ofn.lpstrFile = globalModelPath; 
                ofn.nMaxFile = 512; 
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR; 
    
                if (GetOpenFileNameA(&ofn)) {
                    currentStatusText = "MODELL GELADEN: " + fs::path(globalModelPath).filename().string();
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
            if (id == 6) { 
                 OPENFILENAMEA ofn = {0}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
                 ofn.lpstrFilter = "Images\0*.jpg;*.png\0"; ofn.lpstrFile = globalImgPath; ofn.nMaxFile = 260;
                 ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR; GetOpenFileNameA(&ofn);
            }
            if (id == 9) PurgeSystem();
            
            if (id == 10 && !isRunning) {
                char pBuf[1024], nBuf[1024], sBuf[20], cBuf[20], wBuf[10], hBuf[10], strBuf[10];

                GetWindowTextA(hTextEdit, pBuf, 1024);
                GetWindowTextA(hNegTextEdit, nBuf, 1024); 
                GetWindowTextA(hSeedEdit, sBuf, 20);
                GetWindowTextA(hCountEdit, cBuf, 20);
                GetWindowTextA(hWidthEdit, wBuf, 10);     
                GetWindowTextA(hHeightEdit, hBuf, 10);    
                GetWindowTextA(hStrengthEdit, strBuf, 10); 

                bool isVideoMode = (SendMessage(hVideoCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

                std::thread(StableSlideshowLoop, std::string(pBuf), std::string(nBuf), atoi(sBuf), atoi(cBuf), std::string(wBuf), std::string(hBuf), isVideoMode, std::string(strBuf)).detach();
            }
            
            // DIE WICHTIGEN BUTTONS SIND WIEDER DA!
            if (id == 11) stopRequested = true;
            if (id == 20) std::thread(BakeVideoMP4).detach();
            
        } break;
        
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            Graphics g(hdc);
            RECT r = {20, 340, 320, 380}; DrawTextA(hdc, currentStatusText.c_str(), -1, &r, DT_LEFT);
            if (gpPreviewImage) g.DrawImage(gpPreviewImage, 360, 340, 140, 140);
            EndPaint(hwnd, &ps);
        } break;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lpC, int nS) {
    char path[MAX_PATH]; GetModuleFileNameA(NULL, path, MAX_PATH);
    PathRemoveFileSpecA(path); exeDir = path;
    GdiplusStartupInput gsi; ULONG_PTR gst; GdiplusStartup(&gst, &gsi, nullptr);
    WNDCLASSA wc = {0}; wc.lpfnWndProc = WndProc; wc.hInstance = hI;
    wc.lpszClassName = "B_AI_ULTIMATE"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassA(&wc);
    CreateWindowA("B_AI_ULTIMATE", "BASTION AI - COMMAND CENTER", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 530, 560, nullptr, nullptr, hI, nullptr);
    MSG m; while (GetMessage(&m, nullptr, 0, 0)) { TranslateMessage(&m); DispatchMessage(&m); }
    GdiplusShutdown(gst); return 0;
}
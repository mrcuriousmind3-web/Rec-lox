#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "resource.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
constexpr wchar_t kWindowClass[] = L"RecLoxMainWindow";
constexpr UINT WM_APP_STATE = WM_APP + 1;
constexpr UINT WM_APP_ERROR = WM_APP + 2;
constexpr UINT WM_APP_FINISHED = WM_APP + 3;

constexpr int ID_BTN_RECORD = 1001;
constexpr int ID_BTN_PAUSE = 1002;
constexpr int ID_BTN_OPEN = 1003;
constexpr int ID_BTN_GITHUB = 1004;
constexpr int ID_STATUS = 1005;
constexpr int ID_HOTKEY_TOGGLE = 2001;
constexpr int ID_HOTKEY_PAUSE = 2002;

enum class UiState : int { Idle, Starting, Recording, Paused, Stopping };

HWND g_window = nullptr;
HWND g_recordButton = nullptr;
HWND g_pauseButton = nullptr;
HWND g_statusLabel = nullptr;
HWND g_footerLabel = nullptr;
HFONT g_titleFont = nullptr;
HFONT g_bodyFont = nullptr;
HFONT g_buttonFont = nullptr;
HBRUSH g_backgroundBrush = nullptr;

HANDLE g_worker = nullptr;
std::atomic<bool> g_stop{ false };
std::atomic<bool> g_paused{ false };
std::atomic<int> g_state{ static_cast<int>(UiState::Idle) };
std::mutex g_errorMutex;
std::wstring g_lastError;
std::filesystem::path g_outputDirectory;

void SetLastErrorText(const std::wstring& value) {
    std::lock_guard<std::mutex> lock(g_errorMutex);
    g_lastError = value;
}

std::wstring GetLastErrorText() {
    std::lock_guard<std::mutex> lock(g_errorMutex);
    return g_lastError;
}

std::wstring HrText(HRESULT hr) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
    return buffer;
}

void ReportFailure(const wchar_t* step, HRESULT hr) {
    std::wstring message = step;
    message += L"\n\nWindows error: ";
    message += HrText(hr);
    SetLastErrorText(message);
}

std::filesystem::path GetVideosDirectory() {
    PWSTR knownFolder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, KF_FLAG_CREATE, nullptr, &knownFolder)) && knownFolder) {
        std::filesystem::path result(knownFolder);
        CoTaskMemFree(knownFolder);
        return result;
    }

    wchar_t profile[MAX_PATH]{};
    DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (length > 0 && length < MAX_PATH) return std::filesystem::path(profile) / L"Videos";
    return std::filesystem::current_path();
}

std::filesystem::path MakeOutputPath() {
    g_outputDirectory = GetVideosDirectory() / L"Rec-lox";
    std::error_code ec;
    std::filesystem::create_directories(g_outputDirectory, ec);

    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::wstringstream name;
    name << L"Roblox_" << std::setfill(L'0')
         << std::setw(4) << time.wYear << L"-"
         << std::setw(2) << time.wMonth << L"-"
         << std::setw(2) << time.wDay << L"_"
         << std::setw(2) << time.wHour << L"-"
         << std::setw(2) << time.wMinute << L"-"
         << std::setw(2) << time.wSecond << L".mp4";
    return g_outputDirectory / name.str();
}

HRESULT CreateVideoWriter(const std::filesystem::path& filePath, UINT32 width, UINT32 height,
                          UINT32 fps, IMFSinkWriter** writerOut, DWORD* streamOut) {
    *writerOut = nullptr;
    *streamOut = 0;

    IMFSinkWriter* writer = nullptr;
    IMFMediaType* outputType = nullptr;
    IMFMediaType* inputType = nullptr;
    HRESULT hr = MFCreateSinkWriterFromURL(filePath.c_str(), nullptr, nullptr, &writer);
    if (FAILED(hr)) goto done;

    hr = MFCreateMediaType(&outputType);
    if (FAILED(hr)) goto done;
    hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr)) goto done;
    hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (FAILED(hr)) goto done;
    hr = outputType->SetUINT32(MF_MT_AVG_BITRATE, 10000000);
    if (FAILED(hr)) goto done;
    hr = outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(hr)) goto done;
    hr = MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, width, height);
    if (FAILED(hr)) goto done;
    hr = MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, fps, 1);
    if (FAILED(hr)) goto done;
    hr = MFSetAttributeRatio(outputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(hr)) goto done;

    hr = writer->AddStream(outputType, streamOut);
    if (FAILED(hr)) goto done;

    hr = MFCreateMediaType(&inputType);
    if (FAILED(hr)) goto done;
    hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr)) goto done;
    hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(hr)) goto done;
    hr = inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(hr)) goto done;
    hr = inputType->SetUINT32(MF_MT_DEFAULT_STRIDE, width * 4);
    if (FAILED(hr)) goto done;
    hr = MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, width, height);
    if (FAILED(hr)) goto done;
    hr = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, fps, 1);
    if (FAILED(hr)) goto done;
    hr = MFSetAttributeRatio(inputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(hr)) goto done;

    hr = writer->SetInputMediaType(*streamOut, inputType, nullptr);
    if (FAILED(hr)) goto done;
    hr = writer->BeginWriting();
    if (FAILED(hr)) goto done;

    *writerOut = writer;
    writer = nullptr;

done:
    if (inputType) inputType->Release();
    if (outputType) outputType->Release();
    if (writer) writer->Release();
    return hr;
}

HRESULT WriteFrame(IMFSinkWriter* writer, DWORD streamIndex, const BYTE* pixels,
                   DWORD byteCount, LONGLONG timestamp, LONGLONG duration) {
    IMFMediaBuffer* buffer = nullptr;
    IMFSample* sample = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(byteCount, &buffer);
    if (FAILED(hr)) return hr;

    BYTE* destination = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    hr = buffer->Lock(&destination, &maxLength, &currentLength);
    if (SUCCEEDED(hr)) {
        if (maxLength < byteCount) {
            hr = E_FAIL;
        } else {
            std::memcpy(destination, pixels, byteCount);
            hr = buffer->SetCurrentLength(byteCount);
        }
        buffer->Unlock();
    }

    if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer);
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(timestamp);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(duration);
    if (SUCCEEDED(hr)) hr = writer->WriteSample(streamIndex, sample);

    if (sample) sample->Release();
    if (buffer) buffer->Release();
    return hr;
}

bool RecordPrimaryDisplay() {
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) {
        ReportFailure(L"Could not initialize Windows recording services.", comResult);
        return false;
    }

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) {
        ReportFailure(L"Could not start Windows Media Foundation.", hr);
        CoUninitialize();
        return false;
    }

    bool success = false;
    bool writerStarted = false;
    IMFSinkWriter* writer = nullptr;
    DWORD streamIndex = 0;
    HDC screen = nullptr;
    HDC memory = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    BYTE* pixels = nullptr;
    std::filesystem::path outputPath;

    do {
        int rawWidth = GetSystemMetrics(SM_CXSCREEN);
        int rawHeight = GetSystemMetrics(SM_CYSCREEN);
        if (rawWidth < 2 || rawHeight < 2) {
            ReportFailure(L"Could not detect the primary display.", E_FAIL);
            break;
        }

        const UINT32 width = static_cast<UINT32>(rawWidth & ~1);
        const UINT32 height = static_cast<UINT32>(rawHeight & ~1);
        constexpr UINT32 fps = 30;
        const DWORD byteCount = width * height * 4;

        outputPath = MakeOutputPath();
        hr = CreateVideoWriter(outputPath, width, height, fps, &writer, &streamIndex);
        if (FAILED(hr)) {
            ReportFailure(L"Could not create the MP4 encoder. Check that Windows Media Foundation and the H.264 video encoder are available.", hr);
            break;
        }
        writerStarted = true;

        screen = GetDC(nullptr);
        memory = CreateCompatibleDC(screen);
        if (!screen || !memory) {
            ReportFailure(L"Could not create the screen capture surface.", HRESULT_FROM_WIN32(GetLastError()));
            break;
        }

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(width);
        info.bmiHeader.biHeight = -static_cast<LONG>(height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        if (!bitmap || !pixels) {
            ReportFailure(L"Could not allocate the capture frame buffer.", HRESULT_FROM_WIN32(GetLastError()));
            break;
        }
        previousBitmap = SelectObject(memory, bitmap);

        PostMessageW(g_window, WM_APP_STATE, static_cast<WPARAM>(UiState::Recording), 0);
        const LONGLONG frameDuration = 10000000LL / fps;
        LONGLONG frameNumber = 0;
        const auto frameInterval = std::chrono::microseconds(1000000 / fps);
        auto nextFrame = std::chrono::steady_clock::now();

        while (!g_stop.load()) {
            if (g_paused.load()) {
                Sleep(40);
                nextFrame = std::chrono::steady_clock::now() + frameInterval;
                continue;
            }

            if (!BitBlt(memory, 0, 0, static_cast<int>(width), static_cast<int>(height),
                        screen, 0, 0, SRCCOPY | CAPTUREBLT)) {
                hr = HRESULT_FROM_WIN32(GetLastError());
                ReportFailure(L"Screen capture failed while recording.", hr);
                break;
            }

            CURSORINFO cursorInfo{};
            cursorInfo.cbSize = sizeof(cursorInfo);
            if (GetCursorInfo(&cursorInfo) && cursorInfo.flags == CURSOR_SHOWING) {
                int hotX = 0;
                int hotY = 0;
                ICONINFO iconInfo{};
                if (GetIconInfo(cursorInfo.hCursor, &iconInfo)) {
                    hotX = static_cast<int>(iconInfo.xHotspot);
                    hotY = static_cast<int>(iconInfo.yHotspot);
                    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
                    if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
                }
                DrawIconEx(memory, cursorInfo.ptScreenPos.x - hotX, cursorInfo.ptScreenPos.y - hotY,
                           cursorInfo.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
            }

            hr = WriteFrame(writer, streamIndex, pixels, byteCount, frameNumber * frameDuration, frameDuration);
            if (FAILED(hr)) {
                ReportFailure(L"Could not write a video frame to the MP4 file.", hr);
                break;
            }
            ++frameNumber;

            nextFrame += frameInterval;
            const auto now = std::chrono::steady_clock::now();
            if (nextFrame > now) {
                std::this_thread::sleep_until(nextFrame);
            } else {
                nextFrame = now;
            }
        }

        if (SUCCEEDED(hr)) success = true;
    } while (false);

    if (writer && writerStarted) {
        HRESULT finalResult = writer->Finalize();
        if (FAILED(finalResult)) {
            ReportFailure(L"Could not finalize the MP4 recording.", finalResult);
            success = false;
        }
    }
    if (writer) writer->Release();
    if (previousBitmap && previousBitmap != HGDI_ERROR) SelectObject(memory, previousBitmap);
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    if (screen) ReleaseDC(nullptr, screen);

    MFShutdown();
    CoUninitialize();

    if (success) {
        SetLastErrorText(L"");
    } else if (!GetLastErrorText().empty()) {
        // The UI receives the detailed failure message from the worker thread.
    } else {
        SetLastErrorText(L"Recording ended before a video frame could be written.");
    }
    return success;
}

DWORD WINAPI RecordingThread(void*) {
    g_stop.store(false);
    g_paused.store(false);
    const bool success = RecordPrimaryDisplay();
    if (!success) PostMessageW(g_window, WM_APP_ERROR, 0, 0);
    PostMessageW(g_window, WM_APP_FINISHED, 0, 0);
    return 0;
}

void SetUiState(UiState state) {
    g_state.store(static_cast<int>(state));
    if (!g_statusLabel) return;

    const wchar_t* status = L"READY  ·  PRESS F8 OR CLICK START";
    const wchar_t* recordText = L"Start recording  ·  F8";
    const wchar_t* pauseText = L"Pause  ·  F9";
    bool pauseEnabled = false;

    switch (state) {
    case UiState::Starting:
        status = L"PREPARING YOUR RECORDING…";
        recordText = L"Starting…";
        break;
    case UiState::Recording:
        status = L"●  RECORDING  ·  F8 TO STOP";
        recordText = L"Stop recording  ·  F8";
        pauseEnabled = true;
        break;
    case UiState::Paused:
        status = L"Ⅱ  PAUSED  ·  F9 TO RESUME";
        recordText = L"Stop recording  ·  F8";
        pauseText = L"Resume  ·  F9";
        pauseEnabled = true;
        break;
    case UiState::Stopping:
        status = L"FINALIZING MP4…";
        recordText = L"Finishing…";
        break;
    case UiState::Idle:
    default:
        break;
    }

    SetWindowTextW(g_statusLabel, status);
    SetWindowTextW(g_recordButton, recordText);
    SetWindowTextW(g_pauseButton, pauseText);
    EnableWindow(g_recordButton, state != UiState::Starting && state != UiState::Stopping);
    EnableWindow(g_pauseButton, pauseEnabled);
    InvalidateRect(g_recordButton, nullptr, TRUE);
    InvalidateRect(g_pauseButton, nullptr, TRUE);
}

void StartRecording() {
    if (g_worker) return;
    g_stop.store(false);
    g_paused.store(false);
    SetLastErrorText(L"");
    SetUiState(UiState::Starting);
    g_worker = CreateThread(nullptr, 0, RecordingThread, nullptr, 0, nullptr);
    if (!g_worker) {
        SetUiState(UiState::Idle);
        MessageBoxW(g_window, L"Windows could not start the recording thread.", L"Rec-lox", MB_OK | MB_ICONERROR);
    }
}

void StopRecording() {
    if (!g_worker) return;
    g_stop.store(true);
    SetUiState(UiState::Stopping);
}

void ToggleRecording() {
    if (!g_worker) {
        StartRecording();
        return;
    }
    const auto state = static_cast<UiState>(g_state.load());
    if (state == UiState::Recording || state == UiState::Paused || state == UiState::Starting) StopRecording();
}

void TogglePause() {
    if (!g_worker) return;
    const auto state = static_cast<UiState>(g_state.load());
    if (state != UiState::Recording && state != UiState::Paused) return;
    const bool paused = !g_paused.load();
    g_paused.store(paused);
    SetUiState(paused ? UiState::Paused : UiState::Recording);
}

void OpenRecordingsFolder() {
    if (g_outputDirectory.empty()) g_outputDirectory = GetVideosDirectory() / L"Rec-lox";
    std::error_code ec;
    std::filesystem::create_directories(g_outputDirectory, ec);
    ShellExecuteW(g_window, L"open", g_outputDirectory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenGitHub() {
    ShellExecuteW(g_window, L"open", L"https://github.com/mrcuriousmind3-web/Rec-lox", nullptr, nullptr, SW_SHOWNORMAL);
}

HWND AddStatic(HWND parent, const wchar_t* text, int x, int y, int width, int height, int id, HFONT font) {
    HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
}

HWND AddButton(HWND parent, const wchar_t* text, int x, int y, int width, int height, int id) {
    HWND control = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_buttonFont), TRUE);
    return control;
}

void CreateControls(HWND hwnd) {
    g_titleFont = CreateFontW(-31, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_bodyFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_buttonFont = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    AddStatic(hwnd, L"Rec-lox", 34, 25, 450, 42, 1010, g_titleFont);
    AddStatic(hwnd, L"Lightweight screen recording for your Roblox sessions.", 36, 72, 480, 25, 1011, g_bodyFont);
    g_statusLabel = AddStatic(hwnd, L"READY  ·  PRESS F8 OR CLICK START", 36, 126, 480, 27, ID_STATUS, g_buttonFont);

    g_recordButton = AddButton(hwnd, L"Start recording  ·  F8", 36, 176, 230, 55, ID_BTN_RECORD);
    g_pauseButton = AddButton(hwnd, L"Pause  ·  F9", 282, 176, 230, 55, ID_BTN_PAUSE);
    g_pauseButton ? EnableWindow(g_pauseButton, FALSE) : FALSE;

    AddButton(hwnd, L"Open recordings folder", 36, 250, 230, 44, ID_BTN_OPEN);
    AddButton(hwnd, L"View source on GitHub", 282, 250, 230, 44, ID_BTN_GITHUB);
    g_footerLabel = AddStatic(hwnd, L"F8  Start / stop     ·     F9  Pause / resume", 36, 322, 480, 24, 1012, g_bodyFont);
    AddStatic(hwnd, L"MP4 files are saved locally in Videos  >  Rec-lox", 36, 350, 480, 23, 1013, g_bodyFont);
}

void DrawButton(const DRAWITEMSTRUCT* item) {
    const int id = static_cast<int>(item->CtlID);
    COLORREF fill = RGB(37, 48, 68);
    COLORREF text = RGB(236, 241, 248);
    if (id == ID_BTN_RECORD) {
        fill = (g_state.load() == static_cast<int>(UiState::Recording) ||
                g_state.load() == static_cast<int>(UiState::Paused))
            ? RGB(194, 69, 78) : RGB(105, 222, 145);
        text = RGB(15, 24, 31);
    } else if (id == ID_BTN_PAUSE) {
        fill = RGB(49, 66, 91);
    }

    if (!IsWindowEnabled(item->hwndItem)) {
        fill = RGB(35, 42, 55);
        text = RGB(117, 128, 145);
    } else if (item->itemState & ODS_SELECTED) {
        fill = RGB(GetRValue(fill) * 8 / 10, GetGValue(fill) * 8 / 10, GetBValue(fill) * 8 / 10);
    }

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, fill);
    HGDIOBJ oldBrush = SelectObject(item->hDC, brush);
    HGDIOBJ oldPen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, item->rcItem.left, item->rcItem.top,
        item->rcItem.right, item->rcItem.bottom, 14, 14);
    SelectObject(item->hDC, oldBrush);
    SelectObject(item->hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, text);
    RECT textRect = item->rcItem;
    wchar_t label[128]{};
    GetWindowTextW(item->hwndItem, label, static_cast<int>(std::size(label)));
    DrawTextW(item->hDC, label, -1, &textRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateControls(hwnd);
        RegisterHotKey(hwnd, ID_HOTKEY_TOGGLE, MOD_NOREPEAT, VK_F8);
        RegisterHotKey(hwnd, ID_HOTKEY_PAUSE, MOD_NOREPEAT, VK_F9);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BTN_RECORD: ToggleRecording(); return 0;
        case ID_BTN_PAUSE: TogglePause(); return 0;
        case ID_BTN_OPEN: OpenRecordingsFolder(); return 0;
        case ID_BTN_GITHUB: OpenGitHub(); return 0;
        }
        break;

    case WM_HOTKEY:
        if (wParam == ID_HOTKEY_TOGGLE) ToggleRecording();
        if (wParam == ID_HOTKEY_PAUSE) TogglePause();
        return 0;

    case WM_APP_STATE:
        SetUiState(static_cast<UiState>(wParam));
        return 0;

    case WM_APP_ERROR: {
        const std::wstring error = GetLastErrorText();
        MessageBoxW(hwnd, error.empty() ? L"An unexpected recording error occurred." : error.c_str(),
            L"Rec-lox recording error", MB_OK | MB_ICONERROR);
        return 0;
    }

    case WM_APP_FINISHED:
        if (g_worker) {
            CloseHandle(g_worker);
            g_worker = nullptr;
        }
        g_paused.store(false);
        g_stop.store(false);
        SetUiState(UiState::Idle);
        return 0;

    case WM_DRAWITEM:
        DrawButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        const HWND control = reinterpret_cast<HWND>(lParam);
        if (GetDlgCtrlID(control) == ID_STATUS) {
            SetTextColor(dc, g_state.load() == static_cast<int>(UiState::Recording)
                ? RGB(112, 235, 157) : RGB(183, 195, 211));
        } else {
            SetTextColor(dc, RGB(173, 185, 203));
        }
        return reinterpret_cast<LRESULT>(g_backgroundBrush);
    }

    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect, g_backgroundBrush);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(48, 61, 81));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, 36, 112, nullptr);
        LineTo(dc, rect.right - 36, 112);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        EndPaint(hwnd, &paint);
        return 0;
    }

    case WM_DESTROY:
        if (g_worker) {
            g_stop.store(true);
            WaitForSingleObject(g_worker, INFINITE);
            CloseHandle(g_worker);
            g_worker = nullptr;
        }
        UnregisterHotKey(hwnd, ID_HOTKEY_TOGGLE);
        UnregisterHotKey(hwnd, ID_HOTKEY_PAUSE);
        if (g_titleFont) DeleteObject(g_titleFont);
        if (g_bodyFont) DeleteObject(g_bodyFont);
        if (g_buttonFont) DeleteObject(g_buttonFont);
        if (g_backgroundBrush) DeleteObject(g_backgroundBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDPIAware();
    g_backgroundBrush = CreateSolidBrush(RGB(17, 22, 34));

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    windowClass.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = g_backgroundBrush;
    windowClass.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&windowClass)) return 1;

    RECT bounds{ 0, 0, 550, 405 };
    AdjustWindowRect(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    g_window = CreateWindowExW(0, kWindowClass, L"Rec-lox — Roblox screen recorder",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, nullptr);

    if (!g_window) return 1;
    ShowWindow(g_window, showCommand);
    UpdateWindow(g_window);
    SetUiState(UiState::Idle);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

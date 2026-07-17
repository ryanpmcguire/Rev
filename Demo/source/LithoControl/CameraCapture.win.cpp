module;

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

module LithoControl.Interface;   // implementation unit -- no 'export'

namespace LithoControl {

    void Interface::scanCameras() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool uninitCom = (hr == S_OK);
        MFStartup(MF_VERSION);

        cameraDevices.clear();
        IMFAttributes* pAttr = nullptr;
        MFCreateAttributes(&pAttr, 1);
        pAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                       MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

        IMFActivate** ppDevices = nullptr;
        UINT32 count = 0;
        MFEnumDeviceSources(pAttr, &ppDevices, &count);
        pAttr->Release();

        std::vector<Dropdown::Option> opts;
        for (UINT32 i = 0; i < count; i++) {
            WCHAR* name = nullptr; UINT32 nameLen = 0;
            ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                              &name, &nameLen);
            std::string nameStr;
            if (name) {
                int n = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
                nameStr.resize((size_t)n - 1);
                WideCharToMultiByte(CP_UTF8, 0, name, -1, nameStr.data(), n, nullptr, nullptr);
                CoTaskMemFree(name);
            }
            if (nameStr.empty()) nameStr = "Camera " + std::to_string(i);
            cameraDevices.push_back({ nameStr });
            opts.push_back({ nameStr, std::to_string(i) });
            ppDevices[i]->Release();
        }
        CoTaskMemFree(ppDevices);
        MFShutdown();
        if (uninitCom) CoUninitialize();

        if (cameraDrop) cameraDrop->params.options = opts;
        logQ.push("[CAM] Found " + std::to_string(count) + " camera(s)");
    }

    void Interface::startCamera() {
        if (!cameraDrop || cameraDrop->params.value.empty()) {
            logQ.push("[CAM] Select a camera first"); return;
        }
        int idx = 0;
        try { idx = std::stoi(cameraDrop->params.value); }
        catch (...) { logQ.push("[CAM] Invalid device"); return; }

        if (cameraBtnTxt) cameraBtnTxt->content = "STOP";
        cameraRunning = true;
        if (cameraThread.joinable()) cameraThread.detach();
        cameraThread = std::thread([this, idx]() { runCameraCapture(idx); });
    }

    void Interface::stopCamera() {
        cameraRunning = false;
        auto* r = reinterpret_cast<IMFSourceReader*>(cameraReader.load());
        if (r) r->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        if (cameraThread.joinable()) cameraThread.join();
        if (cameraBtnTxt) cameraBtnTxt->content = "START";
    }

    void Interface::runCameraCapture(int deviceIdx) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool uninitCom = (hr == S_OK);
        MFStartup(MF_VERSION);

        IMFAttributes* pAttr = nullptr;
        MFCreateAttributes(&pAttr, 1);
        pAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                       MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        IMFActivate** ppDevices = nullptr;
        UINT32 count = 0;
        MFEnumDeviceSources(pAttr, &ppDevices, &count);
        pAttr->Release();

        if (deviceIdx < 0 || deviceIdx >= (int)count) {
            for (UINT32 i = 0; i < count; i++) ppDevices[i]->Release();
            CoTaskMemFree(ppDevices);
            logQ.push("[CAM] Device index out of range");
            cameraRunning = false;
            MFShutdown(); if (uninitCom) CoUninitialize(); return;
        }

        IMFMediaSource* pSource = nullptr;
        hr = ppDevices[deviceIdx]->ActivateObject(IID_PPV_ARGS(&pSource));
        for (UINT32 i = 0; i < count; i++) ppDevices[i]->Release();
        CoTaskMemFree(ppDevices);

        if (FAILED(hr)) {
            logQ.push("[CAM] Failed to activate device");
            cameraRunning = false;
            MFShutdown(); if (uninitCom) CoUninitialize(); return;
        }

        // Enable the MF video processor so we can request RGB32 output
        // regardless of the camera's native format (NV12, YUY2, etc.)
        IMFAttributes* pReaderAttr = nullptr;
        MFCreateAttributes(&pReaderAttr, 1);
        pReaderAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

        IMFSourceReader* pReader = nullptr;
        hr = MFCreateSourceReaderFromMediaSource(pSource, pReaderAttr, &pReader);
        pReaderAttr->Release();
        pSource->Release();

        if (FAILED(hr)) {
            logQ.push("[CAM] Failed to create source reader");
            cameraRunning = false;
            MFShutdown(); if (uninitCom) CoUninitialize(); return;
        }

        // Request RGB32 (BGRA, bottom-up) output so no YUV conversion needed
        IMFMediaType* pType = nullptr;
        MFCreateMediaType(&pType);
        pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                          nullptr, pType);
        pType->Release();

        if (FAILED(hr)) {
            logQ.push("[CAM] RGB32 not supported by this camera");
            pReader->Release();
            cameraRunning = false;
            MFShutdown(); if (uninitCom) CoUninitialize(); return;
        }

        IMFMediaType* pActual = nullptr;
        pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pActual);
        UINT32 fw = 0, fh = 0;
        MFGetAttributeSize(pActual, MF_MT_FRAME_SIZE, &fw, &fh);
        pActual->Release();

        logQ.push("[CAM] Live: " + std::to_string(fw) + "x" + std::to_string(fh));
        cameraReader.store(pReader);

        while (cameraRunning) {
            DWORD streamIndex = 0, flags = 0;
            LONGLONG timestamp = 0;
            IMFSample* pSample = nullptr;
            hr = pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                     0, &streamIndex, &flags, &timestamp, &pSample);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
            if (!pSample) continue;

            IMFMediaBuffer* pBuffer = nullptr;
            pSample->ConvertToContiguousBuffer(&pBuffer);
            BYTE* pData = nullptr; DWORD maxLen = 0, curLen = 0;
            pBuffer->Lock(&pData, &maxLen, &curLen);

            // RGB32 = BGR0 packed, bottom-up -> flip Y and swap B/R for RGBA
            int w = (int)fw, h = (int)fh;
            std::vector<uint8_t> rgba((size_t)w * h * 4);
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    int si = ((h - 1 - y) * w + x) * 4;
                    int di = (y * w + x) * 4;
                    rgba[di+0] = pData[si+2];
                    rgba[di+1] = pData[si+1];
                    rgba[di+2] = pData[si+0];
                    rgba[di+3] = 255;
                }
            }

            pBuffer->Unlock();
            pBuffer->Release();
            pSample->Release();

            {
                std::lock_guard<std::mutex> lk(cameraFrameMtx);
                cameraFrameRGBA = std::move(rgba);
                cameraFrameW = w; cameraFrameH = h;
            }
            cameraFrameReady = true;
            requestRepaint();
        }

        cameraReader.store(nullptr);
        pReader->Release();
        cameraRunning = false;
        MFShutdown();
        if (uninitCom) CoUninitialize();
        logQ.push("[CAM] Stopped");
    }

    // ---------------------------------------------------------------------
    // Slicer subprocess (see SlicerSubprocess.lnx.cpp for the Linux side).
    //
    // Lives here rather than its own SlicerSubprocess.win.cpp: a standalone
    // 4th implementation unit of LithoControl.Interface reproducibly hit the
    // same MSVC C++20-modules internal compiler error (C1116, "importing
    // module Rev.SocketClient... Specialization of std::_Stop_callback_base::
    // _Do_attach") already worked around once for EdidApply -- see
    // HdmiWindow.win.cpp for the full diagnosis. Folding it into this
    // existing (3rd) implementation unit sidesteps it again.
    // ---------------------------------------------------------------------

    int Interface::runCapturedProcess(const std::string& cmd, const std::string& cwd,
                                       const std::function<void(const std::string&)>& onLine) {
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        HANDLE hR, hW;
        CreatePipe(&hR, &hW, &sa, 0);
        SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{};
        si.cb         = sizeof(si);
        si.hStdOutput = hW;
        si.hStdError  = hW;
        si.dwFlags    = STARTF_USESTDHANDLES;

        const char* cwdArg = cwd.empty() ? nullptr : cwd.c_str();

        PROCESS_INFORMATION pi{};
        // CreateProcessA requires a mutable command-line buffer.
        std::string mutableCmd = cmd;
        BOOL ok = CreateProcessA(nullptr, mutableCmd.data(),
                                 nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW, nullptr, cwdArg, &si, &pi);
        CloseHandle(hW);

        if (!ok) {
            CloseHandle(hR);
            return -1;
        }

        char buf[256];
        DWORD rd;
        std::string pending;
        while (ReadFile(hR, buf, sizeof(buf) - 1, &rd, nullptr) && rd > 0) {
            buf[rd] = '\0';
            pending.append(buf, rd);

            size_t pos;
            while ((pos = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, pos);
                pending.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) onLine(line);
            }
        }
        if (!pending.empty()) onLine(pending);

        DWORD exitCode = 1;
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hR);

        return (int)exitCode;
    }

} // namespace LithoControl

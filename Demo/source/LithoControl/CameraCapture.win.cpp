module;

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <strmif.h>
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>

module LithoControl.Interface;   // implementation unit -- no 'export'

import Rev.AmcamCamera;

namespace LithoControl {

    // Forward declarations -- defined further down alongside runCameraCapture()
    // (the only place queryCameraExtendedRange() has an IMFMediaSource to query),
    // but releaseCameraExtendedCtrl() also needs to be reachable from
    // runCameraCaptureAmcam() above it.
    static void releaseCameraExtendedCtrl(Interface* self);

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
            opts.push_back({ nameStr, "mf:" + std::to_string(i) });
            ppDevices[i]->Release();
        }
        CoTaskMemFree(ppDevices);
        MFShutdown();
        if (uninitCom) CoUninitialize();

        // AmScope vendor-SDK devices (amcam.dll) -- covers cameras like the
        // MU130 that the calib-dt tooling drives directly through the same
        // SDK rather than through the generic MediaFoundation/UVC path above.
        size_t amcamCount = 0;
        for (const auto& d : Rev::AmcamCamera::listDevices()) {
            std::string label = "AmScope: " + d.name;
            cameraDevices.push_back({ label });
            opts.push_back({ label, "amcam:" + d.id });
            amcamCount++;
        }

        if (cameraDrop) cameraDrop->params.options = opts;
        logQ.push("[CAM] Found " + std::to_string(count) + " camera(s), "
                   + std::to_string(amcamCount) + " AmScope SDK device(s)");
    }

    void Interface::startCamera() {
        if (!cameraDrop || cameraDrop->params.value.empty()) {
            logQ.push("[CAM] Select a camera first"); return;
        }

        const std::string& value = cameraDrop->params.value;

        if (value.rfind("amcam:", 0) == 0) {
            std::string deviceId = value.substr(6);
            cameraBackend = CameraBackend::AmScope;

            if (cameraBtnTxt) cameraBtnTxt->content = "STOP";
            cameraRunning = true;
            if (cameraThread.joinable()) cameraThread.detach();
            cameraThread = std::thread([this, deviceId]() { runCameraCaptureAmcam(deviceId); });
            return;
        }

        std::string idxStr = value.rfind("mf:", 0) == 0 ? value.substr(3) : value;
        int idx = 0;
        try { idx = std::stoi(idxStr); }
        catch (...) { logQ.push("[CAM] Invalid device"); return; }

        cameraBackend = CameraBackend::MediaFoundation;
        if (cameraBtnTxt) cameraBtnTxt->content = "STOP";
        cameraRunning = true;
        if (cameraThread.joinable()) cameraThread.detach();
        cameraThread = std::thread([this, idx]() { runCameraCapture(idx); });
    }

    void Interface::stopCamera() {
        cameraRunning = false;
        if (cameraBackend == CameraBackend::MediaFoundation) {
            auto* r = reinterpret_cast<IMFSourceReader*>(cameraReader.load());
            if (r) r->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        }
        if (cameraThread.joinable()) cameraThread.join();
        if (cameraBtnTxt) cameraBtnTxt->content = "START";
    }

    // AmScope vendor-SDK capture path (see Rev.AmcamCamera / AmcamCamera.win.ixx).
    // The SDK delivers frames on its own thread via Rev::AmcamCamera's
    // callback; this function just keeps the RAII camera object alive until
    // stopCamera() clears cameraRunning.
    void Interface::runCameraCaptureAmcam(const std::string& deviceId) {
        logQ.push("[CAM] Starting AmScope SDK capture...");

        // No manual exposure/gain surface on this backend -- the SDK drives
        // its own auto-exposure (see AmcamCamera.win.ixx). Ensure any
        // extended-settings state left over from a prior MF session is
        // cleared so the (hidden, per CameraBackend::AmScope gating) UI
        // doesn't retain stale values.
        releaseCameraExtendedCtrl(this);

        Rev::AmcamCamera cam(deviceId, [this](const uint8_t* bgr, int w, int h) {
            if (!shouldEmitCameraFrame()) return;

            std::vector<uint8_t> rgba((size_t)w * h * 4);
            for (int i = 0; i < w * h; i++) {
                rgba[i * 4 + 0] = bgr[i * 3 + 2];
                rgba[i * 4 + 1] = bgr[i * 3 + 1];
                rgba[i * 4 + 2] = bgr[i * 3 + 0];
                rgba[i * 4 + 3] = 255;
            }
            applyCameraAdjustments(rgba, w, h);
            {
                std::lock_guard<std::mutex> lk(cameraFrameMtx);
                cameraFrameRGBA = std::move(rgba);
                cameraFrameW = w; cameraFrameH = h;
            }
            cameraFrameReady = true;
            requestRepaint();
        });

        if (!cam.connected()) {
            logQ.push("[CAM] AmScope camera failed to open");
            cameraRunning = false;
            if (cameraBtnTxt) cameraBtnTxt->content = "START";
            return;
        }

        logQ.push("[CAM] Live (AmScope SDK): " + std::to_string(cam.width) + "x" + std::to_string(cam.height));

        while (cameraRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        logQ.push("[CAM] Stopped");
    }

    // -- Extended UVC controls (exposure / gain-ISO) -------------------------
    // Most UVC webcam drivers still implement the legacy DirectShow control
    // interfaces (IAMCameraControl / IAMVideoProcAmp) on the same media
    // source object Media Foundation hands back from ActivateObject(), even
    // though capture itself goes through IMFSourceReader -- so no separate
    // DirectShow graph is needed just to reach exposure/gain. Not every
    // camera/driver supports these; GetRange() failing (min==max stays 0) is
    // the "unsupported" signal surfaced in the UI. Never called for the
    // AmScope SDK backend (CameraBackend::AmScope), which has its own
    // internal auto-exposure and no equivalent control surface here.
    // Only acquires the interface pointers -- deliberately does NOT read the
    // exposure/gain range yet. GetRange() reflects whatever capture format is
    // active *at the moment it's called*, and on most UVC sensors the max
    // exposure is capped to roughly one frame period (a driver/firmware
    // limit, not something this code imposes) -- calling it here, before the
    // RGB32/frame-rate negotiation below, would report the device's default
    // (often high-frame-rate) mode's ceiling rather than the one actually in
    // use. See refreshCameraExtendedRange(), called once negotiation is done.
    static void acquireCameraExtendedCtrl(Interface* self, IMFMediaSource* pSource) {
        IAMCameraControl* pCamCtrl = nullptr;
        pSource->QueryInterface(IID_IAMCameraControl, (void**)&pCamCtrl);
        IAMVideoProcAmp* pProcAmp = nullptr;
        pSource->QueryInterface(IID_IAMVideoProcAmp, (void**)&pProcAmp);
        self->cameraCtrlIface.store(pCamCtrl);
        self->cameraProcAmpIface.store(pProcAmp);
    }

    static void refreshCameraExtendedRange(Interface* self) {
        auto* pCamCtrl = reinterpret_cast<IAMCameraControl*>(self->cameraCtrlIface.load());
        auto* pProcAmp = reinterpret_cast<IAMVideoProcAmp*>(self->cameraProcAmpIface.load());

        long lo = 0, hi = 0, step = 0, def = 0, flags = 0;
        if (pCamCtrl && SUCCEEDED(pCamCtrl->GetRange(CameraControl_Exposure, &lo, &hi, &step, &def, &flags))) {
            self->cameraExposureMin = (int)lo;
            self->cameraExposureMax = (int)hi;
            self->cameraExposureVal = (int)def;
            long curVal = 0, curFlags = 0;
            if (SUCCEEDED(pCamCtrl->Get(CameraControl_Exposure, &curVal, &curFlags))) {
                self->cameraExposureVal   = (int)curVal;
                self->cameraAutoExposure  = (curFlags & CameraControl_Flags_Auto) != 0;
            }
        } else {
            self->cameraExposureMin = self->cameraExposureMax = 0;
        }

        if (pProcAmp && SUCCEEDED(pProcAmp->GetRange(VideoProcAmp_Gain, &lo, &hi, &step, &def, &flags))) {
            self->cameraGainMin = (int)lo;
            self->cameraGainMax = (int)hi;
            self->cameraGainVal = (int)def;
            long curVal = 0, curFlags = 0;
            if (SUCCEEDED(pProcAmp->Get(VideoProcAmp_Gain, &curVal, &curFlags)))
                self->cameraGainVal = (int)curVal;
        } else {
            self->cameraGainMin = self->cameraGainMax = 0;
        }
    }

    static void releaseCameraExtendedCtrl(Interface* self) {
        if (auto* p = reinterpret_cast<IAMCameraControl*>(self->cameraCtrlIface.exchange(nullptr))) p->Release();
        if (auto* p = reinterpret_cast<IAMVideoProcAmp*>(self->cameraProcAmpIface.exchange(nullptr))) p->Release();
        self->cameraExposureMin = self->cameraExposureMax = 0;
        self->cameraGainMin     = self->cameraGainMax     = 0;
    }

    void Interface::setCameraExposure(int value) {
        auto* pCamCtrl = reinterpret_cast<IAMCameraControl*>(cameraCtrlIface.load());
        if (!pCamCtrl) return;
        pCamCtrl->Set(CameraControl_Exposure, value,
                      cameraAutoExposure.load() ? CameraControl_Flags_Auto : CameraControl_Flags_Manual);
    }

    void Interface::setCameraGain(int value) {
        auto* pProcAmp = reinterpret_cast<IAMVideoProcAmp*>(cameraProcAmpIface.load());
        if (!pProcAmp) return;
        pProcAmp->Set(VideoProcAmp_Gain, value, VideoProcAmp_Flags_Manual);
    }

    void Interface::setCameraAutoExposure(bool enabled) {
        auto* pCamCtrl = reinterpret_cast<IAMCameraControl*>(cameraCtrlIface.load());
        if (!pCamCtrl) return;
        pCamCtrl->Set(CameraControl_Exposure, (long)cameraExposureVal.load(),
                      enabled ? CameraControl_Flags_Auto : CameraControl_Flags_Manual);
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

        // Acquire the DirectShow-compatibility control interfaces for manual
        // exposure/gain before pSource is released below -- QueryInterface
        // AddRefs independently, so the returned pointers stay valid for the
        // life of this capture session regardless of pSource's own lifetime.
        // Range is queried later, once the capture format is actually set
        // (see refreshCameraExtendedRange()).
        acquireCameraExtendedCtrl(this, pSource);

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
            releaseCameraExtendedCtrl(this);
            cameraRunning = false;
            MFShutdown(); if (uninitCom) CoUninitialize(); return;
        }

        // Request RGB32 (BGRA, bottom-up) output so no YUV conversion needed.
        // Also request a lower frame rate when TARGET FPS is set to anything
        // but Unlimited -- on most UVC sensors the max manual/auto exposure
        // time is capped to roughly one frame period by the driver/firmware
        // (not by this app), so a high default capture rate is why the
        // exposure slider's ceiling can look artificially low (e.g. -3 =
        // 125ms at a high frame rate). Best-effort: if the device rejects
        // the combined subtype+frame-rate request, retry with just the
        // subtype so capture still works, just without a wider ceiling.
        IMFMediaType* pType = nullptr;
        MFCreateMediaType(&pType);
        pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        int reqFps = cameraTargetFps.load();
        if (reqFps > 0) MFSetAttributeRatio(pType, MF_MT_FRAME_RATE, (UINT32)reqFps, 1);
        hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                          nullptr, pType);
        pType->Release();

        if (FAILED(hr) && reqFps > 0) {
            logQ.push("[CAM] Camera rejected " + std::to_string(reqFps) + " fps, retrying at native rate");
            MFCreateMediaType(&pType);
            pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                              nullptr, pType);
            pType->Release();
        }

        if (FAILED(hr)) {
            logQ.push("[CAM] RGB32 not supported by this camera");
            pReader->Release();
            releaseCameraExtendedCtrl(this);
            cameraRunning = false;
            MFShutdown(); if (uninitCom) CoUninitialize(); return;
        }

        IMFMediaType* pActual = nullptr;
        pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pActual);
        UINT32 fw = 0, fh = 0;
        MFGetAttributeSize(pActual, MF_MT_FRAME_SIZE, &fw, &fh);
        UINT32 fpsNum = 0, fpsDen = 1;
        MFGetAttributeRatio(pActual, MF_MT_FRAME_RATE, &fpsNum, &fpsDen);
        pActual->Release();

        // Now that the real capture format (including frame rate) is locked
        // in, the exposure/gain range the driver reports reflects what's
        // actually achievable in this session.
        refreshCameraExtendedRange(this);

        logQ.push("[CAM] Live: " + std::to_string(fw) + "x" + std::to_string(fh) +
                   (fpsDen > 0 ? (" @ " + std::to_string(fpsNum / fpsDen) + "fps") : ""));
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

            if (shouldEmitCameraFrame()) {
                applyCameraAdjustments(rgba, w, h);
                {
                    std::lock_guard<std::mutex> lk(cameraFrameMtx);
                    cameraFrameRGBA = std::move(rgba);
                    cameraFrameW = w; cameraFrameH = h;
                }
                cameraFrameReady = true;
                requestRepaint();
            }
        }

        cameraReader.store(nullptr);
        pReader->Release();
        releaseCameraExtendedCtrl(this);
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

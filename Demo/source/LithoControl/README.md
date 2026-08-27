# LithoControl

The `LithoRev` GUI's application logic — `Interface : public Box` (declared
in `Interface.ixx`, implemented across `Interface.ixx` + `Interface.Build.cpp`
+ platform-specific `.win.cpp`/`.lnx.cpp` files). See the repo root
`CLAUDE.md` for Rev-framework-level facts (module system, style system,
element lifecycle); this file covers LithoControl's own features.

## Calibration Actions panel

Front-end for the [calib-dt](../../../../calib-dt) repo's CLI scripts,
invoked via `uv run <script>` (`runCalibDtSubprocess()`/
`startCalibDtAction()`), pointed at whatever folder **CALIB-DT FOLDER** is
set to. calib-dt is a separate, actively-changing scipy/opencv codebase —
this panel deliberately does not reimplement any of its feature-extraction
or optimization math in C++.

| Control | Function | Notes |
|---|---|---|
| **CALIB-DT FOLDER** | `findCalibDtRoot()` / `findCalibDtRootNearCwd()` | Auto-detects on startup: first tries a sibling of `dlpRoot` (the DLP-photolithography checkout), then a sibling *or* grandparent of the process's own working directory (covers both the desktop-deployed layout, `~/dev/calib-dt` next to `~/dev/Rev`, and running straight off a USB drive, where calib-dt sits at the drive root). Only overrides an already-persisted value if that value no longer resolves to a real calib-dt checkout (heals a stale path automatically) — a still-valid manual override is left alone. |
| **TEST CIRCLE DIAMETER (um)** | feeds `--circle-diameter-um` | Free-text field (µm, matching calib-dt's own units) with 0.15mm/0.07mm preset buttons. Must match whatever test circle is physically under the camera when `SAVE SCALE REF` is pressed. |
| **HOLDOUT FRACTION (0-1)** | feeds `--holdout-fraction` | Blank/0 = calib-dt's default (no held-out validation points) — the reported RMS is then measured on the same points the model was fit on, which a flexible distortion model can trivially interpolate to ~0 regardless of actual fit quality. A nonzero holdout is what actually shows generalization. |
| **CAPTURE DELAY (s)** | `countdownDelay()` | Default 3s, persisted. Waited (with a visible per-second countdown in the runner log) after projecting the grid/white flash but before actually capturing — time to step away from the microscope stage and let vibrations settle. Used by `SAVE SCALE REF`, `CAPTURE FRAMES`, and `RUN CALIBRATION`. |
| **PROJECT PATTERN** | `toggleCalibGrid()` | Toggles the calibration grid on LithoRev's *own* HDMI passthrough window (`generateCalibGrid()`/`composeHdmiFrame()`) — deliberately does **not** call calib-dt's own `--project-pattern`, which would open a second, uncoordinated fullscreen window fighting for the same monitor. |
| **CAPTURE FRAMES** | `captureFramesWithGrid()` → `capture-frames --camera-index <N> --frames 20 --out captures` | Raw camera grab, no fitting. Projects the grid itself first if it isn't already on (restores prior state after) — it doesn't otherwise illuminate anything, so pressing it with the projector idle just times out waiting for stable exposure. Stops the live preview first (see below), resumes after. |
| **RUN CALIBRATION** | `runCalibrationWithGrid()` | Projects the grid, then `run-calibration --camera --camera-index <N> --scale-reference <dir> --width <w> --height <h> --circle-diameter-um <um> [--holdout-fraction <f>] --out calibration_output`. Refuses to start if no scale-reference frame has been saved yet. `--width`/`--height` are the *actual* saved scale-reference frame's dimensions (`scaleReferenceFrameW/H`), forcing calib-dt's separate camera-open to match rather than hoping both sides negotiate the same resolution independently. |
| **ANALYZE SENSITIVITY** | `analyze-sensitivity --out sensitivity_output` | Physics-twin diagnostic report; unrelated to the empirical calibration path above. |
| Status banner | `setCalibStatus()` / `calibStatusLbl` | Idle/Running/Succeeded/Failed, color-coded, with a next-step hint — the one thing meant to be glanceable without scrolling the runner log. |

`calibDtCameraIndexArg()` supplies `--camera-index` to every calib-dt action
that opens the camera itself (`capture-frames`, `run-calibration --camera`),
reading whichever camera is currently selected in the live-preview dropdown
(`cameraDrop`) — calib-dt runs as a *separate process*, so without this it
silently uses its own default (camera 0), independent of whatever the GUI's
preview shows. Logs a warning instead of guessing if nothing's selected.

### SAVE FRAME / SAVE SCALE REF

Both live in the camera settings panel (`buildCameraSettingsPanel()`),
grabbing the current live-preview frame:

- **SAVE FRAME** (`saveCameraFrame()`) — general snapshot, `camera_captures/`.
- **SAVE SCALE REF** (`startCaptureScaleReferenceUnderWhite()` →
  `captureScaleReferenceUnderWhite()` → `saveScaleReferenceFrame()`) —
  the calibration slide's known-diameter circle isn't self-luminous; it
  reflects whatever's illuminating it. This flashes solid white on the
  projector first (`showSolid()` — takes display priority over the
  calibration grid, so the grid itself is left untouched underneath),
  waits ~500ms for a fresh camera frame to reflect it, saves, then restores
  whatever was showing before. Writes to `scale_reference/`, **clearing
  that folder first** — calib-dt's `measure_scale_reference()` runs
  detection on every file there and aborts if any fails, so this folder
  holds one current reference, not an accumulating archive.

Both write under `dataRootDir()`:

```
dataRootDir() = $LITHOREV_DATA_DIR if set, else std::filesystem::current_path()
```

`deploy-and-run.sh` (in the companion `I:\LithoRev-linux-build\` deploy kit)
exports `LITHOREV_DATA_DIR` pointing at the USB drive itself before
launching — the desktop shortcut doesn't go through that script, so it's
simply unset there and falls back to `~/dev/Rev` (the process's own cwd).
`cwd` alone can't distinguish "launched via the script off the drive" from
"launched via the desktop shortcut" — both end up with cwd `~/dev/Rev`
either way, since the script always deploys-then-runs from there — hence
the explicit env var instead of a cwd guess.

calib-dt's own `--out` folders (`calibration_output`, `captures`,
`sensitivity_output`) are written relative to `calibDtRoot`, not
`dataRootDir()` — `runCalibDtSubprocess()` mirrors them to
`$LITHOREV_DATA_DIR/<name>_<timestamp>` on success if that env var is set,
so they end up on the drive too instead of only ever existing on the
target machine's own disk.

### Camera/projector interaction

Most UVC cameras only allow one exclusive open at a time. `runCalibDtSubprocess(..., usesCamera=true)`
(set for `CAPTURE FRAMES` and `RUN CALIBRATION`) stops the live preview —
blocking until the capture thread has actually released the device — before
the calib-dt subprocess runs, and restarts it after. Skipping this let
LithoRev's own preview and calib-dt's separate camera-open fight over the
same device, wedging some drivers badly enough that only a physical
unplug/replug (or, for a built-in camera that can't be unplugged, a reboot)
would clear it.

## Camera capture (`CameraCapture.win.cpp` / `CameraCapture.lnx.cpp`)

- **Windows**: Media Foundation `IMFSourceReader`. **Linux**: V4L2 mmap'd
  buffer I/O + `poll()`.
- **Format**: requests MJPEG first (`V4L2_PIX_FMT_MJPEG`), decoded via
  `stb_image` (`decodeToRGBAFromMemory()`, `ImageDecode.ixx`) — falls back
  to RGB24 then YUYV only for cameras that don't offer MJPEG. MJPEG over a
  raw format because this class of USB2.0 camera only offers its higher
  resolutions that way at all (USB2.0 bandwidth can't sustain uncompressed
  1080p30), and MJPEG over H.264 specifically because it's all-intra — for
  equivalent visual quality that's a *higher* bitrate than H.264 (which
  compresses further via interframe prediction), and needs no persistent
  decoder/GOP state to implement.
- **Resolution**: `cameraResW`/`cameraResH` (UI presets) are requested as
  part of the same `VIDIOC_S_FMT` call as the pixel format — V4L2
  negotiates both together. `0`/`0` ("Native") leaves the driver's own
  default alone. The driver may still negotiate a different actual size
  than requested; `applyCameraAdjustments()` does a post-capture software
  resize to the target if so — but a local copy of the frame dimensions is
  passed into that call, never the capture loop's own persistent w/h
  (which must stay pinned to the real capture size, since it's reused to
  index into the mmap'd V4L2 buffer on every subsequent frame — passing the
  same variable in caused a real out-of-bounds read the first time the
  resize path ever actually ran).
- **`scanCameras()`** dedups by device *name* (`cap.card`), not
  label+path — some cameras expose more than one V4L2 node that
  legitimately reports `VIDEO_CAPTURE` in its own `device_caps` (not the
  capabilities-union false-positive `deviceCaps` already filters), so a
  path-based key can't catch it; only the first (lowest-index) node per
  name is kept.

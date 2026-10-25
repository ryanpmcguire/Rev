# Camera layers

`Camera.ixx` exports `Rev.Media.Camera` and the `Rev::Media::Camera` API.
This is distinct from the display widget, `Rev::Element::Camera`.
It explicitly allocates and owns a `NativeCamera*`, subscribes to native frames
through a callback, and
handles latest-frame synchronization, monotonic frame revisions, control caching,
range/step normalization, reset policy, and confirmed control-change events.
Buffer management, image adjustments, and other portable processing belong in
`Rev::Media::Camera`; native backends should remain limited to the OS boundary.
Camera structs do not divide their representation with `public`/`private` access
sections; their source layout and responsibility boundaries carry that structure.

Frames use two stable buffers. `NativeCamera` owns and fills the write buffer.
When a frame is complete, `Media::Camera` swaps that pointer with its published
buffer, dispatches `Frame::Event` synchronously, and returns the previous published
buffer to the native producer. Subscribers must finish reading or swap away from
the previous pointer before their frame callback returns. The display element does
this under its frame mutex and holds the same mutex through the OpenGL upload.
After both buffers have seen the current dimensions, ordinary frames allocate no
pixel storage and copy no pixel vector at the native/media boundary.

`Camera.win.ixx`, `Camera.lnx.ixx`, and `Camera.mac.ixx` provide the platform-selected
`Rev.Media.NativeCamera` module. Native implementations own device enumeration,
open/close, driver errors, pixel conversion and timestamps, and driver control I/O.
`close()` must stop and detach frame callbacks before returning. The callback may
run on a capture thread; it publishes pixels only and does not invoke UI listeners.
Control/lifecycle calls and their listeners remain on the consumer thread.
Linux and macOS remain explicitly unsupported capture backends.

`Camera.Types.ixx` supplies the raw frame/device/setting contract without a
circular dependency. `Rev::Media::Camera` exposes that vocabulary as `DeviceInfo`,
`Frame` and `Setting`. Native backends
use the underlying `CameraTypes` declarations directly.

Run `python Tests/verify.py <clang++-path>` from this directory for hardware-free
checks of frame delivery/restarts, controls, notifications, and native stub contracts.

# Camera frame clocks

`Camera::Frame::timestamp.timestamp100ns` is the Media Foundation stream presentation time.
Its epoch is stream-relative. Do not subtract it from a host clock to report latency.

Windows frames additionally carry `timestamp.receivedAtMs` and `timestamp.capturedAtMs`, both
in host `std::chrono::steady_clock` milliseconds (the same domain as
`Rev::GlobalTime::now`). `timestamp.receivedAtMs` records entry to the sample callback, before
pixel conversion/copying. `timestamp.capturedAtMs` maps the optional
`MFSampleExtension_DeviceTimestamp` attribute from the Media Foundation/QPC clock
using a contemporaneous `MFGetSystemTime()` reading. The metadata is published
under the same frame mutex as its pixels. A consumer explicitly requests an owned
snapshot by calling `camera.frame->copy()`.

Frame publication and frame-change dispatch happen on the native capture thread. The
camera element responds by swapping its published frame pointer; it does not copy the
pixel buffer on the main thread. `onFrame` listeners receive the frame event followed
by the published frame pointer. A consumer that needs an independently owned CPU
snapshot can call `Frame::copy` from whichever thread owns that work. The copy checks
that the frame is still published before reading it, so a buffer returned to the native
writer cannot be copied concurrently.

Each camera control owns its change dispatcher and exposes `set`, `reset`, `onChange`,
and `unsubscribe`. Control notifications therefore stay attached to the specific
control rather than passing through a camera-wide dispatcher.

`Camera::Info` is the readable camera telemetry: `info.open`, `info.error`, and
`info.controls`. A control mutates its single state in `info.controls`, then publishes
only its `Setting` identifier. Subscribers use that identifier to read the control they
care about; no full settings struct is copied through the event stream.

Missing timestamps, future timestamps and driver timestamps more than five seconds
old produce `timestamp.capturedAtMs == 0`. Availability must be checked per frame.
The Linux/macOS capture stubs expose the same fields but do not produce frames.

The capture timestamp is driver-derived; it is not proof of exposure start or
midpoint, and does not measure monitor scanout or glass-to-glass latency. Consumers
should validate advancing timestamps and reset timing state across stream restarts.
Delivery age includes callback processing and consumer waiting time:
`hostNowMs - timestamp.receivedAtMs`. Where capture time exists, driver-to-callback delay is
`timestamp.receivedAtMs - timestamp.capturedAtMs` and current frame age is
`hostNowMs - timestamp.capturedAtMs`.

See Microsoft's [device timestamp contract](https://learn.microsoft.com/en-us/windows/win32/medfound/mfsampleextension-devicetimestamp).

# Camera frame clocks

`CameraFrame::timestamp100ns` is the Media Foundation stream presentation time.
Its epoch is stream-relative. Do not subtract it from a host clock to report latency.

Windows frames additionally carry `receivedAtMs` and optional `capturedAtMs`, both
in host `std::chrono::steady_clock` milliseconds (the same domain as
`Rev::GlobalTime::now`). `receivedAtMs` records entry to the sample callback, before
pixel conversion/copying. `capturedAtMs` maps the optional
`MFSampleExtension_DeviceTimestamp` attribute from the Media Foundation/QPC clock
using a contemporaneous `MFGetSystemTime()` reading. The metadata is published
under the same frame mutex as its pixels and is copied with `takeFrame`.

Missing timestamps, future timestamps and driver timestamps more than five seconds
old produce an empty `capturedAtMs`. Availability must be checked per frame.
The Linux/macOS capture stubs expose the same fields but do not produce frames.

The capture timestamp is driver-derived; it is not proof of exposure start or
midpoint, and does not measure monitor scanout or glass-to-glass latency. Consumers
should validate advancing timestamps and reset timing state across stream restarts.
Delivery age includes callback processing and consumer waiting time:
`hostNowMs - receivedAtMs`. Where capture time exists, driver-to-callback delay is
`receivedAtMs - *capturedAtMs` and current frame age is
`hostNowMs - *capturedAtMs`.

See Microsoft's [device timestamp contract](https://learn.microsoft.com/en-us/windows/win32/medfound/mfsampleextension-devicetimestamp).

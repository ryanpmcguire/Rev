# Rev USB proxy

`Rev::UsbProxy` manages the shared JSON/WebSocket discovery session and starts
an idempotent external proxy service through its manager script.

`Rev::SerialClient` remains a direct Win32 COM client by default. To use a
proxy byte-stream endpoint, configure it before connecting:

```cpp
Rev::SerialClient serial;
serial.setProxy({
    .enabled = true,
    .serviceScript = "../Tools/serial_proxy_service.py",
    .python = "python",
    .host = "127.0.0.1",
    .dataPort = 8765,
    .logicalDevice = "motion",
});
serial.connect("COM14", 250000);
```

In proxy mode `connect()` first runs the manager's idempotent `start` command.
The manager checks for a healthy service and starts a detached service process
only when required. `SerialClient` never opens the COM handle in this mode;
bytes flow through the configured proxy data endpoint instead.

Proxy-enabled serial clients share a process-static weak service
representative. The first client promotes or creates it and verifies the
external service; subsequent clients retain the same representative. Each
disconnect releases one reference and the last release destroys the in-process
representative. It deliberately does not stop the external service, because
clients in other processes may still depend on it.

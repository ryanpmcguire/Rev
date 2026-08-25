// Standalone projector-window process. Spawned by LithoRev's
// Interface::openHdmiWindow() (see Demo/source/LithoControl/HdmiWindow.lnx.cpp)
// via posix_spawn() -- deliberately NOT fork(), and deliberately a fully
// separate executable rather than a re-exec of LithoRev itself, so this
// process shares nothing with LithoRev's own OpenGL/X11 state. Renders with
// raylib instead of Rev's own window code, per the decision to stop
// depending on Rev's Linux GL/X11 path for this specific feature.
//
// Usage: LithoRevProjector <ipc-fd> <x> <y> <w> <h>
// Receives a continuous stream of fixed-size 640x360 BGRA frames on <ipc-fd>
// (no framing needed -- every message is exactly this many bytes, matching
// Interface::composeHdmiFrame's fixed contract) and displays them scaled to
// the window's actual size. The socket closing (parent shutdown, or this
// process exiting) is the only lifecycle signal needed in either direction.

#include "raylib.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 6) return 1;
    int fd = std::atoi(argv[1]);
    int x  = std::atoi(argv[2]);
    int y  = std::atoi(argv[3]);
    int w  = std::atoi(argv[4]);
    int h  = std::atoi(argv[5]);
    if (w <= 0) w = 640;
    if (h <= 0) h = 360;

    // Non-blocking so a quiet socket never stalls raylib's own frame loop.
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    SetConfigFlags(FLAG_WINDOW_UNDECORATED);
    InitWindow(w, h, "LithoRev Projector");
    SetWindowPosition(x, y);
    SetTargetFPS(60);

    Image blank = GenImageColor(640, 360, BLACK);
    Texture2D tex = LoadTextureFromImage(blank);
    UnloadImage(blank);

    const size_t frameBytes = (size_t)640 * 360 * 4;
    std::vector<uint8_t> frameBuf(frameBytes);
    size_t frameFill = 0;
    std::vector<uint8_t> rgbaBuf(frameBytes);

    bool connectionLost = false;

    while (!WindowShouldClose() && !connectionLost) {
        ssize_t n;
        while ((n = recv(fd, frameBuf.data() + frameFill, frameBytes - frameFill, 0)) > 0) {
            frameFill += (size_t)n;
            if (frameFill == frameBytes) {
                // Source is BGRA-in-memory (Interface::composeHdmiFrame);
                // raylib textures are RGBA-in-memory -- swap R/B per pixel.
                for (size_t i = 0; i < frameBytes; i += 4) {
                    rgbaBuf[i + 0] = frameBuf[i + 2];
                    rgbaBuf[i + 1] = frameBuf[i + 1];
                    rgbaBuf[i + 2] = frameBuf[i + 0];
                    rgbaBuf[i + 3] = frameBuf[i + 3];
                }
                UpdateTexture(tex, rgbaBuf.data());
                frameFill = 0;
            }
        }
        // n == 0 is a real EOF (parent closed us down deliberately, or the
        // whole app is gone) -- exit. n < 0 with no data ready (EAGAIN) is
        // normal and just means "nothing new this tick," keep drawing the
        // last frame we have.
        if (n == 0) { connectionLost = true; break; }

        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(tex,
            Rectangle{ 0, 0, (float)tex.width, (float)tex.height },
            Rectangle{ 0, 0, (float)w, (float)h },
            Vector2{ 0, 0 }, 0.0f, WHITE);
        EndDrawing();
    }

    UnloadTexture(tex);
    CloseWindow();
    close(fd);
    return 0;
}

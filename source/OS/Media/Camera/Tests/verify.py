"""Hardware-free camera policy and native stub checks. Pass a C++ compiler path."""
from pathlib import Path
import subprocess
import sys
import tempfile

CAMERA = Path(__file__).resolve().parents[1]
SOURCE = CAMERA.parents[2]


def plain(path):
    """Compile the production bodies without requiring a C++ module build."""
    return "\n".join(
        line.replace("export namespace", "namespace")
        for line in path.read_text().splitlines()
        if line != "module;"
        and not line.startswith(("export module", "import ", "export import"))
    )


FAKE = r"""
#include <functional>
namespace Rev::Media {
struct NativeCamera {
    inline static NativeCamera* active = nullptr;
    CameraTypes::Frame* writeFrame = nullptr;
    std::function<CameraTypes::Frame*(CameraTypes::Frame*)> frame;
    bool opened = false;
    CameraTypes::Setting info{
        .name = "Gain", .control = CameraTypes::Setting::Type::Gain,
        .mode = CameraTypes::Setting::Mode::Manual,
        .min = 0, .max = 10, .val = 4, .def = 4, .step = 2,
        .supportsManual = true
    };
    explicit NativeCamera(std::function<CameraTypes::Frame*(CameraTypes::Frame*)> f) {
        frame = f; writeFrame = new CameraTypes::Frame; active = this;
    }
    ~NativeCamera() { delete writeFrame; writeFrame = nullptr; }
    static std::vector<CameraTypes::DeviceInfo> devices() { return {}; }
    bool open(size_t) { opened = true; return true; }
    void close() { opened = false; }
    bool isOpen() const { return opened; }
    std::string error() const { return {}; }
    std::vector<CameraTypes::Setting> readControls() { return {info}; }
    bool writeControl(CameraTypes::Setting::Type, long value, CameraTypes::Setting::Mode mode,
                      CameraTypes::Setting& actual) {
        info.val = value; info.mode = mode; actual = info; return true;
    }
    void emit(unsigned char pixel) {
        writeFrame->size = {1, 1, 4};
        writeFrame->data.resize(4);
        writeFrame->data[0] = pixel;
        writeFrame->data[1] = 0;
        writeFrame->data[2] = 0;
        writeFrame->data[3] = 255;
        writeFrame = frame(writeFrame);
    }
};
}
"""

CHECKS = r"""
#include <cassert>
int main() {
    using namespace Rev::Media;
    Camera camera;
    assert(camera.nativeCamera != nullptr);
    assert(camera.open());
    assert(camera.info.open && camera.info.controls.size() == 1);
    CameraTypes::Frame f;
    assert(f.timestamp.capturedAtMs == 0);
    assert(camera.frame->copy().data.empty());
    Camera::Frame* published = nullptr;
    uint64_t publishedRevision = 0;
    int frameEvents = 0;
    camera.onFrame(&frameEvents, [&](Camera::Frame::Event& event, Camera::Frame* frame) {
        published = frame;
        publishedRevision = event.revision;
        ++frameEvents;
    });
    NativeCamera::active->emit(1);
    Camera::Frame* firstBuffer = published;
    NativeCamera::active->emit(2);
    Camera::Frame* secondBuffer = published;
    assert(firstBuffer != secondBuffer && frameEvents == 2 && publishedRevision == 2);
    f = camera.frame->copy();
    assert(f.revision == 2 && f.data[0] == 2);
    auto previous = f.revision;
    camera.close();
    assert(camera.open()); NativeCamera::active->emit(3);
    assert(published == firstBuffer);
    f = camera.frame->copy();
    assert(f.revision > previous && f.data[0] == 3);
    int calls = 0;
    auto gain = camera.control(CameraTypes::Setting::Type::Gain);
    gain->onChange(&calls, [&](CameraTypes::Setting::Type& changed) {
        ++calls; assert(changed == CameraTypes::Setting::Type::Gain);
        assert(camera.control(changed)->val == camera.info.controls[0].val);
    });
    assert(gain->set(7));
    assert(camera.control(CameraTypes::Setting::Type::Gain)->val == 8 && calls == 1);
    assert(camera.info.controls[0].val == 8);
    assert(gain->set(50));
    assert(camera.control(CameraTypes::Setting::Type::Gain)->val == 10);
    assert(!gain->set(4, CameraTypes::Setting::Mode::Automatic));
    assert(camera.control(CameraTypes::Setting::Type::Focus) == nullptr);
    assert(gain->reset() && gain->val == 4);
    gain->unsubscribe(&calls); auto before = calls;
    assert(gain->set(2) && calls == before);
    camera.unsubscribeFrames(&frameEvents);
    camera.close(); assert(!camera.info.open && camera.info.controls.empty());
}
"""


def verify(compiler):
    prefix = plain(CAMERA / "Camera.Types.ixx") + "\n" + plain(SOURCE / "Core/Dispatcher.ixx")
    common = plain(CAMERA / "Camera.ixx")
    with tempfile.TemporaryDirectory(prefix="rev-camera-") as directory:
        cpp = Path(directory) / "test.cpp"
        exe = Path(directory) / "test.exe"
        for backend in ("fake", "lnx", "mac"):
            native = FAKE if backend == "fake" else plain(CAMERA / f"Camera.{backend}.ixx")
            checks = CHECKS if backend == "fake" else """
#include <cassert>
int main() {
    Rev::Media::Camera c;
    assert(c.nativeCamera != nullptr);
    assert(!c.open() && !c.info.open && !c.info.error.empty());
    assert(c.info.controls.empty());
}
"""
            cpp.write_text("\n".join((prefix, native, common, checks)))
            subprocess.run([compiler, "-std=c++23", str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
            print(f"Camera {backend} checks passed")


if __name__ == "__main__":
    verify(sys.argv[1])

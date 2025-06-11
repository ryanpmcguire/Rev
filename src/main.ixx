
import Rev.Window;

using namespace Rev;

Window* window = nullptr;

int main() {

    window = new Window();
    window->run();

    /*for (int i = 0; i < 1000; i++) {
        Box* box = new Box(window);
        box->style = {
            .size = { .width = Px(640/2), .height = Px(480/2) }
        };
    }*/

    return 0;
}
// stb_image, included once per translation unit.
//
// stb's implementation section has no include guard of its own (only its
// declarations do). Clever moves a `#define X_IMPLEMENTATION` + include block
// to the top of the module's .cpp and leaves the include in the .hpp, which
// that .cpp then includes, so the implementation would be compiled twice.
// This guard makes the second include a no-op. Used only by Elements/Image.ixx.
#ifndef REV_STB_IMAGE_H
#define REV_STB_IMAGE_H
#include <stb/stb_image.h>
#endif

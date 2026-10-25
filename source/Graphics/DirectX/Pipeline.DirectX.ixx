module;
#include "Helpers/Pipeline.hpp"
export module Rev.Graphics.Pipeline;
import Rev.Core.Resource;
export import Rev.Graphics.Shader;
export namespace Rev::Graphics {
    // Match the existing graphics backends' Core name visibility. Primitives
    // currently rely on this for unqualified Shared, Font, and other Core types.
    using namespace Rev::Core;
    using Pipeline = DirectX::Pipeline;
}

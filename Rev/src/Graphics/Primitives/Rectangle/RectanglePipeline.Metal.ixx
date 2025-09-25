module;

export module Rev.Graphics.Pipelines.RectanglePipeline;

import Rev.Graphics.Canvas;
import Rev.Graphics.Shader;
import Rev.Graphics.Pipeline;

import Resources.Shaders.Metal.Rectangle.Rectangle_metal;

export namespace Rev {

    struct RectanglePipeline : public Pipeline {

        RectanglePipeline(void* context) : Pipeline(context) {

            // On mac, the whole shader is one file
            shader = new Shader(context, Rectangle_metal, Shader::Stage::Universal);
            
            this->init(context);
        }
    };
};
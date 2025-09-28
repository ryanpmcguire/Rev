module;

export module Rev.Graphics.Pipelines.TextPipeline;

import Rev.Graphics.Canvas;
import Rev.Graphics.Shader;
import Rev.Graphics.Pipeline;

import Resources.Shaders.Metal.Text.Text_metal;

export namespace Rev {

    struct TextPipeline : public Pipeline {

        TextPipeline(void* context) : Pipeline(context) {

            // On mac, the whole shader is one file
            shader = new Shader(context, Text_metal, Shader::Stage::Universal);
            
            this->init(context, 4);
        }
    };
};
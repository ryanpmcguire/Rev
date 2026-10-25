// Render identical fixtures through the original GLSL and the actual D3D12 ports.
// No application windows are shown and no Imager hardware is accessed.
#include <glew/glew.h>
#include "../Helpers/Canvas.hpp"
#include <d3d12sdklayers.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace Rev::Graphics::DirectX;
constexpr unsigned W = 128, H = 128;
using Pixels = std::vector<unsigned char>;

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static std::string load(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(bool(stream), "Missing shader file");
    return {std::istreambuf_iterator<char>(stream), {}};
}
static ResourceView resource(const std::string& s) {
    return {reinterpret_cast<const unsigned char*>(s.data()), s.size()};
}
struct Fixture {
    std::string path, label, definitions;
    std::vector<size_t> attributes;
    std::vector<float> vertices, points;
    std::array<std::vector<float>, 4> uniforms;
    unsigned count = 4, instances = 1, divisor = 0;
    bool fan = true, texture = false, probeDepth = false;
    float scale = 1;
};
static const std::vector<float> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
static const std::vector<float> projection{2.f/W,0,0,0, 0,-2.f/H,0,0, 0,0,-1,0, -1,1,0,1};
static unsigned char texels[] = {240,40,80,210, 25,220,50,128, 30,75,230,245, 210,165,35,90};

struct OpenGL {
    HWND window;
    HDC dc;
    HGLRC context;
    GLuint fbo, color, depth;
    OpenGL() {
        window = CreateWindowExW(0,L"STATIC",L"Rev shader parity",WS_OVERLAPPEDWINDOW,0,0,W,H,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window != nullptr, "Create hidden GL window"); dc = GetDC(window);
        PIXELFORMATDESCRIPTOR pfd{}; pfd.nSize=sizeof(pfd); pfd.nVersion=1;
        pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        pfd.iPixelType=PFD_TYPE_RGBA; pfd.cColorBits=32; pfd.cDepthBits=24; pfd.cStencilBits=8;
        int format=ChoosePixelFormat(dc,&pfd);
        require(format && SetPixelFormat(dc,format,&pfd), "Set GL pixel format");
        context=wglCreateContext(dc); require(context && wglMakeCurrent(dc,context), "Create GL context");
        require(glewInit()==GLEW_OK && GLEW_VERSION_4_3, "OpenGL 4.3 required for original shaders");
        while(glGetError()!=GL_NO_ERROR) {}
        std::cout << "OpenGL: " << glGetString(GL_RENDERER) << '\n';
        glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glGenTextures(1,&color); glBindTexture(GL_TEXTURE_2D,color);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
        glGenRenderbuffers(1,&depth); glBindRenderbuffer(GL_RENDERBUFFER,depth);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,W,H);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,depth);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"GL framebuffer incomplete");
    }
    ~OpenGL() { wglMakeCurrent(nullptr,nullptr); wglDeleteContext(context); ReleaseDC(window,dc); DestroyWindow(window); }
    static GLuint shader(GLenum type, std::string source, const std::string& definitions) {
        auto pos=source.find("DEFINITIONS");
        if(pos!=std::string::npos) source.replace(pos,11,definitions);
        GLuint id=glCreateShader(type); const char* text=source.c_str(); glShaderSource(id,1,&text,nullptr); glCompileShader(id);
        GLint ok=0; glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
        if(!ok) { char log[8192]; glGetShaderInfoLog(id,sizeof(log),nullptr,log); throw std::runtime_error(log); }
        return id;
    }
    Pixels render(const std::filesystem::path& root, const Fixture& f) {
        GLuint vs=shader(GL_VERTEX_SHADER,load(root/(f.path+".vert")),f.definitions);
        GLuint fs=shader(GL_FRAGMENT_SHADER,load(root/(f.path+".frag")),f.definitions);
        GLuint program=glCreateProgram(); glAttachShader(program,vs); glAttachShader(program,fs); glLinkProgram(program);
        GLint ok=0; glGetProgramiv(program,GL_LINK_STATUS,&ok);
        if(!ok) { char log[8192]; glGetProgramInfoLog(program,sizeof(log),nullptr,log); throw std::runtime_error(log); }
        glUseProgram(program); glBindFramebuffer(GL_FRAMEBUFFER,fbo); glViewport(0,0,W,H);
        glDisable(GL_DITHER); glDisable(GL_CULL_FACE); glDisable(GL_FRAMEBUFFER_SRGB);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
        glClearColor(1,1,1,1); glClearDepth(1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
        GLuint vao, vbo, ubos[4], image=0, tbo=0, pointTexture=0;
        glGenVertexArrays(1,&vao); glBindVertexArray(vao);
        glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,f.vertices.size()*4,f.vertices.data(),GL_STATIC_DRAW);
        size_t stride=0, offset=0; for(auto n:f.attributes) stride+=n*4;
        for(unsigned i=0;i<f.attributes.size();++i) {
            glEnableVertexAttribArray(i); glVertexAttribPointer(i,GLint(f.attributes[i]),GL_FLOAT,GL_FALSE,GLsizei(stride),reinterpret_cast<void*>(offset));
            glVertexAttribDivisor(i,f.divisor); offset+=f.attributes[i]*4;
        }
        glGenBuffers(4,ubos);
        for(unsigned i=0;i<4;++i) if(!f.uniforms[i].empty()) {
            glBindBuffer(GL_UNIFORM_BUFFER,ubos[i]); glBufferData(GL_UNIFORM_BUFFER,f.uniforms[i].size()*4,f.uniforms[i].data(),GL_STATIC_DRAW);
            glBindBufferBase(GL_UNIFORM_BUFFER,i,ubos[i]);
        }
        glActiveTexture(GL_TEXTURE0);
        if(f.texture) {
            glGenTextures(1,&image); glBindTexture(GL_TEXTURE_2D,image);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,texels);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }
        if(!f.points.empty()) {
            glGenBuffers(1,&tbo); glBindBuffer(GL_TEXTURE_BUFFER,tbo); glBufferData(GL_TEXTURE_BUFFER,f.points.size()*4,f.points.data(),GL_STATIC_DRAW);
            glGenTextures(1,&pointTexture); glBindTexture(GL_TEXTURE_BUFFER,pointTexture); glTexBuffer(GL_TEXTURE_BUFFER,GL_RG32F,tbo);
        }
        glDrawArraysInstanced(f.fan?GL_TRIANGLE_FAN:GL_TRIANGLES,0,f.count,f.instances);
        // STENCIL outputs transparent black. A later draw behind its depth
        // reveals the discard mask without changing the shader under test.
        if(!f.definitions.empty() || f.probeDepth) {
            GLuint revealVS=shader(GL_VERTEX_SHADER,"#version 430 core\nvoid main(){vec2 p=vec2((gl_VertexID==1)?3:-1,(gl_VertexID==2)?3:-1);gl_Position=vec4(p,0.5,1);}","");
            GLuint revealFS=shader(GL_FRAGMENT_SHADER,"#version 430 core\nout vec4 c;void main(){c=vec4(1,0,0,1);}","");
            GLuint reveal=glCreateProgram(); glAttachShader(reveal,revealVS); glAttachShader(reveal,revealFS); glLinkProgram(reveal);
            glUseProgram(reveal); glDrawArrays(GL_TRIANGLES,0,3);
            glDeleteProgram(reveal); glDeleteShader(revealVS); glDeleteShader(revealFS);
        }
        Pixels pixels(W*H*4); glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        require(glGetError()==GL_NO_ERROR,"GL fixture failed");
        for(unsigned y=0;y<H/2;++y) for(unsigned x=0;x<W*4;++x) std::swap(pixels[y*W*4+x],pixels[(H-1-y)*W*4+x]);
        glDeleteTextures(1,&image); glDeleteTextures(1,&pointTexture); glDeleteBuffers(1,&tbo);
        glDeleteBuffers(4,ubos); glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao);
        glDeleteProgram(program); glDeleteShader(vs); glDeleteShader(fs);
        return pixels;
    }
};

static Pixels directX(Canvas& canvas, const std::filesystem::path& root, const Fixture& f) {
    auto vs=load(root/(f.path+".vert.hlsl")), fs=load(root/(f.path+".frag.hlsl"));
    Pipeline pipeline(canvas.context,{.attribs=f.attributes,.definitions=f.definitions,.directXVert=resource(vs),.directXFrag=resource(fs)});
    size_t stride=0; for(auto n:f.attributes) stride+=n;
    VertexBuffer vertices(canvas.context,{.num=stride?f.vertices.size()/stride:0,.divisor=f.divisor,.attribs=f.attributes});
    if(!f.vertices.empty()) vertices.Buffer::set(f.vertices.data());
    std::array<std::unique_ptr<UniformBuffer>,4> uniforms;
    for(unsigned i=0;i<4;++i) if(!f.uniforms[i].empty()) {
        uniforms[i]=std::make_unique<UniformBuffer>(canvas.context,f.uniforms[i].size()*4); uniforms[i]->set(f.uniforms[i].data());
    }
    Texture image(canvas.context,{texels,2,2,4});
    TextureBuffer points(canvas.context); if(!f.points.empty()) points.set(f.points.data(),f.points.size()/2);
    canvas.beginFrame(W,H,f.scale); canvas.depthTest(true); canvas.depthWrite(true);
    pipeline.bind(); vertices.bind();
    // Exercise Canvas's own GL-compatible projection rather than replacing b0.
    for(unsigned i=1;i<4;++i) if(uniforms[i]) uniforms[i]->bind(i);
    if(f.texture) image.bind(); if(!f.points.empty()) points.bind();
    canvas.drawArraysInstanced(f.fan?Pipeline::TriangleFan:Pipeline::TriangleList,0,f.count,f.instances);
    std::unique_ptr<Pipeline> reveal;
    if(!f.definitions.empty() || f.probeDepth) {
        std::string revealVS="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3:-1,id==2?3:-1,0.75,1);}";
        std::string revealFS="float4 main():SV_Target{return float4(1,0,0,1);}";
        reveal=std::make_unique<Pipeline>(canvas.context,Pipeline::Params{.directXVert=resource(revealVS),.directXFrag=resource(revealFS)});
        reveal->bind(); canvas.drawArrays(Pipeline::TriangleList,0,3);
    }
    auto& ctx=*canvas.context; auto* target=canvas.frameBuffer->texture;
    auto desc=target->id->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 total;
    ctx.device->gpu->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&total);
    auto buffer=ctx.device->create(bufferDesc(total),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    ctx.transition(target->id.Get(),target->state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource=buffer.Get(); destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint=footprint;
    D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource=target->id.Get(); source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ctx.list->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
    ctx.transition(target->id.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,target->state);
    canvas.endFrame(); ctx.waitIdle();
    unsigned char* data=nullptr; check(buffer->Map(0,nullptr,reinterpret_cast<void**>(&data)),"Map parity readback");
    Pixels pixels(W*H*4);
    for(unsigned y=0;y<H;++y) std::memcpy(pixels.data()+y*W*4,data+footprint.Offset+y*footprint.Footprint.RowPitch,W*4);
    D3D12_RANGE empty{}; buffer->Unmap(0,&empty); return pixels;
}

static std::vector<Fixture> fixtures() {
    std::vector<Fixture> all;
    auto add=[&](std::string path,std::string label) -> Fixture& {
        all.push_back({}); auto& f=all.back(); f.path=path; f.label=label; f.uniforms[0]=projection; return f;
    };
    auto& rect=add("Rectangle/Shaders/Rectangle","rectangle: asymmetric radii, borders, shadow");
    rect.uniforms[1]={20,25,80,66, .2f,.6f,.8f,.75f, 8,17,11,5, 3,7,5,2,
        1,0,0,.8f, 0,1,0,.7f, 0,0,1,.9f, 1,1,0,.6f, 4,6,3,7, .1f,.15f,.2f,.6f, .8f,0,0,0};
    auto stencil=rect; stencil.definitions="#define STENCIL"; stencil.label="rectangle: stencil discard"; all.push_back(stencil);
    auto& text=add("Text/Shaders/Text","text: glyph layout, alpha, explicit depth");
    text.attributes={4}; text.vertices={20,24,0,0}; text.divisor=1; text.count=6; text.fan=false; text.texture=true;
    text.uniforms[1]={.3f,.6f,.1f,.8f, 0,0,.3f,.75f}; text.uniforms[2].resize(128*6*4);
    const float glyph[]={0,0,0,0, 72,0,1,0, 72,68,1,1, 0,0,0,0, 72,68,1,1, 0,68,0,1};
    std::copy(std::begin(glyph),std::end(glyph),text.uniforms[2].begin());
    for(auto name:{"Lines","Triangles"}) {
        auto& f=add(std::string(name)+"/Shaders/"+name,name); f.count=3; f.fan=false;
        f.attributes=std::string(name)=="Lines"?std::vector<size_t>{2,4,1}:std::vector<size_t>{2,4};
        f.uniforms[1]={.6f,.2f,.8f,1, .25f,.7f,0,0};
        for(auto v:std::array<std::array<float,7>,3>{{{17,19,1,0,0,1,-1},{110,30,0,1,0,1,1},{48,109,0,0,0,0,0}}})
            f.vertices.insert(f.vertices.end(),v.begin(),v.begin()+(std::string(name)=="Lines"?7:6));
    }
    auto& fast=add("FastLines/Shaders/FastLines","fast lines: joins, caps, affine transform");
    fast.count=15; fast.instances=3; fast.fan=false; fast.points={8,12, 44,64, 73,18, 96,83};
    fast.uniforms[1]={.3f,.7f,.2f,.9f, 9,1.5f,4,.8f}; fast.uniforms[2]={1.05f,.95f,6,9};
    for(auto name:{"Mesh3d","Lines3d","FastLines3d"}) {
        bool mesh=std::string(name)=="Mesh3d", fast3=std::string(name)=="FastLines3d";
        auto& f=add(std::string(mesh?"Mesh3d":"Lines3d")+"/Shaders/"+name,name); f.fan=false;
        f.uniforms[1]={.2f,.7f,.6f,1, .4f,.8f,0,0};
        f.uniforms[2]=identity; f.uniforms[2][11]=.3f; // Perspective w depends on z.
        f.uniforms[2].insert(f.uniforms[2].end(),{.2f,.4f,1,0, 0,0,2,1, -.5f,.3f,.6f,0});
        f.uniforms[3]=identity; f.uniforms[3][0]=.8f; f.uniforms[3][1]=.2f; f.uniforms[3][4]=-.2f; f.uniforms[3][5]=.8f;
        f.uniforms[3].insert(f.uniforms[3].end(),identity.begin(),identity.end()); f.uniforms[3][28]=.1f;
        if(fast3) {
            f.uniforms[1]={.2f,.7f,.6f,1, 8,1.5f,4,.85f, float(W),float(H),1,0};
            f.points={-.8f,-.6f,-.3f,0, 1,0,0,1, -.3f,.65f,.4f,0, 0,1,0,.8f,
                       .35f,-.35f,0,0, 0,0,1,1, .7f,.6f,.7f,0, 1,1,0,.7f};
            f.count=15; f.instances=3;
        } else {
            f.attributes={3,4,3,1,1}; f.count=3;
            f.vertices={-.8f,-.6f,-.3f, 1,0,0,1, .2f,.3f,1,0,0,
                        .75f,-.4f,.5f, 0,1,0,1, -.2f,.4f,1,0,0,
                        -.1f,.8f,.1f, 0,0,0,0, .1f,-.2f,1,0,0};
        }
    }
    for(bool tint:{false,true}) {
        auto& f=add("Svg/Shaders/Svg",tint?"svg: clockwise rotation, tint":"svg: clockwise rotation, texture"); f.texture=true;
        f.uniforms[1]={25,30,75,60, 0,0,0,0, .37f,.7f,0,0};
        if(tint) { f.uniforms[1][4]=.2f; f.uniforms[1][5]=.7f; f.uniforms[1][6]=.4f; f.uniforms[1][7]=.8f; }
    }
    for(unsigned format=4;format<=7;++format) {
        auto& f=add("Video/Shaders/Video","video: pixel format "+std::to_string(format)); f.texture=true;
        f.uniforms[1]={15,23,95,77, .1f,.2f,.8f,.7f, .75f,.3f,1.2f,.6f,
            1.1f,.9f,1.3f,1, .05f,.1f,.03f,0, .95f,.9f,.85f,1, .04f,.02f,.06f,0, .9f,.95f,.85f,1, 0,0,0,0};
        std::memcpy(&f.uniforms[1][32],&format,4);
    }
    auto base=all;
    for(auto f:base) {
        if(f.path=="Rectangle/Shaders/Rectangle" && f.definitions.empty()) {
            f.scale=1.5f; f.uniforms[0][0]=2.f/(W/f.scale); f.uniforms[0][5]=-2.f/(H/f.scale);
            f.label="rectangle: Canvas projection at 150% DPI"; all.push_back(f);
        }
        if(f.label=="Mesh3d" || f.label=="FastLines3d") {
            f.probeDepth=true; f.label+=" with depth occlusion probe"; all.push_back(f);
        }
        if(f.path=="FastLines/Shaders/FastLines") {
            f.points={12,20, 95,75, 13,21, 80,100}; f.label="fast lines: near-hairpin join"; all.push_back(f);
            f.points={12,20, 12,20, 70,60, 110,100}; f.label="fast lines: repeated point (original behavior)"; all.push_back(f);
        }
        if(f.path=="Lines3d/Shaders/FastLines3d") {
            f.probeDepth=false; f.uniforms[1][10]=0; f.label="FastLines3d: uniform color"; all.push_back(f);
        }
    }
    return all;
}

int main(int argc,char** argv) try {
    require(argc>=2,"Usage: ShaderParity <Primitives directory> [--hardware]"); OpenGL gl;
    ComPtr<ID3D12Debug1> validation;
    check(D3D12GetDebugInterface(IID_PPV_ARGS(&validation)),"GPU validation interface");
    validation->EnableDebugLayer(); validation->SetEnableGPUBasedValidation(TRUE);
    auto device=std::make_shared<Device>(argc<3,true); Canvas canvas(nullptr,device);
    unsigned failures=0;
    for(const auto& fixture:fixtures()) {
        std::cout << "Rendering " << fixture.label << std::endl;
        auto expected=gl.render(argv[1],fixture);
        Pixels actual;
        try { actual=directX(canvas,argv[1],fixture); }
        catch (...) {
            ComPtr<ID3D12InfoQueue> messages; device->gpu.As(&messages);
            if(messages) for(UINT64 i=0;i<messages->GetNumStoredMessages();++i) {
                SIZE_T size=0; messages->GetMessage(i,nullptr,&size); std::vector<unsigned char> bytes(size);
                auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data()); messages->GetMessage(i,message,&size);
                std::cerr << message->pDescription << '\n';
            }
            throw;
        }
        unsigned maximum=0, differing=0, visible=0, interiorFailures=0; double error=0;
        for(size_t i=0;i<actual.size();++i) {
            unsigned delta=unsigned(std::abs(int(actual[i])-int(expected[i])));
            maximum=std::max(maximum,delta); differing+=delta>3; error+=delta; visible+=expected[i]!=255;
        }
        // Discard and nearest-sampling boundaries can move by one pixel due to
        // interpolation/derivative rounding. Large differences must match a
        // neighboring reference pixel; never allow a broad interior mismatch.
        for(unsigned y=0;y<H;++y) for(unsigned x=0;x<W;++x) {
            bool match=false;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                int xx=int(x)+dx, yy=int(y)+dy;
                if(xx<0 || yy<0 || xx>=int(W) || yy>=int(H)) continue;
                bool nearby=true;
                for(unsigned c=0;c<4;++c) nearby &= std::abs(int(actual[(y*W+x)*4+c])-int(expected[(yy*W+xx)*4+c]))<=3;
                match|=nearby;
            }
            interiorFailures+=!match;
        }
        bool ok=visible>0 && differing<=actual.size()/500 && interiorFailures==0;
        std::cout << (ok?"PASS ":"FAIL ") << fixture.label << ": max=" << maximum << ", >3=" << differing
                  << ", mean=" << error/actual.size() << ", interior failures=" << interiorFailures << '\n';
        failures+=!ok;
    }
    require(failures==0,"GLSL/HLSL rendered parity failed");
    ComPtr<ID3D12InfoQueue> messages; check(device->gpu.As(&messages),"Read validation messages");
    for(UINT64 i=0;i<messages->GetNumStoredMessages();++i) {
        SIZE_T size=0; messages->GetMessage(i,nullptr,&size); std::vector<unsigned char> bytes(size);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data()); messages->GetMessage(i,message,&size);
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_WARNING) throw std::runtime_error(message->pDescription);
    }
    return 0;
} catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }

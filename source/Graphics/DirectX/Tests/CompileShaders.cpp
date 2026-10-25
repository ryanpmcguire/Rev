#include "../Helpers/Pipeline.hpp"
#include <d3d11shader.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace Rev::Graphics::DirectX;

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Usage: CompileShaders <Primitives directory>");
    unsigned count = 0;
    for (const auto& item : std::filesystem::recursive_directory_iterator(argv[1])) {
        auto path = item.path();
        if (path.extension() != ".hlsl") continue;
        std::ifstream stream(path, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(stream)), {});
        const bool vertex = path.stem().extension() == ".vert";
        for (int variant = 0; variant < (path.filename() == "Rectangle.frag.hlsl" ? 2 : 1); ++variant) {
            Shader shader({reinterpret_cast<const unsigned char*>(text.data()), text.size()},
                vertex ? Shader::Vertex : Shader::Fragment, variant ? "#define STENCIL" : "");
            ComPtr<ID3D11ShaderReflection> reflection;
            check(D3DReflect(shader.shader->GetBufferPointer(), shader.shader->GetBufferSize(),
                IID_PPV_ARGS(&reflection)), "Reflect HLSL");
            D3D11_SHADER_DESC desc{}; check(reflection->GetDesc(&desc), "Shader description");
            for (UINT i = 0; i < desc.ConstantBuffers; ++i) {
                auto* buffer = reflection->GetConstantBufferByIndex(i);
                D3D11_SHADER_BUFFER_DESC layout{}; check(buffer->GetDesc(&layout), "Cbuffer description");
                if (layout.Type != D3D_CT_CBUFFER) continue;
                D3D11_SHADER_INPUT_BIND_DESC binding{};
                check(reflection->GetResourceBindingDescByName(layout.Name, &binding), "Cbuffer binding");
                for (UINT j = 0; j < layout.Variables; ++j) {
                    D3D11_SHADER_VARIABLE_DESC variable{};
                    check(buffer->GetVariableByIndex(j)->GetDesc(&variable), "Variable description");
                    std::cout << path.filename().string() << '|' << variant << '|' << layout.Name << '|'
                        << binding.BindPoint << '|' << variable.Name << '|' << variable.StartOffset << '|'
                        << variable.Size << '\n';
                }
            }
            ++count;
        }
    }
    std::cout << "Compiled " << count << " shader stages/variants.\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }

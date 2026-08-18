// Lightweight checks on the embedded HLSL source.
//
// The production shader is compiled at runtime by Microsoft's D3DCompiler.
// MinGW can build the application without parsing HLSL, so a typo or a reserved
// identifier would otherwise ship unnoticed.
#include "shaders.h"

#include <cstdio>
#include <regex>
#include <string>

int main() {
    const std::string shader = crtb::kWarpShaderHlsl;
    const char* reservedIdentifiers[] = {
        "line", "lineadj", "linear", "point", "sample", "triangle", "triangleadj",
    };

    int failures = 0;
    for (const char* name : reservedIdentifiers) {
        const std::regex declaration(
            std::string(R"(\b(?:bool|int|uint|float|float2|float3|float4)\s+)") +
            name + R"(\b)");
        if (std::regex_search(shader, declaration)) {
            std::printf("FAIL: HLSL reserved identifier used as a variable: %s\n", name);
            ++failures;
        }
    }

    if (shader.find("float4 PSMain(") == std::string::npos) {
        std::printf("FAIL: PSMain entry point is missing\n");
        ++failures;
    }
    if (shader.find("VSOut VSMain(") == std::string::npos) {
        std::printf("FAIL: VSMain entry point is missing\n");
        ++failures;
    }

    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}

#include "program.h"

namespace ftdgl {
namespace impl {
static
ProgramPtr g_RenderProgram = {};

static
const char * vert_source = "\n"
        "attribute vec2 position2;\n"
        "attribute vec4 rect;\n"
        "attribute vec4 color;\n"
        "varying vec2 _coord2;\n"
        "varying vec4 _color;\n"
        "void main() {\n"
        "	_coord2 = mix(rect.xy, rect.zw, position2 * 0.5 + 0.5);\n"
        "   _color = color;\n"
        "	gl_Position = vec4(_coord2 * 2.0 - 1.0, 0.0, 1.0);\n"
        "}\n";

static
const char * frag_source = "\n"
        "uniform sampler2D texture;\n"
        "uniform float first_round;\n"
        "varying vec2 _coord2;\n"
        "varying vec4 _color;\n"
        "void main() {\n"
        "	// Get samples for -2/3 and -1/3\n"
        "	vec2 valueL = texture2D(texture, vec2(_coord2.x + dFdx(_coord2.x), _coord2.y)).yz * 255.0;\n"
        "	vec2 lowerL = mod(valueL, 16.0);\n"
        "	vec2 upperL = (valueL - lowerL) / 16.0;\n"
        "	vec2 alphaL = min(abs(upperL - lowerL), 2.0);\n"
        "\n"
        "	// Get samples for 0, +1/3, and +2/3\n"
        "	vec3 valueR = texture2D(texture, _coord2).xyz * 255.0;\n"
        "	vec3 lowerR = mod(valueR, 16.0);\n"
        "	vec3 upperR = (valueR - lowerR) / 16.0;\n"
        "	vec3 alphaR = min(abs(upperR - lowerR), 2.0);\n"
        "\n"
        "	// Average the energy over the pixels on either side\n"
        "	vec4 rgba = vec4(\n"
        "		(alphaR.x + alphaR.y + alphaR.z) / 6.0,\n"
        "		(alphaL.y + alphaR.x + alphaR.y) / 6.0,\n"
        "		(alphaL.x + alphaL.y + alphaR.x) / 6.0,\n"
        "		0.0);\n"
        "\n"
        "	// Optionally scale by a color\n"
        "	gl_FragColor = first_round == 1.0 ? 1.0 - rgba : _color * rgba;\n"
        "}\n";


} //namespace impl

ProgramPtr CreateRenderProgram() {
    if (!impl::g_RenderProgram) {
        attrib_map_s map[] = {
            {3, "color"},
            {1, "rect"},
            {0, "position2"},
        };

        impl::g_RenderProgram = CreateProgram(impl::vert_source, impl::frag_source, sizeof(map) / sizeof(attrib_map_s), map);
    }

    return impl::g_RenderProgram;
}

} //namespace ftdgl

/*
===========================================================================

dhewm3 FSR 1.0 GLSL shader source strings

The EASU and RCAS shaders are composed at runtime from multiple string
parts passed to glShaderSource as an array:
  [0] version + A_GPU/A_GLSL defines (fixed string below)
  [1] ffx_a.h contents  (loaded from disk or embedded below)
  [2] ffx_fsr1.h contents (loaded from disk or embedded below)
  [3] main shader body (fixed string below)

Set D3_INTEGRATE_FSR_SHADERS to 1 to embed the AMD headers as string
literals (production build).  Set to 0 to load from disk at init time
(useful for shader iteration/debugging).

===========================================================================
*/

#ifndef __FSR_GLSL_H__
#define __FSR_GLSL_H__

// Shared vertex shader — a simple pass-through
static const char *fsrVertSrc =
	"#version 400 compatibility\n"
	"void main() { gl_Position = gl_Vertex; }\n";

// EASU preamble (part 0 of the fragment shader source array)
static const char *fsrEasuPreamble =
	"#version 400\n"
	"#extension GL_ARB_shading_language_packing : enable\n"
	"#define A_GPU 1\n"
	"#define A_GLSL 1\n"
	"#define A_SKIP_EXT 1\n"
	"#define FSR_EASU_F 1\n";

// EASU body (part 3 of the fragment shader source array)
static const char *fsrEasuBody =
	"uniform sampler2D InputTexture;\n"
	"uniform uvec4 Const0;\n"
	"uniform uvec4 Const1;\n"
	"uniform uvec4 Const2;\n"
	"uniform uvec4 Const3;\n"
	"AF4 FsrEasuRF(AF2 p){ return textureGather(InputTexture, p, 0); }\n"
	"AF4 FsrEasuGF(AF2 p){ return textureGather(InputTexture, p, 1); }\n"
	"AF4 FsrEasuBF(AF2 p){ return textureGather(InputTexture, p, 2); }\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    AF3 col;\n"
	"    FsrEasuF(col, AU2(gl_FragCoord.xy), Const0, Const1, Const2, Const3);\n"
	"    FragColor = vec4(col, 1.0);\n"
	"}\n";

// RCAS preamble (part 0 of the fragment shader source array)
static const char *fsrRcasPreamble =
	"#version 400\n"
	"#extension GL_ARB_shading_language_packing : enable\n"
	"#define A_GPU 1\n"
	"#define A_GLSL 1\n"
	"#define A_SKIP_EXT 1\n"
	"#define FSR_RCAS_F 1\n";

// RCAS body (part 3 of the fragment shader source array)
static const char *fsrRcasBody =
	"uniform sampler2D InputTexture;\n"
	"uniform uvec4 Const0;\n"
	"AF4 FsrRcasLoadF(ASU2 p){ return texelFetch(InputTexture, ASU2(p), 0); }\n"
	"void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b){}\n"
	"out vec4 FragColor;\n"
	"void main() {\n"
	"    AF3 col;\n"
	"    FsrRcasF(col.r, col.g, col.b, AU2(gl_FragCoord.xy), Const0);\n"
	"    FragColor = vec4(col, 1.0);\n"
	"}\n";

#endif /* !__FSR_GLSL_H__ */

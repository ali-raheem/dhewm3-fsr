/*
===========================================================================

dhewm3 FSR 2.0 GLSL shader source strings

Contains:
- Vertex shader (pass-through)
- Depth clip shader (depth dilation + prepare for accumulation)
- Temporal accumulation shader (main upscaler)
- RCAS shader (sharpening)

Based on AMD FidelityFX Super Resolution 2.0 (FSR 2.0)
https://github.com/GPUOpen-Effects/FidelityFX-FSR2

===========================================================================
*/

#ifndef __FSR2_GLSL_H__
#define __FSR2_GLSL_H__

static const char *fsr2VertSrc =
	"#version 400 compatibility\n"
	"void main() { gl_Position = gl_Vertex; }\n";

// AU1 = uint, AF1 = float, vec2, vec3, vec4
// FFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE = 0 (simple bilinear)

static const char *fsr2DepthClipFragSrc =
	"#version 400\n"
	"uniform sampler2D depthTexture;\n"
	"uniform sampler2D prevDepthTexture;\n"
	"uniform sampler2D motionTexture;\n"
	"uniform vec2 inputSize;\n"
	"uniform vec2 jitter;\n"
	"out vec4 fragColor;\n"
	"\n"
	"float loadDepth(ivec2 pos) {\n"
	"    pos = clamp(pos, ivec2(0), ivec2(inputSize) - 1);\n"
	"    return texelFetch(depthTexture, pos, 0).r;\n"
	"}\n"
	"\n"
	"void main() {\n"
	"    ivec2 iPos = ivec2(gl_FragCoord.xy);\n"
	"    vec2 uv = (vec2(iPos) + 0.5) / inputSize;\n"
	"    \n"
	"    float depth = loadDepth(iPos);\n"
	"    \n"
	"    vec2 velocity = texture(motionTexture, uv).rg;\n"
	"    vec2 prevUV = uv - velocity;\n"
	"    \n"
	"    float prevDepth = 0.0;\n"
	"    float confidence = 0.0;\n"
	"    \n"
	"    if (prevUV.x >= 0.0 && prevUV.x <= 1.0 && prevUV.y >= 0.0 && prevUV.y <= 1.0) {\n"
	"        ivec2 prevIPos = ivec2(prevUV * inputSize);\n"
	"        prevDepth = texelFetch(prevDepthTexture, clamp(prevIPos, ivec2(0), ivec2(inputSize) - 1), 0).r;\n"
	"        \n"
	"        float depthDiff = abs(depth - prevDepth);\n"
	"        confidence = 1.0 - min(depthDiff * 100.0, 1.0);\n"
	"    }\n"
	"    \n"
	"    float dilatedDepth = depth;\n"
	"    for (int y = -1; y <= 1; y++) {\n"
	"        for (int x = -1; x <= 1; x++) {\n"
	"            dilatedDepth = max(dilatedDepth, loadDepth(iPos + ivec2(x, y)));\n"
	"        }\n"
	"    }\n"
	"    \n"
	"    fragColor = vec4(dilatedDepth, prevDepth, confidence, 1.0);\n"
	"}\n";

static const char *fsr2AccumulateFragSrc =
	"#version 400\n"
	"uniform sampler2D currentColor;\n"
	"uniform sampler2D historyColor;\n"
	"uniform sampler2D motionTexture;\n"
	"uniform sampler2D depthTexture;\n"
	"uniform sampler2D lockStatusTexture;\n"
	"uniform vec2 inputSize;\n"
	"uniform vec2 displaySize;\n"
	"uniform vec2 jitter;\n"
	"uniform int frameIndex;\n"
	"out vec4 fragColor;\n"
	"\n"
	"void main() {\n"
	"    // Simple upscale: sample current color at corresponding input UV\n"
	"    vec2 inputUV = (gl_FragCoord.xy * inputSize / displaySize) / inputSize;\n"
	"    inputUV = clamp(inputUV, 0.0, 1.0);\n"
	"    \n"
	"    vec3 current = texture(currentColor, inputUV).rgb;\n"
	"    \n"
	"    fragColor = vec4(current, 1.0);\n"
	"}\n";

static const char *fsr2RcasFragSrc =
	"#version 400\n"
	"uniform sampler2D inputTexture;\n"
	"uniform float sharpness;\n"
	"out vec4 fragColor;\n"
	"\n"
	"void main() {\n"
	"    vec2 uv = gl_FragCoord.xy / vec2(textureSize(inputTexture, 0));\n"
	"    ivec2 pos = ivec2(gl_FragCoord.xy);\n"
	"    \n"
	"    vec3 c = texelFetch(inputTexture, pos, 0).rgb;\n"
	"    \n"
	"    vec3 n = texelFetch(inputTexture, pos + ivec2(0, -1), 0).rgb;\n"
	"    vec3 s = texelFetch(inputTexture, pos + ivec2(0,  1), 0).rgb;\n"
	"    vec3 w = texelFetch(inputTexture, pos + ivec2(-1, 0), 0).rgb;\n"
	"    vec3 e = texelFetch(inputTexture, pos + ivec2( 1, 0), 0).rgb;\n"
	"    \n"
	"    float sharp = 1.0 - sharpness;\n"
	"    \n"
	"    vec3 minC = min(c, min(min(n, s), min(w, e)));\n"
	"    vec3 maxC = max(c, max(max(n, s), max(w, e)));\n"
	"    \n"
	"    vec3 range = maxC - minC;\n"
	"    vec3 blur = (n + s + w + e) * 0.25;\n"
	"    \n"
	"    vec3 result = mix(blur, c, sharp);\n"
	"    result = clamp(result, minC, maxC);\n"
	"    \n"
	"    fragColor = vec4(result, 1.0);\n"
	"}\n";

static const char *fsr2VelocityFragSrc =
	"#version 400\n"
	"uniform sampler2D depthTexture;\n"
	"uniform mat4 prevViewMatrix;\n"
	"uniform mat4 prevProjectionMatrix;\n"
	"uniform mat4 invProjectionMatrix;\n"
	"uniform vec2 renderSize;\n"
	"out vec2 fragVelocity;\n"
	"\n"
	"vec3 reconstructViewPos(vec2 uv, float depth) {\n"
	"    vec2 ndc = uv * 2.0 - 1.0;\n"
	"    vec4 clipPos = vec4(ndc, depth * 2.0 - 1.0, 1.0);\n"
	"    vec4 viewPos = invProjectionMatrix * clipPos;\n"
	"    viewPos /= viewPos.w;\n"
	"    return viewPos.xyz;\n"
	"}\n"
	"\n"
	"void main() {\n"
	"    vec2 uv = gl_FragCoord.xy / renderSize;\n"
	"    float depth = texture(depthTexture, uv).r;\n"
	"    \n"
	"    vec3 viewPos = reconstructViewPos(uv, depth);\n"
	"    \n"
	"    vec4 prevClipPos = prevProjectionMatrix * prevViewMatrix * vec4(viewPos, 1.0);\n"
	"    prevClipPos /= prevClipPos.w;\n"
	"    vec2 prevNdc = prevClipPos.xy;\n"
	"    \n"
	"    vec2 currNdc = uv * 2.0 - 1.0;\n"
	"    \n"
	"    vec2 velocity = (currNdc - prevNdc) * 0.5;\n"
	"    \n"
	"    fragVelocity = velocity;\n"
	"}\n";

#endif

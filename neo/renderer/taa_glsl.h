/*
===========================================================================

dhewm3 TAA GLSL shader source strings

Contains:
- Velocity vertex shader (pass-through)
- Velocity fragment shader (generates motion vectors from depth)
- TAA resolve vertex shader (pass-through)
- TAA resolve fragment shader (temporal blend)

===========================================================================
*/

#ifndef __TAA_GLSL_H__
#define __TAA_GLSL_H__

static const char *taaVertSrc =
	"#version 400 compatibility\n"
	"void main() { gl_Position = gl_Vertex; }\n";

static const char *taaVelocityFragSrc =
	"#version 400\n"
	"uniform sampler2D depthTexture;\n"
	"uniform mat4 prevViewMatrix;\n"
	"uniform mat4 prevProjectionMatrix;\n"
	"uniform mat4 invProjectionMatrix;\n"
	"uniform vec2 renderSize;\n"
	"out vec2 fragVelocity;\n"
	"\n"
	"vec3 reconstructWorldPos(vec2 uv, float depth) {\n"
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
	"    vec3 viewPos = reconstructWorldPos(uv, depth);\n"
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

static const char *taaResolveFragSrc =
	"#version 400\n"
	"uniform sampler2D currentTexture;\n"
	"uniform sampler2D historyTexture;\n"
	"uniform sampler2D velocityTexture;\n"
	"uniform float feedbackFactor;\n"
	"uniform vec2 renderSize;\n"
	"out vec4 fragColor;\n"
	"\n"
	"vec3 sampleBox(sampler2D tex, vec2 uv, vec2 texelSize) {\n"
	"    vec3 result = vec3(0.0);\n"
	"    result += texture(tex, uv + vec2(-0.5, -0.5) * texelSize).rgb;\n"
	"    result += texture(tex, uv + vec2( 0.5, -0.5) * texelSize).rgb;\n"
	"    result += texture(tex, uv + vec2(-0.5,  0.5) * texelSize).rgb;\n"
	"    result += texture(tex, uv + vec2( 0.5,  0.5) * texelSize).rgb;\n"
	"    return result * 0.25;\n"
	"}\n"
	"\n"
	"vec3 aabbMin(vec3 a, vec3 b) { return min(a, b); }\n"
	"vec3 aabbMax(vec3 a, vec3 b) { return max(a, b); }\n"
	"\n"
	"vec3 clampToAABB(vec3 color, vec3 minColor, vec3 maxColor) {\n"
	"    return clamp(color, minColor, maxColor);\n"
	"}\n"
	"\n"
	"void main() {\n"
	"    vec2 texelSize = 1.0 / renderSize;\n"
	"    vec2 uv = gl_FragCoord.xy / renderSize;\n"
	"    \n"
	"    vec2 velocity = texture(velocityTexture, uv).rg;\n"
	"    vec2 prevUV = uv - velocity;\n"
	"    \n"
	"    vec3 currentColor = texture(currentTexture, uv).rgb;\n"
	"    \n"
	"    vec3 minColor = vec3( 1e9);\n"
	"    vec3 maxColor = vec3(-1e9);\n"
	"    for (int x = -1; x <= 1; x++) {\n"
	"        for (int y = -1; y <= 1; y++) {\n"
	"            vec3 sampleColor = texture(currentTexture, uv + vec2(x, y) * texelSize).rgb;\n"
	"            minColor = aabbMin(minColor, sampleColor);\n"
	"            maxColor = aabbMax(maxColor, sampleColor);\n"
	"        }\n"
	"    }\n"
	"    \n"
	"    vec3 historyColor = vec3(0.0);\n"
	"    float historyValid = 0.0;\n"
	"    \n"
	"    if (prevUV.x >= 0.0 && prevUV.x <= 1.0 && prevUV.y >= 0.0 && prevUV.y <= 1.0) {\n"
	"        historyColor = sampleBox(historyTexture, prevUV, texelSize);\n"
	"        historyValid = 1.0;\n"
	"    }\n"
	"    \n"
	"    historyColor = clampToAABB(historyColor, minColor, maxColor);\n"
	"    \n"
	"    vec3 resolvedColor;\n"
	"    if (historyValid > 0.5) {\n"
	"        resolvedColor = mix(currentColor, historyColor, feedbackFactor);\n"
	"    } else {\n"
	"        resolvedColor = currentColor;\n"
	"    }\n"
	"    \n"
	"    fragColor = vec4(resolvedColor, 1.0);\n"
	"}\n";

static const char *taaCopyVertSrc =
	"#version 400 compatibility\n"
	"void main() { gl_Position = gl_Vertex; }\n";

static const char *taaCopyFragSrc =
	"#version 400\n"
	"uniform sampler2D inputTexture;\n"
	"uniform vec2 texScale;\n"
	"out vec4 fragColor;\n"
	"\n"
	"void main() {\n"
	"    vec2 uv = gl_FragCoord.xy * texScale;\n"
	"    fragColor = texture(inputTexture, uv);\n"
	"}\n";

#endif

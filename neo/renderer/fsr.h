/*
===========================================================================

dhewm3 FSR 1.0 upscaling support
AMD FidelityFX Super Resolution 1.0

===========================================================================
*/

#ifndef __FSR_H__
#define __FSR_H__

#include "sys/platform.h"

// Forward-declare the GL types we use here without pulling in OpenGL headers.
// tr_local.h (which includes qgl.h) must be included before this in .cpp files.
#ifndef GL_FRAMEBUFFER
typedef unsigned int  GLuint;
typedef int           GLint;
typedef unsigned int  GLenum;
#endif

struct fsrState_t {
	bool   available;       // GL 4.0+ detected, extension pointers loaded, shaders compiled
	bool   active;          // r_fsr > 0 && available && internalW < displayW

	int    internalWidth,  internalHeight;  // 3D render resolution
	int    displayWidth,   displayHeight;   // output (window) resolution

	// Scene FBO (3D scene at internalWidth x internalHeight)
	GLuint sceneFBO;
	GLuint sceneColorTex;
	GLuint sceneDepthRBO;

	// EASU output FBO (displayWidth x displayHeight; only used when RCAS is active)
	GLuint easuFBO;
	GLuint easuColorTex;

	// GLSL programs
	GLuint easuProg;
	GLuint rcasProg;

	// Uniform locations
	GLint  easuTexLoc, easuC0, easuC1, easuC2, easuC3;
	GLint  rcasTexLoc, rcasC0;
};

extern fsrState_t fsr;

// called at end of R_InitOpenGL()
void FSR_Init();
// called before GLimp_Shutdown()
void FSR_Shutdown();
// called when r_fsr/r_fsrSharpness changes at runtime
void FSR_Reinit();
// called from R_CheckCvars()
void FSR_CheckCvars();
// bind sceneFBO, set internal-res viewport (called before 3D draws)
void FSR_BeginScene();
// run EASU+RCAS, restore default FBO + display viewport (called after 3D draws)
void FSR_EndScene();

inline bool FSR_IsActive() { return fsr.active; }

#endif /* !__FSR_H__ */

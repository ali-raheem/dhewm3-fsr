/*
===========================================================================

dhewm3 FSR 2.0 upscaling support
AMD FidelityFX Super Resolution 2.0

FSR 2.0 uses temporal upscaling with motion vectors to achieve better
quality than FSR 1.0 at similar performance costs.

Requires OpenGL 4.0 (for GLSL 4.00 and multiple render targets).

===========================================================================
*/

#ifndef __FSR2_H__
#define __FSR2_H__

#include "sys/platform.h"

#ifndef GL_FRAMEBUFFER
typedef unsigned int  GLuint;
typedef int           GLint;
typedef unsigned int  GLenum;
#endif

struct fsr2State_t {
	bool available;
	bool active;

	int inputWidth, inputHeight;
	int displayWidth, displayHeight;

	int frameIndex;
	float jitterX, jitterY;

	GLuint sceneFBO;
	GLuint sceneColorTex;
	GLuint sceneDepthTex;
	GLuint velocityTex;

	GLuint historyFBO;
	GLuint historyColorTex;
	GLuint lockStatusTex[2];
	int lockStatusIndex;

	GLuint outputFBO;
	GLuint outputColorTex;

	GLuint depthClipProg;
	GLuint accumulateProg;
	GLuint rcasProg;

	GLint depthClipDepthLoc, depthClipPrevDepthLoc, depthClipMotionLoc;
	GLint depthClipInputSizeLoc, depthClipJitterLoc;

	GLint accumCurrColorLoc, accumHistoryLoc, accumMotionLoc, accumDepthLoc;
	GLint accumLockStatusLoc, accumExposureLoc;
	GLint accumInputSizeLoc, accumDisplaySizeLoc, accumJitterLoc;
	GLint accumFrameIndexLoc, accumDeltaLoc;

	GLint rcasTexLoc, rcasSharpnessLoc;
};

extern fsr2State_t fsr2;

void FSR2_Init();
void FSR2_Shutdown();
void FSR2_Reinit();
void FSR2_CheckCvars();
void FSR2_BeginScene();
void FSR2_EndScene();

inline bool FSR2_IsActive() { return fsr2.active; }

#endif

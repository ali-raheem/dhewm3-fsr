/*
===========================================================================

dhewm3 TAA (Temporal Anti-Aliasing) support

This module implements temporal anti-aliasing using motion vectors and
temporal history. It can work independently or alongside FSR upscaling.

The implementation generates motion vectors in a separate pass, then
resolves TAA by blending the current frame with a reprojected history
buffer. This reduces temporal aliasing and edge shimmering.

Requires OpenGL 4.0 (for GLSL 4.00 and multiple render targets).

===========================================================================
*/

#ifndef __TAA_H__
#define __TAA_H__

#include "sys/platform.h"

#ifndef GL_FRAMEBUFFER
typedef unsigned int  GLuint;
typedef int           GLint;
typedef unsigned int  GLenum;
#endif

struct taaState_t {
	bool available;
	bool active;

	int renderWidth, renderHeight;

	float prevViewMatrix[16];
	float prevProjectionMatrix[16];
	bool havePrevMatrices;

	int jitterIndex;
	float jitterX, jitterY;

	GLuint sceneFBO;
	GLuint sceneColorTex;
	GLuint sceneDepthTex;
	GLuint velocityTex;

	GLuint historyFBO;
	GLuint historyColorTex;
	bool historyValid;

	GLuint velocityProg;
	GLuint taaProg;
	GLuint copyProg;

	GLint velDepthLoc, velPrevViewLoc, velPrevProjLoc, velInvProjLoc, velWidthLoc;
	GLint taaCurrLoc, taaHistLoc, taaVelLoc, taaFeedbackLoc, taaWidthLoc;
	GLint copyTexLoc, copyTexScaleLoc;
};

extern taaState_t taa;

void TAA_Init();
void TAA_Shutdown();
void TAA_Reinit();
void TAA_CheckCvars();
void TAA_BeginScene();
void TAA_VelocityPass();
void TAA_Resolve();
void TAA_StoreMatrices();
void TAA_InvalidateHistory();

inline bool TAA_IsActive() { return taa.active; }
bool TAA_NeedsSceneFBO();

#endif

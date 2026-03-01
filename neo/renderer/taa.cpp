/*
===========================================================================

dhewm3 TAA (Temporal Anti-Aliasing) support

This module implements temporal anti-aliasing using motion vectors and
temporal history. It can work independently or alongside FSR upscaling.

Requires OpenGL 4.0 (for GLSL 4.00 and multiple render targets).

===========================================================================
*/

#include "sys/platform.h"
#include "renderer/tr_local.h"
#include "renderer/taa.h"
#include "renderer/taa_glsl.h"
#include "renderer/fsr.h"
#include "renderer/fsr2.h"

typedef void        (APIENTRYP PFNTAAGENFRAMEBUFFERSPROC)   (GLsizei, GLuint *);
typedef void        (APIENTRYP PFNTAADELETEFRAMEBUFFERSPROC)(GLsizei, const GLuint *);
typedef void        (APIENTRYP PFNTAABINDFRAMEBUFFERPROC)    (GLenum, GLuint);
typedef void        (APIENTRYP PFNTAAFRAMEBUFFERTEXTURE2DPROC)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void        (APIENTRYP PFNTAAGENRENDERBUFFERSPROC)   (GLsizei, GLuint *);
typedef void        (APIENTRYP PFNTAABINDRENDERBUFFERPROC)   (GLenum, GLuint);
typedef void        (APIENTRYP PFNTAARENDERBUFFERSTORAGEPROC)(GLenum, GLenum, GLsizei, GLsizei);
typedef void        (APIENTRYP PFNTAAFRAMEBUFFERRENDERBUFFERPROC)(GLenum, GLenum, GLenum, GLuint);
typedef void        (APIENTRYP PFNTAADELETERENDERBUFFERSPROC)(GLsizei, const GLuint *);
typedef GLenum      (APIENTRYP PFNTAACHECKFRAMEBUFFERSTATUSPROC)(GLenum);

typedef GLuint      (APIENTRYP PFNTAACREATESHADERPROC)      (GLenum);
typedef void        (APIENTRYP PFNTAASHADERSOURCEPROC)       (GLuint, GLsizei, const GLchar *const*, const GLint *);
typedef void        (APIENTRYP PFNTAACOMPILESHADERPROC)      (GLuint);
typedef void        (APIENTRYP PFNTAAGETSHADERIVPROC)        (GLuint, GLenum, GLint *);
typedef void        (APIENTRYP PFNTAAGETSHADERINFOLOGPROC)   (GLuint, GLsizei, GLsizei *, GLchar *);
typedef void        (APIENTRYP PFNTAADELETESHADERPROC)       (GLuint);
typedef GLuint      (APIENTRYP PFNTAACREATEPROGRAMPROC)      (void);
typedef void        (APIENTRYP PFNTAAATTACHSHADERPROC)       (GLuint, GLuint);
typedef void        (APIENTRYP PFNTAALINKPROGRAMPROC)        (GLuint);
typedef void        (APIENTRYP PFNTAAGETPROGRAMIVPROC)       (GLuint, GLenum, GLint *);
typedef void        (APIENTRYP PFNTAAGETPROGRAMINFOLOGPROC)  (GLuint, GLsizei, GLsizei *, GLchar *);
typedef void        (APIENTRYP PFNTAAUSEPROGRAMPROC)         (GLuint);
typedef void        (APIENTRYP PFNTAADELETEPROGRAMPROC)      (GLuint);
typedef GLint       (APIENTRYP PFNTAAGETUNIFORMLOCATIONPROC) (GLuint, const GLchar *);
typedef void        (APIENTRYP PFNTAAUNIFORM1IPROC)          (GLint, GLint);
typedef void        (APIENTRYP PFNTAAUNIFORM1FPROC)          (GLint, GLfloat);
typedef void        (APIENTRYP PFNTAAUNIFORM2FPROC)          (GLint, GLfloat, GLfloat);
typedef void        (APIENTRYP PFNTAAUNIFORMMATRIX4FVPROC)   (GLint, GLsizei, GLboolean, const GLfloat *);
typedef void        (APIENTRYP PFNTAADRAWBUFFERSPROC)        (GLsizei, const GLenum *);
typedef void        (APIENTRYP PFNTAABLITFRAMEBUFFERPROC)    (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);

static PFNTAAGENFRAMEBUFFERSPROC        taa_glGenFramebuffers;
static PFNTAADELETEFRAMEBUFFERSPROC     taa_glDeleteFramebuffers;
static PFNTAABINDFRAMEBUFFERPROC        taa_glBindFramebuffer;
static PFNTAAFRAMEBUFFERTEXTURE2DPROC   taa_glFramebufferTexture2D;
static PFNTAAGENRENDERBUFFERSPROC       taa_glGenRenderbuffers;
static PFNTAABINDRENDERBUFFERPROC       taa_glBindRenderbuffer;
static PFNTAARENDERBUFFERSTORAGEPROC    taa_glRenderbufferStorage;
static PFNTAAFRAMEBUFFERRENDERBUFFERPROC taa_glFramebufferRenderbuffer;
static PFNTAADELETERENDERBUFFERSPROC    taa_glDeleteRenderbuffers;
static PFNTAACHECKFRAMEBUFFERSTATUSPROC taa_glCheckFramebufferStatus;

static PFNTAACREATESHADERPROC           taa_glCreateShader;
static PFNTAASHADERSOURCEPROC           taa_glShaderSource;
static PFNTAACOMPILESHADERPROC          taa_glCompileShader;
static PFNTAAGETSHADERIVPROC            taa_glGetShaderiv;
static PFNTAAGETSHADERINFOLOGPROC       taa_glGetShaderInfoLog;
static PFNTAADELETESHADERPROC           taa_glDeleteShader;
static PFNTAACREATEPROGRAMPROC          taa_glCreateProgram;
static PFNTAAATTACHSHADERPROC           taa_glAttachShader;
static PFNTAALINKPROGRAMPROC            taa_glLinkProgram;
static PFNTAAGETPROGRAMIVPROC           taa_glGetProgramiv;
static PFNTAAGETPROGRAMINFOLOGPROC      taa_glGetProgramInfoLog;
static PFNTAAUSEPROGRAMPROC             taa_glUseProgram;
static PFNTAADELETEPROGRAMPROC          taa_glDeleteProgram;
static PFNTAAGETUNIFORMLOCATIONPROC     taa_glGetUniformLocation;
static PFNTAAUNIFORM1IPROC              taa_glUniform1i;
static PFNTAAUNIFORM1FPROC              taa_glUniform1f;
static PFNTAAUNIFORM2FPROC              taa_glUniform2f;
static PFNTAAUNIFORMMATRIX4FVPROC       taa_glUniformMatrix4fv;
static PFNTAADRAWBUFFERSPROC            taa_glDrawBuffers;
static PFNTAABLITFRAMEBUFFERPROC        taa_glBlitFramebuffer;

#ifndef GL_FRAMEBUFFER
# define GL_FRAMEBUFFER                  0x8D40
# define GL_READ_FRAMEBUFFER             0x8CA8
# define GL_DRAW_FRAMEBUFFER             0x8CA9
# define GL_COLOR_ATTACHMENT0            0x8CE0
# define GL_COLOR_ATTACHMENT1            0x8CE1
# define GL_DEPTH_STENCIL_ATTACHMENT     0x821A
# define GL_RENDERBUFFER                 0x8D41
# define GL_DEPTH24_STENCIL8             0x88F0
# define GL_FRAMEBUFFER_COMPLETE         0x8CD5
# define GL_COMPILE_STATUS               0x8B81
# define GL_LINK_STATUS                  0x8B82
# define GL_INFO_LOG_LENGTH              0x8B84
# define GL_VERTEX_SHADER                0x8B31
# define GL_FRAGMENT_SHADER              0x8B30
# define GL_RG                           0x8227
# define GL_RG16F                        0x822F
# define GL_DEPTH_STENCIL                0x84F9
# define GL_UNSIGNED_INT_24_8            0x84FA
# define GL_COLOR_BUFFER_BIT             0x00004000
#endif

#ifndef GL_HALF_FLOAT
# define GL_HALF_FLOAT                   0x140B
#endif

extern idCVar r_taa;
extern idCVar r_taaFeedback;
extern idCVar r_fsr;
extern idCVar r_fsr2;

taaState_t taa;

static const float halton23[16][2] = {
	{ 0.5f,  0.33333333f },
	{ 0.25f, 0.66666667f },
	{ 0.75f, 0.11111111f },
	{ 0.125f, 0.44444444f },
	{ 0.625f, 0.77777778f },
	{ 0.375f, 0.22222222f },
	{ 0.875f, 0.55555556f },
	{ 0.0625f, 0.88888889f },
	{ 0.5625f, 0.03703704f },
	{ 0.3125f, 0.37037037f },
	{ 0.8125f, 0.70370370f },
	{ 0.1875f, 0.14814815f },
	{ 0.6875f, 0.48148148f },
	{ 0.4375f, 0.81481481f },
	{ 0.9375f, 0.25925926f },
	{ 0.03125f, 0.59259259f }
};

static bool TAA_LoadExtensionPointers( void ) {
#define LOAD_PROC(type, name) \
	taa_gl##name = (type) GLimp_ExtensionPointer( "gl" #name ); \
	if ( !taa_gl##name ) { common->Warning( "TAA: gl" #name " not found" ); return false; }

	LOAD_PROC( PFNTAAGENFRAMEBUFFERSPROC,         GenFramebuffers         )
	LOAD_PROC( PFNTAADELETEFRAMEBUFFERSPROC,       DeleteFramebuffers      )
	LOAD_PROC( PFNTAABINDFRAMEBUFFERPROC,          BindFramebuffer         )
	LOAD_PROC( PFNTAAFRAMEBUFFERTEXTURE2DPROC,     FramebufferTexture2D    )
	LOAD_PROC( PFNTAAGENRENDERBUFFERSPROC,         GenRenderbuffers        )
	LOAD_PROC( PFNTAABINDRENDERBUFFERPROC,         BindRenderbuffer        )
	LOAD_PROC( PFNTAARENDERBUFFERSTORAGEPROC,      RenderbufferStorage     )
	LOAD_PROC( PFNTAAFRAMEBUFFERRENDERBUFFERPROC,  FramebufferRenderbuffer )
	LOAD_PROC( PFNTAADELETERENDERBUFFERSPROC,      DeleteRenderbuffers     )
	LOAD_PROC( PFNTAACHECKFRAMEBUFFERSTATUSPROC,   CheckFramebufferStatus  )
	LOAD_PROC( PFNTAACREATESHADERPROC,             CreateShader            )
	LOAD_PROC( PFNTAASHADERSOURCEPROC,             ShaderSource            )
	LOAD_PROC( PFNTAACOMPILESHADERPROC,            CompileShader           )
	LOAD_PROC( PFNTAAGETSHADERIVPROC,              GetShaderiv             )
	LOAD_PROC( PFNTAAGETSHADERINFOLOGPROC,         GetShaderInfoLog        )
	LOAD_PROC( PFNTAADELETESHADERPROC,             DeleteShader            )
	LOAD_PROC( PFNTAACREATEPROGRAMPROC,            CreateProgram           )
	LOAD_PROC( PFNTAAATTACHSHADERPROC,             AttachShader            )
	LOAD_PROC( PFNTAALINKPROGRAMPROC,              LinkProgram             )
	LOAD_PROC( PFNTAAGETPROGRAMIVPROC,             GetProgramiv            )
	LOAD_PROC( PFNTAAGETPROGRAMINFOLOGPROC,        GetProgramInfoLog       )
	LOAD_PROC( PFNTAAUSEPROGRAMPROC,               UseProgram              )
	LOAD_PROC( PFNTAADELETEPROGRAMPROC,            DeleteProgram           )
	LOAD_PROC( PFNTAAGETUNIFORMLOCATIONPROC,       GetUniformLocation      )
	LOAD_PROC( PFNTAAUNIFORM1IPROC,                Uniform1i               )
	LOAD_PROC( PFNTAAUNIFORM1FPROC,                Uniform1f               )
	LOAD_PROC( PFNTAAUNIFORM2FPROC,                Uniform2f               )
	LOAD_PROC( PFNTAAUNIFORMMATRIX4FVPROC,         UniformMatrix4fv        )
	LOAD_PROC( PFNTAADRAWBUFFERSPROC,              DrawBuffers             )
	LOAD_PROC( PFNTAABLITFRAMEBUFFERPROC,          BlitFramebuffer         )
#undef LOAD_PROC
	return true;
}

static GLuint TAA_CompileShader( GLenum type, const GLchar **sources, GLsizei count ) {
	GLuint shader = taa_glCreateShader( type );
	if ( !shader ) return 0;

	taa_glShaderSource( shader, count, sources, NULL );
	taa_glCompileShader( shader );

	GLint status = 0;
	taa_glGetShaderiv( shader, GL_COMPILE_STATUS, &status );
	if ( !status ) {
		GLint logLen = 0;
		taa_glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &logLen );
		if ( logLen > 0 ) {
			char *log = (char *)Mem_Alloc( logLen );
			taa_glGetShaderInfoLog( shader, logLen, NULL, log );
			common->Warning( "TAA shader compile error:\n%s", log );
			Mem_Free( log );
		}
		taa_glDeleteShader( shader );
		return 0;
	}
	return shader;
}

static GLuint TAA_CompileProgram( GLuint vertShader, GLuint fragShader ) {
	GLuint prog = taa_glCreateProgram();
	if ( !prog ) return 0;

	taa_glAttachShader( prog, vertShader );
	taa_glAttachShader( prog, fragShader );
	taa_glLinkProgram( prog );

	GLint status = 0;
	taa_glGetProgramiv( prog, GL_LINK_STATUS, &status );
	if ( !status ) {
		GLint logLen = 0;
		taa_glGetProgramiv( prog, GL_INFO_LOG_LENGTH, &logLen );
		if ( logLen > 0 ) {
			char *log = (char *)Mem_Alloc( logLen );
			taa_glGetProgramInfoLog( prog, logLen, NULL, log );
			common->Warning( "TAA program link error:\n%s", log );
			Mem_Free( log );
		}
		taa_glDeleteProgram( prog );
		return 0;
	}
	return prog;
}

static bool TAA_CompileShaders( void ) {
	const GLchar *vertSrcs[1] = { taaVertSrc };
	GLuint vertShader = TAA_CompileShader( GL_VERTEX_SHADER, vertSrcs, 1 );
	if ( !vertShader ) return false;

	const GLchar *velSrcs[1] = { taaVelocityFragSrc };
	GLuint velFrag = TAA_CompileShader( GL_FRAGMENT_SHADER, velSrcs, 1 );
	if ( !velFrag ) { taa_glDeleteShader( vertShader ); return false; }

	taa.velocityProg = TAA_CompileProgram( vertShader, velFrag );
	taa_glDeleteShader( velFrag );
	if ( !taa.velocityProg ) { taa_glDeleteShader( vertShader ); return false; }

	const GLchar *taaSrcs[1] = { taaResolveFragSrc };
	GLuint taaFrag = TAA_CompileShader( GL_FRAGMENT_SHADER, taaSrcs, 1 );
	if ( !taaFrag ) { taa_glDeleteShader( vertShader ); return false; }

	taa.taaProg = TAA_CompileProgram( vertShader, taaFrag );
	taa_glDeleteShader( taaFrag );
	taa_glDeleteShader( vertShader );
	if ( !taa.taaProg ) return false;

	taa.velDepthLoc     = taa_glGetUniformLocation( taa.velocityProg, "depthTexture" );
	taa.velPrevViewLoc  = taa_glGetUniformLocation( taa.velocityProg, "prevViewMatrix" );
	taa.velPrevProjLoc  = taa_glGetUniformLocation( taa.velocityProg, "prevProjectionMatrix" );
	taa.velInvProjLoc   = taa_glGetUniformLocation( taa.velocityProg, "invProjectionMatrix" );
	taa.velWidthLoc     = taa_glGetUniformLocation( taa.velocityProg, "renderSize" );

	taa.taaCurrLoc      = taa_glGetUniformLocation( taa.taaProg, "currentTexture" );
	taa.taaHistLoc      = taa_glGetUniformLocation( taa.taaProg, "historyTexture" );
	taa.taaVelLoc       = taa_glGetUniformLocation( taa.taaProg, "velocityTexture" );
	taa.taaFeedbackLoc  = taa_glGetUniformLocation( taa.taaProg, "feedbackFactor" );
	taa.taaWidthLoc     = taa_glGetUniformLocation( taa.taaProg, "renderSize" );

	const GLchar *copyVertSrcs[1] = { taaCopyVertSrc };
	GLuint copyVert = TAA_CompileShader( GL_VERTEX_SHADER, copyVertSrcs, 1 );
	if ( !copyVert ) return false;

	const GLchar *copyFragSrcs[1] = { taaCopyFragSrc };
	GLuint copyFrag = TAA_CompileShader( GL_FRAGMENT_SHADER, copyFragSrcs, 1 );
	if ( !copyFrag ) { taa_glDeleteShader( copyVert ); return false; }

	taa.copyProg = TAA_CompileProgram( copyVert, copyFrag );
	taa_glDeleteShader( copyFrag );
	taa_glDeleteShader( copyVert );
	if ( !taa.copyProg ) return false;

	taa.copyTexLoc = taa_glGetUniformLocation( taa.copyProg, "inputTexture" );
	taa.copyTexScaleLoc = taa_glGetUniformLocation( taa.copyProg, "texScale" );

	common->Printf( "...TAA shaders compiled (velocity prog=%u, TAA prog=%u, copy prog=%u)\n",
		taa.velocityProg, taa.taaProg, taa.copyProg );
	return true;
}

static bool TAA_CreateFBOs( void ) {
	qglGenTextures( 1, &taa.sceneColorTex );
	qglBindTexture( GL_TEXTURE_2D, taa.sceneColorTex );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, taa.renderWidth, taa.renderHeight,
		0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglBindTexture( GL_TEXTURE_2D, 0 );

	qglGenTextures( 1, &taa.velocityTex );
	qglBindTexture( GL_TEXTURE_2D, taa.velocityTex );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RG16F, taa.renderWidth, taa.renderHeight,
		0, GL_RG, GL_HALF_FLOAT, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglBindTexture( GL_TEXTURE_2D, 0 );

	qglGenTextures( 1, &taa.sceneDepthTex );
	qglBindTexture( GL_TEXTURE_2D, taa.sceneDepthTex );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, taa.renderWidth, taa.renderHeight,
		0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglBindTexture( GL_TEXTURE_2D, 0 );

	taa_glGenFramebuffers( 1, &taa.sceneFBO );
	taa_glBindFramebuffer( GL_FRAMEBUFFER, taa.sceneFBO );
	taa_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, taa.sceneColorTex, 0 );
	taa_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
		GL_TEXTURE_2D, taa.velocityTex, 0 );
	taa_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
		GL_TEXTURE_2D, taa.sceneDepthTex, 0 );

	if ( taa_glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
		common->Warning( "TAA: scene FBO incomplete — disabling TAA" );
		taa_glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		return false;
	}
	taa_glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	qglGenTextures( 1, &taa.historyColorTex );
	qglBindTexture( GL_TEXTURE_2D, taa.historyColorTex );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, taa.renderWidth, taa.renderHeight,
		0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglBindTexture( GL_TEXTURE_2D, 0 );

	taa_glGenFramebuffers( 1, &taa.historyFBO );
	taa_glBindFramebuffer( GL_FRAMEBUFFER, taa.historyFBO );
	taa_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, taa.historyColorTex, 0 );
	if ( taa_glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
		common->Warning( "TAA: history FBO incomplete — disabling TAA" );
		taa_glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		return false;
	}
	taa_glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	return true;
}

static void TAA_DeleteFBOs( void ) {
	if ( taa.historyFBO )      { taa_glDeleteFramebuffers( 1, &taa.historyFBO );      taa.historyFBO = 0; }
	if ( taa.historyColorTex ) { qglDeleteTextures( 1, &taa.historyColorTex );        taa.historyColorTex = 0; }
	if ( taa.sceneFBO )        { taa_glDeleteFramebuffers( 1, &taa.sceneFBO );        taa.sceneFBO = 0; }
	if ( taa.velocityTex )     { qglDeleteTextures( 1, &taa.velocityTex );            taa.velocityTex = 0; }
	if ( taa.sceneColorTex )   { qglDeleteTextures( 1, &taa.sceneColorTex );          taa.sceneColorTex = 0; }
	if ( taa.sceneDepthTex )   { qglDeleteTextures( 1, &taa.sceneDepthTex );          taa.sceneDepthTex = 0; }
}

static void TAA_DeleteShaders( void ) {
	if ( taa.velocityProg ) { taa_glDeleteProgram( taa.velocityProg ); taa.velocityProg = 0; }
	if ( taa.taaProg )      { taa_glDeleteProgram( taa.taaProg );      taa.taaProg = 0; }
	if ( taa.copyProg )     { taa_glDeleteProgram( taa.copyProg );     taa.copyProg = 0; }
}

void TAA_Init( void ) {
	memset( &taa, 0, sizeof( taa ) );

	if ( !glConfig.fsrAvailable ) {
		common->Printf( "TAA: disabled (OpenGL 4.0 not available)\n" );
		return;
	}

	if ( !TAA_LoadExtensionPointers() ) {
		common->Warning( "TAA: failed to load required GL extension pointers" );
		return;
	}

	if ( !TAA_CompileShaders() ) {
		common->Warning( "TAA: shader compilation failed" );
		return;
	}

	taa.available = true;
	taa.havePrevMatrices = false;
	taa.historyValid = false;
	taa.jitterIndex = 0;
	common->Printf( "TAA: initialized (GL 4.0)\n" );
}

void TAA_Shutdown( void ) {
	TAA_DeleteFBOs();
	if ( taa_glDeleteProgram ) {
		TAA_DeleteShaders();
	}
	memset( &taa, 0, sizeof( taa ) );
}

void TAA_Reinit( void ) {
	if ( !taa.available ) return;

	TAA_DeleteFBOs();
	taa.active = false;
	taa.historyValid = false;

	if ( !r_taa.GetBool() ) {
		return;
	}

	if ( FSR2_IsActive() ) {
		taa.renderWidth  = fsr2.inputWidth;
		taa.renderHeight = fsr2.inputHeight;
	} else if ( FSR_IsActive() ) {
		taa.renderWidth  = fsr.internalWidth;
		taa.renderHeight = fsr.internalHeight;
	} else {
		taa.renderWidth  = glConfig.vidWidth;
		taa.renderHeight = glConfig.vidHeight;
	}

	if ( !TAA_CreateFBOs() ) {
		taa.available = false;
		return;
	}

	taa.active = true;
	common->Printf( "TAA: active at %dx%d\n", taa.renderWidth, taa.renderHeight );
}

void TAA_CheckCvars( void ) {
	if ( !taa.available ) return;

	if ( r_taa.IsModified() || r_fsr.IsModified() || r_fsr2.IsModified() ) {
		r_taa.ClearModified();
		TAA_Reinit();
	}
}

void TAA_InvalidateHistory( void ) {
	taa.historyValid = false;
	taa.havePrevMatrices = false;
}

void TAA_BeginScene( void ) {
	taa_glBindFramebuffer( GL_FRAMEBUFFER, taa.sceneFBO );

	GLuint buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	taa_glDrawBuffers( 2, buffers );

	qglViewport( 0, 0, taa.renderWidth, taa.renderHeight );
	if ( r_useScissor.GetBool() ) {
		qglScissor( 0, 0, taa.renderWidth, taa.renderHeight );
	}

	qglClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	qglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

	taa.jitterIndex = (taa.jitterIndex + 1) % 16;
	taa.jitterX = (halton23[taa.jitterIndex][0] - 0.5f) / (float)taa.renderWidth;
	taa.jitterY = (halton23[taa.jitterIndex][1] - 0.5f) / (float)taa.renderHeight;
}

void TAA_StoreMatrices( void ) {
	if ( !taa.active ) return;

	memcpy( taa.prevViewMatrix, backEnd.viewDef->worldSpace.modelViewMatrix, sizeof(taa.prevViewMatrix) );
	memcpy( taa.prevProjectionMatrix, backEnd.viewDef->projectionMatrix, sizeof(taa.prevProjectionMatrix) );
	taa.havePrevMatrices = true;
}

void TAA_VelocityPass( void ) {
	GLuint targetFBO = FSR2_IsActive() ? fsr2.sceneFBO : (FSR_IsActive() ? fsr.sceneFBO : taa.sceneFBO);
	GLuint depthTex = FSR2_IsActive() ? fsr2.sceneDepthTex : (FSR_IsActive() ? fsr.sceneDepthTex : taa.sceneDepthTex);

	taa_glBindFramebuffer( GL_FRAMEBUFFER, targetFBO );
	qglDrawBuffer( GL_COLOR_ATTACHMENT1 );

	if ( !taa.havePrevMatrices ) {
		qglClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		qglClear( GL_COLOR_BUFFER_BIT );
		qglDrawBuffer( GL_COLOR_ATTACHMENT0 );
		return;
	}

	qglViewport( 0, 0, taa.renderWidth, taa.renderHeight );

	qglDisable( GL_DEPTH_TEST );
	qglDisable( GL_STENCIL_TEST );
	qglDisable( GL_CULL_FACE );
	qglDisable( GL_BLEND );
	qglDisable( GL_SCISSOR_TEST );
	qglDisable( GL_VERTEX_PROGRAM_ARB );
	qglDisable( GL_FRAGMENT_PROGRAM_ARB );
	qglDisable( GL_ALPHA_TEST );
	qglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	qglDepthMask( GL_FALSE );

	float invProj[16];
	memset( invProj, 0, sizeof(invProj) );
	float a = backEnd.viewDef->projectionMatrix[0];
	float b = backEnd.viewDef->projectionMatrix[5];
	float c = backEnd.viewDef->projectionMatrix[10];
	float d = backEnd.viewDef->projectionMatrix[14];
	if ( fabsf(c) > 0.0001f && fabsf(d) > 0.0001f ) {
		invProj[0] = 1.0f / a;
		invProj[5] = 1.0f / b;
		invProj[10] = 0.0f;
		invProj[11] = -1.0f / d;
		invProj[14] = 1.0f;
		invProj[15] = c / d;
	}

	taa_glUseProgram( taa.velocityProg );

	qglActiveTextureARB( GL_TEXTURE0_ARB );
	qglBindTexture( GL_TEXTURE_2D, depthTex );
	taa_glUniform1i( taa.velDepthLoc, 0 );

	taa_glUniformMatrix4fv( taa.velPrevViewLoc, 1, GL_FALSE, taa.prevViewMatrix );
	taa_glUniformMatrix4fv( taa.velPrevProjLoc, 1, GL_FALSE, taa.prevProjectionMatrix );
	taa_glUniformMatrix4fv( taa.velInvProjLoc, 1, GL_FALSE, invProj );
	taa_glUniform2f( taa.velWidthLoc, (float)taa.renderWidth, (float)taa.renderHeight );

	qglBegin( GL_TRIANGLE_STRIP );
		qglVertex2f( -1.0f, -1.0f );
		qglVertex2f(  1.0f, -1.0f );
		qglVertex2f( -1.0f,  1.0f );
		qglVertex2f(  1.0f,  1.0f );
	qglEnd();

	taa_glUseProgram( 0 );
	qglDrawBuffer( GL_COLOR_ATTACHMENT0 );
}

void TAA_Resolve( void ) {
	GLuint srcColorTex = FSR2_IsActive() ? fsr2.sceneColorTex : (FSR_IsActive() ? fsr.sceneColorTex : taa.sceneColorTex);
	GLuint srcVelocityTex = FSR2_IsActive() ? fsr2.velocityTex : (FSR_IsActive() ? fsr.velocityTex : taa.velocityTex);
	GLuint srcFBO = FSR2_IsActive() ? fsr2.sceneFBO : (FSR_IsActive() ? fsr.sceneFBO : taa.sceneFBO);
	int width = FSR2_IsActive() ? fsr2.inputWidth : (FSR_IsActive() ? fsr.internalWidth : taa.renderWidth);
	int height = FSR2_IsActive() ? fsr2.inputHeight : (FSR_IsActive() ? fsr.internalHeight : taa.renderHeight);

	GLuint tempFBO = 0;
	GLuint tempTex = 0;

	qglGenTextures( 1, &tempTex );
	qglBindTexture( GL_TEXTURE_2D, tempTex );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height,
		0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	qglBindTexture( GL_TEXTURE_2D, 0 );

	taa_glGenFramebuffers( 1, &tempFBO );
	taa_glBindFramebuffer( GL_FRAMEBUFFER, tempFBO );
	taa_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, tempTex, 0 );

	qglViewport( 0, 0, width, height );
	qglDisable( GL_DEPTH_TEST );
	qglDisable( GL_STENCIL_TEST );
	qglDisable( GL_CULL_FACE );
	qglDisable( GL_BLEND );
	qglDisable( GL_SCISSOR_TEST );
	qglDisable( GL_VERTEX_PROGRAM_ARB );
	qglDisable( GL_FRAGMENT_PROGRAM_ARB );
	qglDisable( GL_ALPHA_TEST );
	qglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	qglDepthMask( GL_FALSE );

	taa_glUseProgram( taa.taaProg );

	qglActiveTextureARB( GL_TEXTURE0_ARB );
	qglBindTexture( GL_TEXTURE_2D, srcColorTex );
	taa_glUniform1i( taa.taaCurrLoc, 0 );

	qglActiveTextureARB( GL_TEXTURE1_ARB );
	qglBindTexture( GL_TEXTURE_2D, taa.historyColorTex );
	taa_glUniform1i( taa.taaHistLoc, 1 );

	qglActiveTextureARB( GL_TEXTURE2_ARB );
	qglBindTexture( GL_TEXTURE_2D, srcVelocityTex );
	taa_glUniform1i( taa.taaVelLoc, 2 );

	float feedback = taa.historyValid ? r_taaFeedback.GetFloat() : 0.0f;
	taa_glUniform1f( taa.taaFeedbackLoc, feedback );
	taa_glUniform2f( taa.taaWidthLoc, (float)width, (float)height );

	qglBegin( GL_TRIANGLE_STRIP );
		qglVertex2f( -1.0f, -1.0f );
		qglVertex2f(  1.0f, -1.0f );
		qglVertex2f( -1.0f,  1.0f );
		qglVertex2f(  1.0f,  1.0f );
	qglEnd();

	taa_glUseProgram( 0 );

	taa_glBindFramebuffer( GL_READ_FRAMEBUFFER, tempFBO );
	taa_glBindFramebuffer( GL_DRAW_FRAMEBUFFER, taa.historyFBO );
	taa_glBlitFramebuffer( 0, 0, width, height,
	                    0, 0, width, height,
	                    GL_COLOR_BUFFER_BIT, GL_NEAREST );

	if ( !FSR_IsActive() && !FSR2_IsActive() ) {
		taa_glBindFramebuffer( GL_READ_FRAMEBUFFER, tempFBO );
		taa_glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
		taa_glBlitFramebuffer( 0, 0, width, height,
		                    0, 0, width, height,
		                    GL_COLOR_BUFFER_BIT, GL_NEAREST );
	} else {
		taa_glBindFramebuffer( GL_READ_FRAMEBUFFER, tempFBO );
		taa_glBindFramebuffer( GL_DRAW_FRAMEBUFFER, srcFBO );
		qglDrawBuffer( GL_COLOR_ATTACHMENT0 );
		taa_glBlitFramebuffer( 0, 0, width, height,
		                    0, 0, width, height,
		                    GL_COLOR_BUFFER_BIT, GL_NEAREST );
	}

	taa_glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	taa.historyValid = true;

	taa_glDeleteFramebuffers( 1, &tempFBO );
	qglDeleteTextures( 1, &tempTex );
}

bool TAA_NeedsSceneFBO( void ) {
	return TAA_IsActive() || FSR_IsActive() || FSR2_IsActive();
}

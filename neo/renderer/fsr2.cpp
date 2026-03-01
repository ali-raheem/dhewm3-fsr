/*
===========================================================================

dhewm3 FSR 2.0 upscaling support
AMD FidelityFX Super Resolution 2.0

FSR 2.0 uses temporal upscaling with motion vectors for better quality
than FSR 1.0 at similar performance costs.

Requires OpenGL 4.0 (for GLSL 4.00 and multiple render targets).

===========================================================================
*/

#include "sys/platform.h"
#include "renderer/tr_local.h"
#include "renderer/fsr2.h"
#include "renderer/fsr2_glsl.h"
#include "renderer/taa.h"

typedef void        (APIENTRYP PFNFSR2GENFRAMEBUFFERSPROC)        (GLsizei, GLuint *);
typedef void        (APIENTRYP PFNFSR2DELETEFRAMEBUFFERSPROC)     (GLsizei, const GLuint *);
typedef void        (APIENTRYP PFNFSR2BINDFRAMEBUFFERPROC)        (GLenum, GLuint);
typedef void        (APIENTRYP PFNFSR2FRAMEBUFFERTEXTURE2DPROC)   (GLenum, GLenum, GLenum, GLuint, GLint);
typedef void        (APIENTRYP PFNFSR2GENRENDERBUFFERSPROC)       (GLsizei, GLuint *);
typedef void        (APIENTRYP PFNFSR2BINDRENDERBUFFERPROC)       (GLenum, GLuint);
typedef void        (APIENTRYP PFNFSR2RENDERBUFFERSTORAGEPROC)    (GLenum, GLenum, GLsizei, GLsizei);
typedef void        (APIENTRYP PFNFSR2FRAMEBUFFERRENDERBUFFERPROC)(GLenum, GLenum, GLenum, GLuint);
typedef void        (APIENTRYP PFNFSR2DELETERENDERBUFFERSPROC)    (GLsizei, const GLuint *);
typedef GLenum      (APIENTRYP PFNFSR2CHECKFRAMEBUFFERSTATUSPROC)(GLenum);
typedef void        (APIENTRYP PFNFSR2DRAWBUFFERSPROC)            (GLsizei, const GLenum *);
typedef void        (APIENTRYP PFNFSR2BLITFRAMEBUFFERPROC)        (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);

typedef GLuint      (APIENTRYP PFNFSR2CREATESHADERPROC)           (GLenum);
typedef void        (APIENTRYP PFNFSR2SHADERSOURCEPROC)           (GLuint, GLsizei, const GLchar *const*, const GLint *);
typedef void        (APIENTRYP PFNFSR2COMPILESHADERPROC)          (GLuint);
typedef void        (APIENTRYP PFNFSR2GETSHADERIVPROC)            (GLuint, GLenum, GLint *);
typedef void        (APIENTRYP PFNFSR2GETSHADERINFOLOGPROC)       (GLuint, GLsizei, GLsizei *, GLchar *);
typedef void        (APIENTRYP PFNFSR2DELETESHADERPROC)           (GLuint);
typedef GLuint      (APIENTRYP PFNFSR2CREATEPROGRAMPROC)          (void);
typedef void        (APIENTRYP PFNFSR2ATTACHSHADERPROC)           (GLuint, GLuint);
typedef void        (APIENTRYP PFNFSR2LINKPROGRAMPROC)            (GLuint);
typedef void        (APIENTRYP PFNFSR2GETPROGRAMIVPROC)           (GLuint, GLenum, GLint *);
typedef void        (APIENTRYP PFNFSR2GETPROGRAMINFOLOGPROC)      (GLuint, GLsizei, GLsizei *, GLchar *);
typedef void        (APIENTRYP PFNFSR2USEPROGRAMPROC)             (GLuint);
typedef void        (APIENTRYP PFNFSR2DELETEPROGRAMPROC)          (GLuint);
typedef GLint       (APIENTRYP PFNFSR2GETUNIFORMLOCATIONPROC)     (GLuint, const GLchar *);
typedef void        (APIENTRYP PFNFSR2UNIFORM1IPROC)              (GLint, GLint);
typedef void        (APIENTRYP PFNFSR2UNIFORM1FPROC)              (GLint, GLfloat);
typedef void        (APIENTRYP PFNFSR2UNIFORM2FPROC)              (GLint, GLfloat, GLfloat);
typedef void        (APIENTRYP PFNFSR2UNIFORMMATRIX4FVPROC)       (GLint, GLsizei, GLboolean, const GLfloat *);

static PFNFSR2GENFRAMEBUFFERSPROC        fsr2_glGenFramebuffers;
static PFNFSR2DELETEFRAMEBUFFERSPROC     fsr2_glDeleteFramebuffers;
static PFNFSR2BINDFRAMEBUFFERPROC        fsr2_glBindFramebuffer;
static PFNFSR2FRAMEBUFFERTEXTURE2DPROC   fsr2_glFramebufferTexture2D;
static PFNFSR2GENRENDERBUFFERSPROC       fsr2_glGenRenderbuffers;
static PFNFSR2BINDRENDERBUFFERPROC       fsr2_glBindRenderbuffer;
static PFNFSR2RENDERBUFFERSTORAGEPROC    fsr2_glRenderbufferStorage;
static PFNFSR2FRAMEBUFFERRENDERBUFFERPROC fsr2_glFramebufferRenderbuffer;
static PFNFSR2DELETERENDERBUFFERSPROC    fsr2_glDeleteRenderbuffers;
static PFNFSR2CHECKFRAMEBUFFERSTATUSPROC fsr2_glCheckFramebufferStatus;
static PFNFSR2DRAWBUFFERSPROC            fsr2_glDrawBuffers;
static PFNFSR2BLITFRAMEBUFFERPROC        fsr2_glBlitFramebuffer;

static PFNFSR2CREATESHADERPROC           fsr2_glCreateShader;
static PFNFSR2SHADERSOURCEPROC           fsr2_glShaderSource;
static PFNFSR2COMPILESHADERPROC          fsr2_glCompileShader;
static PFNFSR2GETSHADERIVPROC            fsr2_glGetShaderiv;
static PFNFSR2GETSHADERINFOLOGPROC       fsr2_glGetShaderInfoLog;
static PFNFSR2DELETESHADERPROC           fsr2_glDeleteShader;
static PFNFSR2CREATEPROGRAMPROC          fsr2_glCreateProgram;
static PFNFSR2ATTACHSHADERPROC           fsr2_glAttachShader;
static PFNFSR2LINKPROGRAMPROC            fsr2_glLinkProgram;
static PFNFSR2GETPROGRAMIVPROC           fsr2_glGetProgramiv;
static PFNFSR2GETPROGRAMINFOLOGPROC      fsr2_glGetProgramInfoLog;
static PFNFSR2USEPROGRAMPROC             fsr2_glUseProgram;
static PFNFSR2DELETEPROGRAMPROC          fsr2_glDeleteProgram;
static PFNFSR2GETUNIFORMLOCATIONPROC     fsr2_glGetUniformLocation;
static PFNFSR2UNIFORM1IPROC              fsr2_glUniform1i;
static PFNFSR2UNIFORM1FPROC              fsr2_glUniform1f;
static PFNFSR2UNIFORM2FPROC              fsr2_glUniform2f;
static PFNFSR2UNIFORMMATRIX4FVPROC       fsr2_glUniformMatrix4fv;

#ifndef GL_FRAMEBUFFER
# define GL_FRAMEBUFFER                  0x8D40
# define GL_READ_FRAMEBUFFER             0x8CA8
# define GL_DRAW_FRAMEBUFFER             0x8CA9
# define GL_COLOR_ATTACHMENT0            0x8CE0
# define GL_COLOR_ATTACHMENT1            0x8CE1
# define GL_COLOR_ATTACHMENT2            0x8CE2
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
# define GL_R8                           0x8229
#endif

#ifndef GL_HALF_FLOAT
# define GL_HALF_FLOAT                   0x140B
#endif

extern idCVar r_fsr2;
extern idCVar r_fsr2Sharpness;

fsr2State_t fsr2;

static const float fsr2Jitter[16][2] = {
	{ 0.5f, 0.33333333f },
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

static const float fsr2ScaleFactors[5] = {
	1.0f,
	1.0f / 0.67f,   // 1: Quality (67% render scale, 1.5x upscale)
	1.0f / 0.59f,   // 2: Balanced (59% render scale, 1.7x upscale)
	1.0f / 0.50f,   // 3: Performance (50% render scale, 2.0x upscale)
	1.0f / 0.37f    // 4: Ultra Performance (37% render scale, 2.7x upscale)
};

static bool FSR2_LoadExtensionPointers( void ) {
#define LOAD_PROC(type, name) \
	fsr2_gl##name = (type) GLimp_ExtensionPointer( "gl" #name ); \
	if ( !fsr2_gl##name ) { common->Warning( "FSR2: gl" #name " not found" ); return false; }

	LOAD_PROC( PFNFSR2GENFRAMEBUFFERSPROC,         GenFramebuffers         )
	LOAD_PROC( PFNFSR2DELETEFRAMEBUFFERSPROC,       DeleteFramebuffers      )
	LOAD_PROC( PFNFSR2BINDFRAMEBUFFERPROC,          BindFramebuffer         )
	LOAD_PROC( PFNFSR2FRAMEBUFFERTEXTURE2DPROC,     FramebufferTexture2D    )
	LOAD_PROC( PFNFSR2GENRENDERBUFFERSPROC,         GenRenderbuffers        )
	LOAD_PROC( PFNFSR2BINDRENDERBUFFERPROC,         BindRenderbuffer        )
	LOAD_PROC( PFNFSR2RENDERBUFFERSTORAGEPROC,      RenderbufferStorage     )
	LOAD_PROC( PFNFSR2FRAMEBUFFERRENDERBUFFERPROC,  FramebufferRenderbuffer )
	LOAD_PROC( PFNFSR2DELETERENDERBUFFERSPROC,      DeleteRenderbuffers     )
	LOAD_PROC( PFNFSR2CHECKFRAMEBUFFERSTATUSPROC,   CheckFramebufferStatus  )
	LOAD_PROC( PFNFSR2DRAWBUFFERSPROC,              DrawBuffers             )
	LOAD_PROC( PFNFSR2BLITFRAMEBUFFERPROC,          BlitFramebuffer         )
	LOAD_PROC( PFNFSR2CREATESHADERPROC,             CreateShader            )
	LOAD_PROC( PFNFSR2SHADERSOURCEPROC,             ShaderSource            )
	LOAD_PROC( PFNFSR2COMPILESHADERPROC,            CompileShader           )
	LOAD_PROC( PFNFSR2GETSHADERIVPROC,              GetShaderiv             )
	LOAD_PROC( PFNFSR2GETSHADERINFOLOGPROC,         GetShaderInfoLog        )
	LOAD_PROC( PFNFSR2DELETESHADERPROC,             DeleteShader            )
	LOAD_PROC( PFNFSR2CREATEPROGRAMPROC,            CreateProgram           )
	LOAD_PROC( PFNFSR2ATTACHSHADERPROC,             AttachShader            )
	LOAD_PROC( PFNFSR2LINKPROGRAMPROC,              LinkProgram             )
	LOAD_PROC( PFNFSR2GETPROGRAMIVPROC,             GetProgramiv            )
	LOAD_PROC( PFNFSR2GETPROGRAMINFOLOGPROC,        GetProgramInfoLog       )
	LOAD_PROC( PFNFSR2USEPROGRAMPROC,               UseProgram              )
	LOAD_PROC( PFNFSR2DELETEPROGRAMPROC,            DeleteProgram           )
	LOAD_PROC( PFNFSR2GETUNIFORMLOCATIONPROC,       GetUniformLocation      )
	LOAD_PROC( PFNFSR2UNIFORM1IPROC,                Uniform1i               )
	LOAD_PROC( PFNFSR2UNIFORM1FPROC,                Uniform1f               )
	LOAD_PROC( PFNFSR2UNIFORM2FPROC,                Uniform2f               )
	LOAD_PROC( PFNFSR2UNIFORMMATRIX4FVPROC,         UniformMatrix4fv        )
#undef LOAD_PROC
	return true;
}

static GLuint FSR2_CompileShader( GLenum type, const GLchar **sources, GLsizei count ) {
	GLuint shader = fsr2_glCreateShader( type );
	if ( !shader ) return 0;

	fsr2_glShaderSource( shader, count, sources, NULL );
	fsr2_glCompileShader( shader );

	GLint status = 0;
	fsr2_glGetShaderiv( shader, GL_COMPILE_STATUS, &status );
	if ( !status ) {
		GLint logLen = 0;
		fsr2_glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &logLen );
		if ( logLen > 0 ) {
			char *log = (char *)Mem_Alloc( logLen );
			fsr2_glGetShaderInfoLog( shader, logLen, NULL, log );
			common->Warning( "FSR2 shader compile error:\n%s", log );
			Mem_Free( log );
		}
		fsr2_glDeleteShader( shader );
		return 0;
	}
	return shader;
}

static GLuint FSR2_CompileProgram( GLuint vertShader, GLuint fragShader ) {
	GLuint prog = fsr2_glCreateProgram();
	if ( !prog ) return 0;

	fsr2_glAttachShader( prog, vertShader );
	fsr2_glAttachShader( prog, fragShader );
	fsr2_glLinkProgram( prog );

	GLint status = 0;
	fsr2_glGetProgramiv( prog, GL_LINK_STATUS, &status );
	if ( !status ) {
		GLint logLen = 0;
		fsr2_glGetProgramiv( prog, GL_INFO_LOG_LENGTH, &logLen );
		if ( logLen > 0 ) {
			char *log = (char *)Mem_Alloc( logLen );
			fsr2_glGetProgramInfoLog( prog, logLen, NULL, log );
			common->Warning( "FSR2 program link error:\n%s", log );
			Mem_Free( log );
		}
		fsr2_glDeleteProgram( prog );
		return 0;
	}
	return prog;
}

static bool FSR2_CompileShaders( void ) {
	const GLchar *vertSrcs[1] = { fsr2VertSrc };
	GLuint vertShader = FSR2_CompileShader( GL_VERTEX_SHADER, vertSrcs, 1 );
	if ( !vertShader ) return false;

	const GLchar *depthClipSrcs[1] = { fsr2DepthClipFragSrc };
	GLuint depthClipFrag = FSR2_CompileShader( GL_FRAGMENT_SHADER, depthClipSrcs, 1 );
	if ( !depthClipFrag ) { fsr2_glDeleteShader( vertShader ); return false; }

	fsr2.depthClipProg = FSR2_CompileProgram( vertShader, depthClipFrag );
	fsr2_glDeleteShader( depthClipFrag );
	if ( !fsr2.depthClipProg ) { fsr2_glDeleteShader( vertShader ); return false; }

	const GLchar *accumSrcs[1] = { fsr2AccumulateFragSrc };
	GLuint accumFrag = FSR2_CompileShader( GL_FRAGMENT_SHADER, accumSrcs, 1 );
	if ( !accumFrag ) { fsr2_glDeleteShader( vertShader ); return false; }

	fsr2.accumulateProg = FSR2_CompileProgram( vertShader, accumFrag );
	fsr2_glDeleteShader( accumFrag );
	if ( !fsr2.accumulateProg ) { fsr2_glDeleteShader( vertShader ); return false; }

	const GLchar *rcasSrcs[1] = { fsr2RcasFragSrc };
	GLuint rcasFrag = FSR2_CompileShader( GL_FRAGMENT_SHADER, rcasSrcs, 1 );
	if ( !rcasFrag ) { fsr2_glDeleteShader( vertShader ); return false; }

	fsr2.rcasProg = FSR2_CompileProgram( vertShader, rcasFrag );
	fsr2_glDeleteShader( rcasFrag );
	fsr2_glDeleteShader( vertShader );
	if ( !fsr2.rcasProg ) return false;

	fsr2.depthClipDepthLoc = fsr2_glGetUniformLocation( fsr2.depthClipProg, "depthTexture" );
	fsr2.depthClipPrevDepthLoc = fsr2_glGetUniformLocation( fsr2.depthClipProg, "prevDepthTexture" );
	fsr2.depthClipMotionLoc = fsr2_glGetUniformLocation( fsr2.depthClipProg, "motionTexture" );
	fsr2.depthClipInputSizeLoc = fsr2_glGetUniformLocation( fsr2.depthClipProg, "inputSize" );
	fsr2.depthClipJitterLoc = fsr2_glGetUniformLocation( fsr2.depthClipProg, "jitter" );

	fsr2.accumCurrColorLoc = fsr2_glGetUniformLocation( fsr2.accumulateProg, "currentColor" );
	fsr2.accumHistoryLoc = fsr2_glGetUniformLocation( fsr2.accumulateProg, "historyColor" );
	fsr2.accumMotionLoc = fsr2_glGetUniformLocation( fsr2.accumulateProg, "motionTexture" );
	fsr2.accumDepthLoc = fsr2_glGetUniformLocation( fsr2.accumulateProg, "depthTexture" );
	fsr2.accumLockStatusLoc = fsr2_glGetUniformLocation( fsr2.accumulateProg, "lockStatusTexture" );
	fsr2.accumInputSizeLoc = fsr2_glGetUniformLocation( fsr2.accumulateProg, "inputSize" );
	fsr2.accumDisplaySizeLoc = fsr2_glGetUniformLocation( fsr2.accumulateProg, "displaySize" );
	fsr2.accumJitterLoc = fsr2_glGetUniformLocation( fsr2.accumulateProg, "jitter" );
	fsr2.accumFrameIndexLoc = fsr2_glGetUniformLocation( fsr2.accumulateProg, "frameIndex" );

	fsr2.rcasTexLoc = fsr2_glGetUniformLocation( fsr2.rcasProg, "inputTexture" );
	fsr2.rcasSharpnessLoc = fsr2_glGetUniformLocation( fsr2.rcasProg, "sharpness" );

	common->Printf( "...FSR 2.0 shaders compiled (depthClip=%u, accum=%u, rcas=%u)\n",
		fsr2.depthClipProg, fsr2.accumulateProg, fsr2.rcasProg );
	return true;
}

static bool FSR2_CreateFBOs( void ) {
	qglGenTextures( 1, &fsr2.sceneColorTex );
	qglBindTexture( GL_TEXTURE_2D, fsr2.sceneColorTex );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, fsr2.inputWidth, fsr2.inputHeight,
		0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglBindTexture( GL_TEXTURE_2D, 0 );

	qglGenTextures( 1, &fsr2.sceneDepthTex );
	qglBindTexture( GL_TEXTURE_2D, fsr2.sceneDepthTex );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, fsr2.inputWidth, fsr2.inputHeight,
		0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglBindTexture( GL_TEXTURE_2D, 0 );

	qglGenTextures( 1, &fsr2.velocityTex );
	qglBindTexture( GL_TEXTURE_2D, fsr2.velocityTex );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RG16F, fsr2.inputWidth, fsr2.inputHeight,
		0, GL_RG, GL_HALF_FLOAT, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglBindTexture( GL_TEXTURE_2D, 0 );

	fsr2_glGenFramebuffers( 1, &fsr2.sceneFBO );
	fsr2_glBindFramebuffer( GL_FRAMEBUFFER, fsr2.sceneFBO );
	fsr2_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fsr2.sceneColorTex, 0 );
	fsr2_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, fsr2.velocityTex, 0 );
	fsr2_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, fsr2.sceneDepthTex, 0 );

	if ( fsr2_glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
		common->Warning( "FSR2: scene FBO incomplete" );
		fsr2_glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		return false;
	}
	fsr2_glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	qglGenTextures( 1, &fsr2.historyColorTex );
	qglBindTexture( GL_TEXTURE_2D, fsr2.historyColorTex );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, fsr2.displayWidth, fsr2.displayHeight,
		0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglBindTexture( GL_TEXTURE_2D, 0 );

	for ( int i = 0; i < 2; i++ ) {
		qglGenTextures( 1, &fsr2.lockStatusTex[i] );
		qglBindTexture( GL_TEXTURE_2D, fsr2.lockStatusTex[i] );
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_R8, fsr2.displayWidth, fsr2.displayHeight,
			0, GL_RED, GL_UNSIGNED_BYTE, NULL );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	}
	qglBindTexture( GL_TEXTURE_2D, 0 );

	fsr2_glGenFramebuffers( 1, &fsr2.historyFBO );
	fsr2_glBindFramebuffer( GL_FRAMEBUFFER, fsr2.historyFBO );
	fsr2_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fsr2.historyColorTex, 0 );
	if ( fsr2_glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
		common->Warning( "FSR2: history FBO incomplete" );
		fsr2_glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		return false;
	}
	fsr2_glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	qglGenTextures( 1, &fsr2.outputColorTex );
	qglBindTexture( GL_TEXTURE_2D, fsr2.outputColorTex );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, fsr2.displayWidth, fsr2.displayHeight,
		0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglBindTexture( GL_TEXTURE_2D, 0 );

	fsr2_glGenFramebuffers( 1, &fsr2.outputFBO );
	fsr2_glBindFramebuffer( GL_FRAMEBUFFER, fsr2.outputFBO );
	fsr2_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fsr2.outputColorTex, 0 );
	if ( fsr2_glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
		common->Warning( "FSR2: output FBO incomplete" );
		fsr2_glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		return false;
	}
	fsr2_glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	return true;
}

static void FSR2_DeleteFBOs( void ) {
	if ( fsr2.outputFBO )        { fsr2_glDeleteFramebuffers( 1, &fsr2.outputFBO );        fsr2.outputFBO = 0; }
	if ( fsr2.outputColorTex )   { qglDeleteTextures( 1, &fsr2.outputColorTex );          fsr2.outputColorTex = 0; }
	if ( fsr2.historyFBO )       { fsr2_glDeleteFramebuffers( 1, &fsr2.historyFBO );       fsr2.historyFBO = 0; }
	for ( int i = 0; i < 2; i++ ) {
		if ( fsr2.lockStatusTex[i] ) { qglDeleteTextures( 1, &fsr2.lockStatusTex[i] );    fsr2.lockStatusTex[i] = 0; }
	}
	if ( fsr2.historyColorTex )  { qglDeleteTextures( 1, &fsr2.historyColorTex );         fsr2.historyColorTex = 0; }
	if ( fsr2.sceneFBO )         { fsr2_glDeleteFramebuffers( 1, &fsr2.sceneFBO );         fsr2.sceneFBO = 0; }
	if ( fsr2.velocityTex )      { qglDeleteTextures( 1, &fsr2.velocityTex );             fsr2.velocityTex = 0; }
	if ( fsr2.sceneDepthTex )    { qglDeleteTextures( 1, &fsr2.sceneDepthTex );           fsr2.sceneDepthTex = 0; }
	if ( fsr2.sceneColorTex )    { qglDeleteTextures( 1, &fsr2.sceneColorTex );           fsr2.sceneColorTex = 0; }
}

static void FSR2_DeleteShaders( void ) {
	if ( fsr2.rcasProg )        { fsr2_glDeleteProgram( fsr2.rcasProg );        fsr2.rcasProg = 0; }
	if ( fsr2.accumulateProg )  { fsr2_glDeleteProgram( fsr2.accumulateProg );  fsr2.accumulateProg = 0; }
	if ( fsr2.depthClipProg )   { fsr2_glDeleteProgram( fsr2.depthClipProg );   fsr2.depthClipProg = 0; }
}

void FSR2_Init( void ) {
	memset( &fsr2, 0, sizeof( fsr2 ) );

	if ( !glConfig.fsrAvailable ) {
		common->Printf( "FSR2: disabled (OpenGL 4.0 not available)\n" );
		return;
	}

	if ( !FSR2_LoadExtensionPointers() ) {
		common->Warning( "FSR2: failed to load GL extension pointers" );
		return;
	}

	if ( !FSR2_CompileShaders() ) {
		common->Warning( "FSR2: shader compilation failed" );
		return;
	}

	fsr2.available = true;
	fsr2.frameIndex = 0;
	fsr2.lockStatusIndex = 0;
	common->Printf( "FSR2: initialized (GL 4.0)\n" );
}

void FSR2_Shutdown( void ) {
	FSR2_DeleteFBOs();
	if ( fsr2_glDeleteProgram ) {
		FSR2_DeleteShaders();
	}
	memset( &fsr2, 0, sizeof( fsr2 ) );
}

void FSR2_Reinit( void ) {
	if ( !fsr2.available ) return;

	FSR2_DeleteFBOs();
	fsr2.active = false;

	if ( r_fsr2.GetInteger() < 1 || r_fsr2.GetInteger() > 4 ) {
		return;
	}

	// Use tr.origWidth/origHeight which always contains the display size,
	// not glConfig.vidWidth/vidHeight which may have been overridden by FSR2.
	fsr2.displayWidth  = tr.origWidth;
	fsr2.displayHeight = tr.origHeight;

	// Fallback if origWidth/origHeight are not valid yet
	if ( fsr2.displayWidth <= 0 || fsr2.displayHeight <= 0 ) {
		fsr2.displayWidth  = glConfig.vidWidth;
		fsr2.displayHeight = glConfig.vidHeight;
	}

	int preset = r_fsr2.GetInteger();
	if ( preset < 1 ) preset = 1;
	if ( preset > 4 ) preset = 4;
	float scale = fsr2ScaleFactors[preset];

	fsr2.inputWidth  = int( float( fsr2.displayWidth  ) / scale + 0.5f );
	fsr2.inputHeight = int( float( fsr2.displayHeight ) / scale + 0.5f );

	if ( fsr2.inputWidth  < 64 ) fsr2.inputWidth  = 64;
	if ( fsr2.inputHeight < 64 ) fsr2.inputHeight = 64;

	if ( !FSR2_CreateFBOs() ) {
		fsr2.available = false;
		return;
	}

	fsr2.active = true;
	fsr2.frameIndex = 0;
	common->Printf( "FSR2: active at %dx%d -> %dx%d (preset %d)\n",
		fsr2.inputWidth, fsr2.inputHeight, fsr2.displayWidth, fsr2.displayHeight, preset );
}

void FSR2_CheckCvars( void ) {
	if ( !fsr2.available ) return;

	if ( r_fsr2.IsModified() || r_fsr2Sharpness.IsModified() ) {
		r_fsr2.ClearModified();
		r_fsr2Sharpness.ClearModified();
		FSR2_Reinit();
	}
}

void FSR2_BeginScene( void ) {
	fsr2_glBindFramebuffer( GL_FRAMEBUFFER, fsr2.sceneFBO );

	GLuint buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	fsr2_glDrawBuffers( 2, buffers );

	qglViewport( 0, 0, fsr2.inputWidth, fsr2.inputHeight );
	if ( r_useScissor.GetBool() ) {
		qglScissor( 0, 0, fsr2.inputWidth, fsr2.inputHeight );
	}

	qglClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	qglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

	fsr2.frameIndex = ( fsr2.frameIndex + 1 ) % 16;
	fsr2.jitterX = ( fsr2Jitter[fsr2.frameIndex][0] - 0.5f ) / (float)fsr2.inputWidth;
	fsr2.jitterY = ( fsr2Jitter[fsr2.frameIndex][1] - 0.5f ) / (float)fsr2.inputHeight;
}

static void FSR2_VelocityPass( void ) {
	fsr2_glBindFramebuffer( GL_FRAMEBUFFER, fsr2.sceneFBO );
	qglDrawBuffer( GL_COLOR_ATTACHMENT1 );

	if ( !taa.havePrevMatrices ) {
		qglClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		qglClear( GL_COLOR_BUFFER_BIT );
		qglDrawBuffer( GL_COLOR_ATTACHMENT0 );
		return;
	}

	qglViewport( 0, 0, fsr2.inputWidth, fsr2.inputHeight );

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

	GLuint depthProg = taa.velocityProg;
	fsr2_glUseProgram( depthProg );

	qglActiveTextureARB( GL_TEXTURE0_ARB );
	qglBindTexture( GL_TEXTURE_2D, fsr2.sceneDepthTex );
	fsr2_glUniform1i( taa.velDepthLoc, 0 );

	fsr2_glUniformMatrix4fv( taa.velPrevViewLoc, 1, GL_FALSE, taa.prevViewMatrix );
	fsr2_glUniformMatrix4fv( taa.velPrevProjLoc, 1, GL_FALSE, taa.prevProjectionMatrix );
	fsr2_glUniformMatrix4fv( taa.velInvProjLoc, 1, GL_FALSE, invProj );
	fsr2_glUniform2f( taa.velWidthLoc, (float)fsr2.inputWidth, (float)fsr2.inputHeight );

	qglBegin( GL_TRIANGLE_STRIP );
		qglVertex2f( -1.0f, -1.0f );
		qglVertex2f(  1.0f, -1.0f );
		qglVertex2f( -1.0f,  1.0f );
		qglVertex2f(  1.0f,  1.0f );
	qglEnd();

	fsr2_glUseProgram( 0 );
	qglDrawBuffer( GL_COLOR_ATTACHMENT0 );
}

void FSR2_EndScene( void ) {
	FSR2_VelocityPass();

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

	fsr2_glBindFramebuffer( GL_FRAMEBUFFER, fsr2.outputFBO );
	qglViewport( 0, 0, fsr2.displayWidth, fsr2.displayHeight );

	fsr2_glUseProgram( fsr2.accumulateProg );

	qglActiveTextureARB( GL_TEXTURE0_ARB );
	qglBindTexture( GL_TEXTURE_2D, fsr2.sceneColorTex );
	fsr2_glUniform1i( fsr2.accumCurrColorLoc, 0 );

	qglActiveTextureARB( GL_TEXTURE1_ARB );
	qglBindTexture( GL_TEXTURE_2D, fsr2.historyColorTex );
	fsr2_glUniform1i( fsr2.accumHistoryLoc, 1 );

	qglActiveTextureARB( GL_TEXTURE2_ARB );
	qglBindTexture( GL_TEXTURE_2D, fsr2.velocityTex );
	fsr2_glUniform1i( fsr2.accumMotionLoc, 2 );

	qglActiveTextureARB( GL_TEXTURE3_ARB );
	qglBindTexture( GL_TEXTURE_2D, fsr2.sceneDepthTex );
	fsr2_glUniform1i( fsr2.accumDepthLoc, 3 );

	qglActiveTextureARB( GL_TEXTURE4_ARB );
	qglBindTexture( GL_TEXTURE_2D, fsr2.lockStatusTex[fsr2.lockStatusIndex] );
	fsr2_glUniform1i( fsr2.accumLockStatusLoc, 4 );

	fsr2_glUniform2f( fsr2.accumInputSizeLoc, (float)fsr2.inputWidth, (float)fsr2.inputHeight );
	fsr2_glUniform2f( fsr2.accumDisplaySizeLoc, (float)fsr2.displayWidth, (float)fsr2.displayHeight );
	fsr2_glUniform2f( fsr2.accumJitterLoc, fsr2.jitterX * fsr2.inputWidth, fsr2.jitterY * fsr2.inputHeight );
	fsr2_glUniform1i( fsr2.accumFrameIndexLoc, fsr2.frameIndex );

	qglBegin( GL_TRIANGLE_STRIP );
		qglVertex2f( -1.0f, -1.0f );
		qglVertex2f(  1.0f, -1.0f );
		qglVertex2f( -1.0f,  1.0f );
		qglVertex2f(  1.0f,  1.0f );
	qglEnd();

	fsr2_glBindFramebuffer( GL_READ_FRAMEBUFFER, fsr2.outputFBO );
	fsr2_glBindFramebuffer( GL_DRAW_FRAMEBUFFER, fsr2.historyFBO );
	fsr2_glBlitFramebuffer( 0, 0, fsr2.displayWidth, fsr2.displayHeight,
		0, 0, fsr2.displayWidth, fsr2.displayHeight,
		GL_COLOR_BUFFER_BIT, GL_NEAREST );

	bool rcasEnabled = ( r_fsr2Sharpness.GetFloat() >= 0.0f );

	if ( rcasEnabled ) {
		fsr2_glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		qglViewport( 0, 0, fsr2.displayWidth, fsr2.displayHeight );

		fsr2_glUseProgram( fsr2.rcasProg );

		qglActiveTextureARB( GL_TEXTURE0_ARB );
		qglBindTexture( GL_TEXTURE_2D, fsr2.outputColorTex );
		fsr2_glUniform1i( fsr2.rcasTexLoc, 0 );
		fsr2_glUniform1f( fsr2.rcasSharpnessLoc, r_fsr2Sharpness.GetFloat() );

		qglBegin( GL_TRIANGLE_STRIP );
			qglVertex2f( -1.0f, -1.0f );
			qglVertex2f(  1.0f, -1.0f );
			qglVertex2f( -1.0f,  1.0f );
			qglVertex2f(  1.0f,  1.0f );
		qglEnd();
	} else {
		fsr2_glBindFramebuffer( GL_READ_FRAMEBUFFER, fsr2.outputFBO );
		fsr2_glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
		fsr2_glBlitFramebuffer( 0, 0, fsr2.displayWidth, fsr2.displayHeight,
			0, 0, fsr2.displayWidth, fsr2.displayHeight,
			GL_COLOR_BUFFER_BIT, GL_NEAREST );
	}

	fsr2_glUseProgram( 0 );
	qglBindTexture( GL_TEXTURE_2D, 0 );
	fsr2_glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	fsr2.lockStatusIndex = 1 - fsr2.lockStatusIndex;

	// Restore glConfig dimensions so RB_SetDefaultGLState sets the scissor correctly
	// and subsequent 2D rendering uses display resolution.
	glConfig.vidWidth  = fsr2.displayWidth;
	glConfig.vidHeight = fsr2.displayHeight;

	// Full engine state reset: wipes backEnd.glState, re-establishes all GL defaults
	// (texture units, client arrays, depth/blend/cull, etc.).  This is the only safe
	// way to clean up after raw qgl* / GLSL calls that bypassed the engine's state
	// tracker — the same reset the engine performs at the very start of each frame.
	RB_SetDefaultGLState();
}

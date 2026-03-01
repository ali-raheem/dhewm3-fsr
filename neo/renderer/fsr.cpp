/*
===========================================================================

dhewm3 FSR 1.0 upscaling support
AMD FidelityFX Super Resolution 1.0

This module adds FSR 1.0 (Edge-Adaptive Spatial Upsampling + Robust
Contrast-Adaptive Sharpening) to dhewm3.  The 3D scene is rendered to
an off-screen FBO at a reduced internal resolution; EASU and RCAS
GLSL passes then upscale to the display resolution.  The 2D HUD/UI
continues to render at full display resolution and is unaffected.

Requires OpenGL 4.0 (for GLSL 4.00 and textureGather).

===========================================================================
*/

#include "sys/platform.h"
#include "renderer/tr_local.h"   // glConfig, qgl*, backEnd, etc.
#include "renderer/fsr.h"
#include "renderer/taa.h"

// -------------------------------------------------------------------------
// CPU-side AMD FSR header includes (for FsrEasuCon / FsrRcasCon math)
// The AMD headers define many helper functions unused in the CPU path;
// suppress the resulting -Wunused-function warnings.
// -------------------------------------------------------------------------
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define A_CPU 1
#include "renderer/ffx_a.h"
#include "renderer/ffx_fsr1.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

// -------------------------------------------------------------------------
// GLSL shader source strings (preamble + body; AMD headers come from the
// CMake-generated fsr_embedded.h which embeds ffx_a.h / ffx_fsr1.h verbatim)
// -------------------------------------------------------------------------
#include "renderer/fsr_glsl.h"

// fsr_embedded.h is generated at build time by CMake's configure_file:
//   cmake reads ffx_a.h and ffx_fsr1.h and embeds them as C++ raw string
//   literals (ffxAGlslSrc, ffxFsr1GlslSrc).
#include "fsr_embedded.h"  // from ${CMAKE_BINARY_DIR}

// -------------------------------------------------------------------------
// CVars (declared in RenderSystem_init.cpp, extern'd here)
// -------------------------------------------------------------------------
extern idCVar r_fsr;
extern idCVar r_fsrSharpness;
extern idCVar r_taa;

// -------------------------------------------------------------------------
// Global FSR state
// -------------------------------------------------------------------------
fsrState_t fsr;

// -------------------------------------------------------------------------
// GL function pointers for FBO and GLSL (loaded via GLimp_ExtensionPointer)
// Named fsr_gl* to avoid clashing with qgl* wrappers.
// -------------------------------------------------------------------------
typedef void        (APIENTRYP PFNFSRGENFRAMEBUFFERSPROC)   (GLsizei, GLuint *);
typedef void        (APIENTRYP PFNFSRDELETEFRAMEBUFFERSPROC)(GLsizei, const GLuint *);
typedef void        (APIENTRYP PFNFSRBINDFRAMEBUFFERPROC)    (GLenum, GLuint);
typedef void        (APIENTRYP PFNFSRFRAMEBUFFERTEXTURE2DPROC)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void        (APIENTRYP PFNFSRGENRENDERBUFFERSPROC)   (GLsizei, GLuint *);
typedef void        (APIENTRYP PFNFSRBINDRENDERBUFFERPROC)   (GLenum, GLuint);
typedef void        (APIENTRYP PFNFSRRENDERBUFFERSTORAGEPROC)(GLenum, GLenum, GLsizei, GLsizei);
typedef void        (APIENTRYP PFNFSRFRAMEBUFFERRENDERBUFFERPROC)(GLenum, GLenum, GLenum, GLuint);
typedef void        (APIENTRYP PFNFSRDELETERENDERBUFFERSPROC)(GLsizei, const GLuint *);
typedef GLenum      (APIENTRYP PFNFSRCHECKFRAMEBUFFERSTATUSPROC)(GLenum);

typedef GLuint      (APIENTRYP PFNFSRCREATESHADERPROC)      (GLenum);
typedef void        (APIENTRYP PFNFSRSHADERSOURCEPROC)       (GLuint, GLsizei, const GLchar *const*, const GLint *);
typedef void        (APIENTRYP PFNFSRCOMPILESHADERPROC)      (GLuint);
typedef void        (APIENTRYP PFNFSRGETSHADERIVPROC)        (GLuint, GLenum, GLint *);
typedef void        (APIENTRYP PFNFSRGETSHADERINFOLOGPROC)   (GLuint, GLsizei, GLsizei *, GLchar *);
typedef void        (APIENTRYP PFNFSRDELETESHADERPROC)       (GLuint);
typedef GLuint      (APIENTRYP PFNFSRCREATEPROGRAMPROC)      (void);
typedef void        (APIENTRYP PFNFSRATTACHSHADERPROC)       (GLuint, GLuint);
typedef void        (APIENTRYP PFNFSRLINKPROGRAMPROC)        (GLuint);
typedef void        (APIENTRYP PFNFSRGETPROGRAMIVPROC)       (GLuint, GLenum, GLint *);
typedef void        (APIENTRYP PFNFSRGETPROGRAMINFOLOGPROC)  (GLuint, GLsizei, GLsizei *, GLchar *);
typedef void        (APIENTRYP PFNFSRUSEPROGRAMPROC)         (GLuint);
typedef void        (APIENTRYP PFNFSRDELETEPROGRAMPROC)      (GLuint);
typedef GLint       (APIENTRYP PFNFSRGETUNIFORMLOCATIONPROC) (GLuint, const GLchar *);
typedef void        (APIENTRYP PFNFSRUNIFORM1IPROC)          (GLint, GLint);
typedef void        (APIENTRYP PFNFSRUNIFORM4FVPROC)         (GLint, GLsizei, const GLfloat *);
typedef void        (APIENTRYP PFNFSRUNIFORM4UIVPROC)        (GLint, GLsizei, const GLuint *);
typedef void        (APIENTRYP PFNFSRDRAWBUFFERSPROC)        (GLsizei, const GLenum *);

static PFNFSRGENFRAMEBUFFERSPROC        fsr_glGenFramebuffers;
static PFNFSRDELETEFRAMEBUFFERSPROC     fsr_glDeleteFramebuffers;
static PFNFSRBINDFRAMEBUFFERPROC        fsr_glBindFramebuffer;
static PFNFSRFRAMEBUFFERTEXTURE2DPROC   fsr_glFramebufferTexture2D;
static PFNFSRGENRENDERBUFFERSPROC       fsr_glGenRenderbuffers;
static PFNFSRBINDRENDERBUFFERPROC       fsr_glBindRenderbuffer;
static PFNFSRRENDERBUFFERSTORAGEPROC    fsr_glRenderbufferStorage;
static PFNFSRFRAMEBUFFERRENDERBUFFERPROC fsr_glFramebufferRenderbuffer;
static PFNFSRDELETERENDERBUFFERSPROC    fsr_glDeleteRenderbuffers;
static PFNFSRCHECKFRAMEBUFFERSTATUSPROC fsr_glCheckFramebufferStatus;
static PFNFSRDRAWBUFFERSPROC            fsr_glDrawBuffers;

static PFNFSRCREATESHADERPROC           fsr_glCreateShader;
static PFNFSRSHADERSOURCEPROC           fsr_glShaderSource;
static PFNFSRCOMPILESHADERPROC          fsr_glCompileShader;
static PFNFSRGETSHADERIVPROC            fsr_glGetShaderiv;
static PFNFSRGETSHADERINFOLOGPROC       fsr_glGetShaderInfoLog;
static PFNFSRDELETESHADERPROC           fsr_glDeleteShader;
static PFNFSRCREATEPROGRAMPROC          fsr_glCreateProgram;
static PFNFSRATTACHSHADERPROC           fsr_glAttachShader;
static PFNFSRLINKPROGRAMPROC            fsr_glLinkProgram;
static PFNFSRGETPROGRAMIVPROC           fsr_glGetProgramiv;
static PFNFSRGETPROGRAMINFOLOGPROC      fsr_glGetProgramInfoLog;
static PFNFSRUSEPROGRAMPROC             fsr_glUseProgram;
static PFNFSRDELETEPROGRAMPROC          fsr_glDeleteProgram;
static PFNFSRGETUNIFORMLOCATIONPROC     fsr_glGetUniformLocation;
static PFNFSRUNIFORM1IPROC              fsr_glUniform1i;
static PFNFSRUNIFORM4FVPROC             fsr_glUniform4fv;
static PFNFSRUNIFORM4UIVPROC            fsr_glUniform4uiv;

// GL enumerants not defined in the old gl.h headers this engine uses
#ifndef GL_FRAMEBUFFER
# define GL_FRAMEBUFFER                  0x8D40
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
#endif

#ifndef GL_HALF_FLOAT
# define GL_HALF_FLOAT                   0x140B
#endif

// -------------------------------------------------------------------------
// Quality preset scale factors (rendered width / display width)
// Index: 0=off, 1=Ultra Quality (77%), 2=Quality (67%), 3=Balanced (59%),
//        4=Performance (50%)
// -------------------------------------------------------------------------
static const float fsrScales[5] = { 1.0f, 0.7692f, 0.6667f, 0.5882f, 0.5000f };

// -------------------------------------------------------------------------
// FSR_LoadExtensionPointers
// -------------------------------------------------------------------------
static bool FSR_LoadExtensionPointers( void ) {
#define LOAD_PROC(type, name) \
	fsr_gl##name = (type) GLimp_ExtensionPointer( "gl" #name ); \
	if ( !fsr_gl##name ) { common->Warning( "FSR: gl" #name " not found" ); return false; }

	LOAD_PROC( PFNFSRGENFRAMEBUFFERSPROC,         GenFramebuffers         )
	LOAD_PROC( PFNFSRDELETEFRAMEBUFFERSPROC,       DeleteFramebuffers      )
	LOAD_PROC( PFNFSRBINDFRAMEBUFFERPROC,          BindFramebuffer         )
	LOAD_PROC( PFNFSRFRAMEBUFFERTEXTURE2DPROC,     FramebufferTexture2D    )
	LOAD_PROC( PFNFSRGENRENDERBUFFERSPROC,         GenRenderbuffers        )
	LOAD_PROC( PFNFSRBINDRENDERBUFFERPROC,         BindRenderbuffer        )
	LOAD_PROC( PFNFSRRENDERBUFFERSTORAGEPROC,      RenderbufferStorage     )
	LOAD_PROC( PFNFSRFRAMEBUFFERRENDERBUFFERPROC,  FramebufferRenderbuffer )
	LOAD_PROC( PFNFSRDELETERENDERBUFFERSPROC,      DeleteRenderbuffers     )
	LOAD_PROC( PFNFSRCHECKFRAMEBUFFERSTATUSPROC,   CheckFramebufferStatus  )
	LOAD_PROC( PFNFSRDRAWBUFFERSPROC,              DrawBuffers             )
	LOAD_PROC( PFNFSRCREATESHADERPROC,             CreateShader            )
	LOAD_PROC( PFNFSRSHADERSOURCEPROC,             ShaderSource            )
	LOAD_PROC( PFNFSRCOMPILESHADERPROC,            CompileShader           )
	LOAD_PROC( PFNFSRGETSHADERIVPROC,              GetShaderiv             )
	LOAD_PROC( PFNFSRGETSHADERINFOLOGPROC,         GetShaderInfoLog        )
	LOAD_PROC( PFNFSRDELETESHADERPROC,             DeleteShader            )
	LOAD_PROC( PFNFSRCREATEPROGRAMPROC,            CreateProgram           )
	LOAD_PROC( PFNFSRATTACHSHADERPROC,             AttachShader            )
	LOAD_PROC( PFNFSRLINKPROGRAMPROC,              LinkProgram             )
	LOAD_PROC( PFNFSRGETPROGRAMIVPROC,             GetProgramiv            )
	LOAD_PROC( PFNFSRGETPROGRAMINFOLOGPROC,        GetProgramInfoLog       )
	LOAD_PROC( PFNFSRUSEPROGRAMPROC,               UseProgram              )
	LOAD_PROC( PFNFSRDELETEPROGRAMPROC,            DeleteProgram           )
	LOAD_PROC( PFNFSRGETUNIFORMLOCATIONPROC,       GetUniformLocation      )
	LOAD_PROC( PFNFSRUNIFORM1IPROC,                Uniform1i               )
	LOAD_PROC( PFNFSRUNIFORM4FVPROC,               Uniform4fv              )
	LOAD_PROC( PFNFSRUNIFORM4UIVPROC,              Uniform4uiv             )
#undef LOAD_PROC
	return true;
}

// -------------------------------------------------------------------------
// FSR_CompileShader
// -------------------------------------------------------------------------
static GLuint FSR_CompileShader( GLenum type, const GLchar **sources, GLsizei count ) {
	GLuint shader = fsr_glCreateShader( type );
	if ( !shader ) return 0;

	fsr_glShaderSource( shader, count, sources, NULL );
	fsr_glCompileShader( shader );

	GLint status = 0;
	fsr_glGetShaderiv( shader, GL_COMPILE_STATUS, &status );
	if ( !status ) {
		GLint logLen = 0;
		fsr_glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &logLen );
		if ( logLen > 0 ) {
			char *log = (char *)Mem_Alloc( logLen );
			fsr_glGetShaderInfoLog( shader, logLen, NULL, log );
			common->Warning( "FSR shader compile error:\n%s", log );
			Mem_Free( log );
		}
		fsr_glDeleteShader( shader );
		return 0;
	}
	return shader;
}

// -------------------------------------------------------------------------
// FSR_CompileProgram
// -------------------------------------------------------------------------
static GLuint FSR_CompileProgram( GLuint vertShader, GLuint fragShader ) {
	GLuint prog = fsr_glCreateProgram();
	if ( !prog ) return 0;

	fsr_glAttachShader( prog, vertShader );
	fsr_glAttachShader( prog, fragShader );
	fsr_glLinkProgram( prog );

	GLint status = 0;
	fsr_glGetProgramiv( prog, GL_LINK_STATUS, &status );
	if ( !status ) {
		GLint logLen = 0;
		fsr_glGetProgramiv( prog, GL_INFO_LOG_LENGTH, &logLen );
		if ( logLen > 0 ) {
			char *log = (char *)Mem_Alloc( logLen );
			fsr_glGetProgramInfoLog( prog, logLen, NULL, log );
			common->Warning( "FSR program link error:\n%s", log );
			Mem_Free( log );
		}
		fsr_glDeleteProgram( prog );
		return 0;
	}
	return prog;
}

// -------------------------------------------------------------------------
// FSR_CompileShaders — compile EASU and RCAS programs once
// Returns false on failure.
// -------------------------------------------------------------------------
static bool FSR_CompileShaders( void ) {
	// --- shared vertex shader ---
	const GLchar *vertSrcs[1] = { fsrVertSrc };
	GLuint vertShader = FSR_CompileShader( GL_VERTEX_SHADER, vertSrcs, 1 );
	if ( !vertShader ) return false;

	// --- EASU fragment shader (4 source parts) ---
	const GLchar *easuSrcs[4] = {
		fsrEasuPreamble,
		ffxAGlslSrc,
		ffxFsr1GlslSrc,
		fsrEasuBody
	};
	GLuint easuFrag = FSR_CompileShader( GL_FRAGMENT_SHADER, easuSrcs, 4 );
	if ( !easuFrag ) { fsr_glDeleteShader( vertShader ); return false; }

	fsr.easuProg = FSR_CompileProgram( vertShader, easuFrag );
	fsr_glDeleteShader( easuFrag );
	if ( !fsr.easuProg ) { fsr_glDeleteShader( vertShader ); return false; }

	// --- RCAS fragment shader (4 source parts) ---
	const GLchar *rcasSrcs[4] = {
		fsrRcasPreamble,
		ffxAGlslSrc,
		ffxFsr1GlslSrc,
		fsrRcasBody
	};
	GLuint rcasFrag = FSR_CompileShader( GL_FRAGMENT_SHADER, rcasSrcs, 4 );
	if ( !rcasFrag ) { fsr_glDeleteShader( vertShader ); return false; }

	fsr.rcasProg = FSR_CompileProgram( vertShader, rcasFrag );
	fsr_glDeleteShader( rcasFrag );
	fsr_glDeleteShader( vertShader );
	if ( !fsr.rcasProg ) return false;

	// --- cache uniform locations ---
	fsr.easuTexLoc = fsr_glGetUniformLocation( fsr.easuProg, "InputTexture" );
	fsr.easuC0     = fsr_glGetUniformLocation( fsr.easuProg, "Const0" );
	fsr.easuC1     = fsr_glGetUniformLocation( fsr.easuProg, "Const1" );
	fsr.easuC2     = fsr_glGetUniformLocation( fsr.easuProg, "Const2" );
	fsr.easuC3     = fsr_glGetUniformLocation( fsr.easuProg, "Const3" );
	fsr.rcasTexLoc = fsr_glGetUniformLocation( fsr.rcasProg, "InputTexture" );
	fsr.rcasC0     = fsr_glGetUniformLocation( fsr.rcasProg, "Const0" );

	common->Printf( "...FSR 1.0 shaders compiled (EASU prog=%u, RCAS prog=%u)\n",
		fsr.easuProg, fsr.rcasProg );
	return true;
}

// -------------------------------------------------------------------------
// FSR_ComputeInternalRes
// -------------------------------------------------------------------------
static void FSR_ComputeInternalRes( void ) {
	int q = r_fsr.GetInteger();
	if ( q < 1 ) q = 1;
	if ( q > 4 ) q = 4;
	// Round down to a multiple of 64 pixels in each dimension
	fsr.internalWidth  = ((int)( fsr.displayWidth  * fsrScales[q] ) + 63) & ~63;
	fsr.internalHeight = ((int)( fsr.displayHeight * fsrScales[q] ) + 63) & ~63;
	// Guard against rounding to zero on tiny windows
	if ( fsr.internalWidth  < 64 ) fsr.internalWidth  = 64;
	if ( fsr.internalHeight < 64 ) fsr.internalHeight = 64;
}

// -------------------------------------------------------------------------
// FSR_CreateFBOs
// Returns false on failure (fsr.available set to false).
// -------------------------------------------------------------------------
static bool FSR_CreateFBOs( void ) {
	bool rcasEnabled = ( r_fsrSharpness.GetFloat() >= 0.0f );
	bool taaEnabled = taa.available && r_taa.GetBool();

	// --- scene FBO (internal resolution) ---
	qglGenTextures( 1, &fsr.sceneColorTex );
	qglBindTexture( GL_TEXTURE_2D, fsr.sceneColorTex );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, fsr.internalWidth, fsr.internalHeight,
		0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglBindTexture( GL_TEXTURE_2D, 0 );

	if ( taaEnabled ) {
		qglGenTextures( 1, &fsr.velocityTex );
		qglBindTexture( GL_TEXTURE_2D, fsr.velocityTex );
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_RG16F, fsr.internalWidth, fsr.internalHeight,
			0, GL_RG, GL_HALF_FLOAT, NULL );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		qglBindTexture( GL_TEXTURE_2D, 0 );

		qglGenTextures( 1, &fsr.sceneDepthTex );
		qglBindTexture( GL_TEXTURE_2D, fsr.sceneDepthTex );
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, fsr.internalWidth, fsr.internalHeight,
			0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		qglBindTexture( GL_TEXTURE_2D, 0 );
	} else {
		fsr_glGenRenderbuffers( 1, &fsr.sceneDepthRBO );
		fsr_glBindRenderbuffer( GL_RENDERBUFFER, fsr.sceneDepthRBO );
		fsr_glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
			fsr.internalWidth, fsr.internalHeight );
		fsr_glBindRenderbuffer( GL_RENDERBUFFER, 0 );
	}

	fsr_glGenFramebuffers( 1, &fsr.sceneFBO );
	fsr_glBindFramebuffer( GL_FRAMEBUFFER, fsr.sceneFBO );
	fsr_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, fsr.sceneColorTex, 0 );
	if ( taaEnabled ) {
		fsr_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
			GL_TEXTURE_2D, fsr.velocityTex, 0 );
		fsr_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
			GL_TEXTURE_2D, fsr.sceneDepthTex, 0 );
	} else {
		fsr_glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
			GL_RENDERBUFFER, fsr.sceneDepthRBO );
	}

	if ( fsr_glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
		common->Warning( "FSR: scene FBO incomplete — disabling FSR" );
		fsr_glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		return false;
	}
	fsr_glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	// --- EASU output FBO (display resolution, only needed when RCAS follows) ---
	if ( rcasEnabled ) {
		qglGenTextures( 1, &fsr.easuColorTex );
		qglBindTexture( GL_TEXTURE_2D, fsr.easuColorTex );
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, fsr.displayWidth, fsr.displayHeight,
			0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		qglBindTexture( GL_TEXTURE_2D, 0 );

		fsr_glGenFramebuffers( 1, &fsr.easuFBO );
		fsr_glBindFramebuffer( GL_FRAMEBUFFER, fsr.easuFBO );
		fsr_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, fsr.easuColorTex, 0 );
		if ( fsr_glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
			common->Warning( "FSR: EASU FBO incomplete — disabling FSR" );
			fsr_glBindFramebuffer( GL_FRAMEBUFFER, 0 );
			return false;
		}
		fsr_glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	}

	return true;
}

// -------------------------------------------------------------------------
// FSR_DeleteFBOs
// -------------------------------------------------------------------------
static void FSR_DeleteFBOs( void ) {
	if ( fsr.easuFBO      ) { fsr_glDeleteFramebuffers( 1, &fsr.easuFBO      ); fsr.easuFBO      = 0; }
	if ( fsr.easuColorTex ) { qglDeleteTextures( 1, &fsr.easuColorTex );        fsr.easuColorTex = 0; }
	if ( fsr.velocityTex  ) { qglDeleteTextures( 1, &fsr.velocityTex  );        fsr.velocityTex  = 0; }
	if ( fsr.sceneDepthTex ) { qglDeleteTextures( 1, &fsr.sceneDepthTex );      fsr.sceneDepthTex = 0; }
	if ( fsr.sceneFBO      ) { fsr_glDeleteFramebuffers( 1, &fsr.sceneFBO     ); fsr.sceneFBO     = 0; }
	if ( fsr.sceneColorTex ) { qglDeleteTextures( 1, &fsr.sceneColorTex );      fsr.sceneColorTex = 0; }
	if ( fsr.sceneDepthRBO ) { fsr_glDeleteRenderbuffers( 1, &fsr.sceneDepthRBO ); fsr.sceneDepthRBO = 0; }
}

// -------------------------------------------------------------------------
// FSR_DeleteShaders
// -------------------------------------------------------------------------
static void FSR_DeleteShaders( void ) {
	if ( fsr.easuProg ) { fsr_glDeleteProgram( fsr.easuProg ); fsr.easuProg = 0; }
	if ( fsr.rcasProg ) { fsr_glDeleteProgram( fsr.rcasProg ); fsr.rcasProg = 0; }
}

// -------------------------------------------------------------------------
// FSR_UpdateActiveState — set fsr.active based on current settings
// -------------------------------------------------------------------------
static void FSR_UpdateActiveState( void ) {
	fsr.active = fsr.available &&
	             ( r_fsr.GetInteger() > 0 ) &&
	             ( fsr.internalWidth < fsr.displayWidth || fsr.internalHeight < fsr.displayHeight );
}

// =========================================================================
// Public API
// =========================================================================

/*
===================
FSR_Init
Called at end of R_InitOpenGL().
===================
*/
void FSR_Init( void ) {
	memset( &fsr, 0, sizeof( fsr ) );

	if ( !glConfig.fsrAvailable ) {
		common->Printf( "FSR 1.0: disabled (OpenGL 4.0 not available)\n" );
		return;
	}

	if ( !FSR_LoadExtensionPointers() ) {
		common->Warning( "FSR 1.0: failed to load required GL extension pointers" );
		return;
	}

	if ( !FSR_CompileShaders() ) {
		common->Warning( "FSR 1.0: shader compilation failed" );
		return;
	}

	fsr.available = true;
	common->Printf( "FSR 1.0: initialized (GL 4.0, EASU + RCAS)\n" );

	// Warn once if MSAA is active
	if ( r_multiSamples.GetInteger() > 0 ) {
		common->Warning( "FSR: MSAA is active but the scene FBO is non-MSAA; "
			"MSAA is not applied to the 3D scene when FSR is enabled." );
	}
}

/*
===================
FSR_Shutdown
Called before GLimp_Shutdown().
===================
*/
void FSR_Shutdown( void ) {
	FSR_DeleteFBOs();
	if ( fsr_glDeleteProgram ) {
		FSR_DeleteShaders();
	}
	memset( &fsr, 0, sizeof( fsr ) );
}

/*
===================
FSR_Reinit
Called when r_fsr or r_fsrSharpness changes at runtime.
Recreates FBOs without recompiling shaders.
===================
*/
void FSR_Reinit( void ) {
	if ( !fsr.available ) return;

	FSR_DeleteFBOs();
	fsr.active = false;

	int q = r_fsr.GetInteger();
	if ( q <= 0 ) {
		// FSR disabled — no FBOs needed
		return;
	}

	// BeginFrame sets displayWidth/Height each frame, but on first activation they may be 0.
	if ( fsr.displayWidth == 0 || fsr.displayHeight == 0 ) {
		fsr.displayWidth  = glConfig.vidWidth;
		fsr.displayHeight = glConfig.vidHeight;
	}

	FSR_ComputeInternalRes();

	if ( !FSR_CreateFBOs() ) {
		fsr.available = false;
		return;
	}

	FSR_UpdateActiveState();

	if ( fsr.active ) {
		common->Printf( "FSR: internal %dx%d -> display %dx%d (preset %d)\n",
			fsr.internalWidth, fsr.internalHeight,
			fsr.displayWidth,  fsr.displayHeight, q );
	}
}

/*
===================
FSR_CheckCvars
Called from R_CheckCvars() each frame.
===================
*/
void FSR_CheckCvars( void ) {
	if ( !fsr.available ) return;

	if ( r_fsr.IsModified() || r_fsrSharpness.IsModified() ) {
		r_fsr.ClearModified();
		r_fsrSharpness.ClearModified();
		FSR_Reinit();
		if ( TAA_IsActive() ) {
			TAA_Reinit();
		}
	}
}

/*
===================
FSR_BeginScene
Bind the scene FBO and set the viewport to internal resolution.
Called just before the first 3D draw command in a frame.
===================
*/
void FSR_BeginScene( void ) {
	fsr_glBindFramebuffer( GL_FRAMEBUFFER, fsr.sceneFBO );

	bool taaEnabled = taa.available && r_taa.GetBool();
	if ( taaEnabled ) {
		GLuint buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		fsr_glDrawBuffers( 2, buffers );
	} else {
		qglDrawBuffer( GL_COLOR_ATTACHMENT0 );
	}

	qglViewport( 0, 0, fsr.internalWidth, fsr.internalHeight );
	if ( r_useScissor.GetBool() ) {
		qglScissor( 0, 0, fsr.internalWidth, fsr.internalHeight );
	}

	qglClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	qglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
}

/*
===================
FSR_EndScene
Run EASU (and optionally RCAS), then restore default FBO + display viewport.
Called after the last 3D draw, before 2D UI rendering.
===================
*/
void FSR_EndScene( void ) {
	bool rcasEnabled = ( r_fsrSharpness.GetFloat() >= 0.0f ) && ( fsr.rcasProg != 0 ) && ( fsr.easuFBO != 0 );

	// Restore default framebuffer so EASU/RCAS output lands on screen
	// (for RCAS: EASU -> easuFBO first, RCAS -> default FBO)
	fsr_glBindFramebuffer( GL_FRAMEBUFFER, rcasEnabled ? fsr.easuFBO : 0 );

	// ---- disable 3D render state that would interfere ----
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

	// ---- EASU pass ----
	qglViewport( 0, 0, fsr.displayWidth, fsr.displayHeight );

	// Compute EASU constants on the CPU
	AU1 con0[4], con1[4], con2[4], con3[4];
	FsrEasuCon(
		con0, con1, con2, con3,
		(AF1)fsr.internalWidth,  (AF1)fsr.internalHeight,  // viewport = full internal
		(AF1)fsr.internalWidth,  (AF1)fsr.internalHeight,  // input image size = same
		(AF1)fsr.displayWidth,   (AF1)fsr.displayHeight    // output size
	);

	// Bind scene color texture to unit 0
	qglActiveTextureARB( GL_TEXTURE0_ARB );
	qglBindTexture( GL_TEXTURE_2D, fsr.sceneColorTex );

	fsr_glUseProgram( fsr.easuProg );
	fsr_glUniform1i ( fsr.easuTexLoc, 0 );
	fsr_glUniform4uiv( fsr.easuC0, 1, (const GLuint *)con0 );
	fsr_glUniform4uiv( fsr.easuC1, 1, (const GLuint *)con1 );
	fsr_glUniform4uiv( fsr.easuC2, 1, (const GLuint *)con2 );
	fsr_glUniform4uiv( fsr.easuC3, 1, (const GLuint *)con3 );

	// Draw fullscreen triangle strip (NDC coordinates)
	qglBegin( GL_TRIANGLE_STRIP );
		qglVertex2f( -1.0f, -1.0f );
		qglVertex2f(  1.0f, -1.0f );
		qglVertex2f( -1.0f,  1.0f );
		qglVertex2f(  1.0f,  1.0f );
	qglEnd();

	// ---- RCAS pass ----
	if ( rcasEnabled ) {
		fsr_glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		qglViewport( 0, 0, fsr.displayWidth, fsr.displayHeight );

		AU1 rcasCon[4];
		FsrRcasCon( rcasCon, (AF1)r_fsrSharpness.GetFloat() );

		qglBindTexture( GL_TEXTURE_2D, fsr.easuColorTex );

		fsr_glUseProgram( fsr.rcasProg );
		fsr_glUniform1i ( fsr.rcasTexLoc, 0 );
		fsr_glUniform4uiv( fsr.rcasC0, 1, (const GLuint *)rcasCon );

		qglBegin( GL_TRIANGLE_STRIP );
			qglVertex2f( -1.0f, -1.0f );
			qglVertex2f(  1.0f, -1.0f );
			qglVertex2f( -1.0f,  1.0f );
			qglVertex2f(  1.0f,  1.0f );
		qglEnd();
	}

	// Deactivate GLSL program before handing control back to the engine.
	fsr_glUseProgram( 0 );

	// Restore glConfig dimensions so RB_SetDefaultGLState sets the scissor correctly
	// and subsequent 2D rendering uses display resolution.
	glConfig.vidWidth  = fsr.displayWidth;
	glConfig.vidHeight = fsr.displayHeight;

	// Full engine state reset: wipes backEnd.glState, re-establishes all GL defaults
	// (texture units, client arrays, depth/blend/cull, etc.).  This is the only safe
	// way to clean up after raw qgl* / GLSL calls that bypassed the engine's state
	// tracker — the same reset the engine performs at the very start of each frame.
	RB_SetDefaultGLState();
}

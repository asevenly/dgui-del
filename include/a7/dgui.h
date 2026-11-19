#ifndef A7_DGUI_H
#define A7_DGUI_H

#define A7_DGUI_GL_FRONT          0x0404
#define A7_DGUI_GL_BACK           0x0405
#define A7_DGUI_GL_FRONT_AND_BACK 0x0408
#define A7_DGUI_GL_POLYGON_MODE   0x0B40
#define A7_DGUI_GL_CULL_FACE      0x0B44
#define A7_DGUI_GL_CULL_FACE_MODE 0x0B45
#define A7_DGUI_GL_DEPTH_TEST     0x0B71
#define A7_DGUI_GL_POINT          0x1B00
#define A7_DGUI_GL_LINE           0x1B01
#define A7_DGUI_GL_FILL           0x1B02

typedef enum A7DguiCmd {
    A7_Dgui_CmdNone = 0,
    A7_Dgui_CmdToggleCullFace,
    A7_Dgui_CmdCycleCullMode,
    A7_Dgui_CmdCyclePolyMode,
    A7_Dgui_CmdToggleDepthTest
} A7DguiCmd;

typedef struct A7DguiGlProcs {
    void (*enable)(unsigned int cap);
    void (*disable)(unsigned int cap);
    unsigned char (*isEnabled)(unsigned int cap);
    void (*getIntegerv)(unsigned int pname, int *data);
    void (*cullFace)(unsigned int mode);
    void (*polygonMode)(unsigned int face, unsigned int mode);
} A7DguiGlProcs;

typedef struct A7DguiGlState {
    int cullFaceEnabled;
    int cullMode;
    int polyMode;
    int depthTestEnabled;
} A7DguiGlState;

typedef struct A7DguiCtx_ A7DguiCtx;

/* Call BEFORE glfwInit(): forks a separate SDL2 debug window process */
A7DguiCtx *a7_dgui_ctx_create(void);
void a7_dgui_ctx_destroy(A7DguiCtx *ctx);

/* Call AFTER glfwMakeContextCurrent() */
void a7_dgui_gl_set_procs(A7DguiCtx *ctx, const A7DguiGlProcs *procs);

/* Call once per frame on your OpenGL render thread */
void a7_dgui_gl_update(A7DguiCtx *ctx);

#define A7_DGUI_BIND_GL(ctx) do { \
    A7DguiGlProcs procs_; \
    procs_.enable      = glEnable; \
    procs_.disable     = glDisable; \
    procs_.isEnabled   = glIsEnabled; \
    procs_.getIntegerv = glGetIntegerv; \
    procs_.cullFace    = glCullFace; \
    procs_.polygonMode = glPolygonMode; \
    a7_dgui_gl_set_procs((ctx), &procs_); \
} while (0)

const char *a7_dgui_cull_mode_name(int mode);
const char *a7_dgui_poly_mode_name(int mode);


#endif /* INCLUDE GUARD */

#define _POSIX_C_SOURCE 200809L

#include <a7/dgui.h>
#include <SDL.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define A7_DGUI_CMD_BYTES_   4
#define A7_DGUI_STATE_BYTES_ 16

struct A7DguiCtx_ {
    int cmdReadFd_;
    int stateWriteFd_;
    pid_t childPid_;
    A7DguiGlProcs glProcs_;
    int hasGl_;
    unsigned char rxBuf_[A7_DGUI_CMD_BYTES_];
    int rxLen_;
};

/* --- Private Helpers --- */
static void a7_Dgui_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

static void a7_Dgui_write_i32(unsigned char *dst, int val) {
    unsigned int u = (unsigned int)val;
    dst[0] = (unsigned char)(u & 0xFFu);
    dst[1] = (unsigned char)((u >> 8) & 0xFFu);
    dst[2] = (unsigned char)((u >> 16) & 0xFFu);
    dst[3] = (unsigned char)((u >> 24) & 0xFFu);
}

static int a7_Dgui_read_i32(const unsigned char *src) {
    unsigned int u;
    u = ((unsigned int)src[0]) |
        ((unsigned int)src[1] << 8) |
        ((unsigned int)src[2] << 16) |
        ((unsigned int)src[3] << 24);
    return (int)u;
}

static void a7_Dgui_read_gl_state(const A7DguiGlProcs *gl, A7DguiGlState *outState) {
    int polyModes[2];
    int cullMode = A7_DGUI_GL_BACK;

    polyModes[0] = A7_DGUI_GL_FILL;
    polyModes[1] = A7_DGUI_GL_FILL;

    outState->cullFaceEnabled  = gl->isEnabled(A7_DGUI_GL_CULL_FACE) ? 1 : 0;
    outState->depthTestEnabled = gl->isEnabled(A7_DGUI_GL_DEPTH_TEST) ? 1 : 0;

    gl->getIntegerv(A7_DGUI_GL_CULL_FACE_MODE, &cullMode);
    gl->getIntegerv(A7_DGUI_GL_POLYGON_MODE, polyModes);

    outState->cullMode = cullMode;
    outState->polyMode = polyModes[0];
}

static void a7_Dgui_apply_cmd(const A7DguiGlProcs *gl, A7DguiCmd cmd) {
    A7DguiGlState cur;
    a7_Dgui_read_gl_state(gl, &cur);

    if (cmd == A7_Dgui_CmdToggleCullFace) {
        if (cur.cullFaceEnabled) {
            gl->disable(A7_DGUI_GL_CULL_FACE);
        } else {
            gl->enable(A7_DGUI_GL_CULL_FACE);
        }
    } else if (cmd == A7_Dgui_CmdToggleDepthTest) {
        if (cur.depthTestEnabled) {
            gl->disable(A7_DGUI_GL_DEPTH_TEST);
        } else {
            gl->enable(A7_DGUI_GL_DEPTH_TEST);
        }
    } else if (cmd == A7_Dgui_CmdCycleCullMode) {
        int nextMode = A7_DGUI_GL_BACK;
        if (cur.cullMode == A7_DGUI_GL_BACK) {
            nextMode = A7_DGUI_GL_FRONT;
        } else if (cur.cullMode == A7_DGUI_GL_FRONT) {
            nextMode = A7_DGUI_GL_FRONT_AND_BACK;
        }
        gl->cullFace((unsigned int)nextMode);
    } else if (cmd == A7_Dgui_CmdCyclePolyMode) {
        int nextPoly = A7_DGUI_GL_FILL;
        if (cur.polyMode == A7_DGUI_GL_FILL) {
            nextPoly = A7_DGUI_GL_LINE;
        } else if (cur.polyMode == A7_DGUI_GL_LINE) {
            nextPoly = A7_DGUI_GL_POINT;
        }
        gl->polygonMode(A7_DGUI_GL_FRONT_AND_BACK, (unsigned int)nextPoly);
    }
}

static void a7_Dgui_send_state(A7DguiCtx *ctx) {
    A7DguiGlState st;
    unsigned char pkt[A7_DGUI_STATE_BYTES_];
    ssize_t written;

    if (ctx->stateWriteFd_ < 0) {
        return;
    }

    a7_Dgui_read_gl_state(&ctx->glProcs_, &st);
    a7_Dgui_write_i32(pkt + 0,  st.cullFaceEnabled);
    a7_Dgui_write_i32(pkt + 4,  st.cullMode);
    a7_Dgui_write_i32(pkt + 8,  st.polyMode);
    a7_Dgui_write_i32(pkt + 12, st.depthTestEnabled);

    written = write(ctx->stateWriteFd_, pkt, A7_DGUI_STATE_BYTES_);
    if (written <= 0) {
        close(ctx->stateWriteFd_);
        ctx->stateWriteFd_ = -1;
    }
}

static void a7_Dgui_fill_rect_rgba(
    unsigned char *rgbaPixels,
    int bufW,
    int bufH,
    int x,
    int y,
    int w,
    int h,
    unsigned char r,
    unsigned char g,
    unsigned char b
) {
    int px;
    int py;
    int xEnd = x + w;
    int yEnd = y + h;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (xEnd > bufW) xEnd = bufW;
    if (yEnd > bufH) yEnd = bufH;

    for (py = y; py < yEnd; py++) {
        unsigned char *row = rgbaPixels + (py * bufW * 4);
        for (px = x; px < xEnd; px++) {
            unsigned char *p = row + (px * 4);
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
    }
}

static void a7_Dgui_present_child(
    SDL_Window *win,
    const A7DguiGlState *st,
    unsigned char **fb,
    int *fbCap
) {
    SDL_Surface *winSurf;
    SDL_Surface *srcSurf;
    int needBytes;

    winSurf = SDL_GetWindowSurface(win);
    if (!winSurf || winSurf->w <= 0 || winSurf->h <= 0) {
        return;
    }

    needBytes = winSurf->w * winSurf->h * 4;
    if (needBytes > *fbCap) {
        unsigned char *next = (unsigned char *)realloc(*fb, (size_t)needBytes);
        if (!next) {
            return;
        }
        *fb = next;
        *fbCap = needBytes;
    }

    a7_Dgui_fill_rect_rgba(*fb, winSurf->w, winSurf->h, 0, 0, winSurf->w, winSurf->h, 28, 30, 36);

    /* Status indicators: [CullFace] [PolyMode] [DepthTest] */
    a7_Dgui_fill_rect_rgba(
        *fb, winSurf->w, winSurf->h, 20, 20, 80, 40,
        st->cullFaceEnabled ? 55 : 160,
        st->cullFaceEnabled ? 170 : 55,
        65
    );

    if (st->polyMode == A7_DGUI_GL_FILL) {
        a7_Dgui_fill_rect_rgba(*fb, winSurf->w, winSurf->h, 120, 20, 80, 40, 55, 170, 95);
    } else if (st->polyMode == A7_DGUI_GL_LINE) {
        a7_Dgui_fill_rect_rgba(*fb, winSurf->w, winSurf->h, 120, 20, 80, 40, 60, 130, 210);
    } else {
        a7_Dgui_fill_rect_rgba(*fb, winSurf->w, winSurf->h, 120, 20, 80, 40, 215, 165, 50);
    }

    a7_Dgui_fill_rect_rgba(
        *fb, winSurf->w, winSurf->h, 220, 20, 80, 40,
        st->depthTestEnabled ? 55 : 160,
        st->depthTestEnabled ? 170 : 55,
        65
    );

    srcSurf = SDL_CreateRGBSurfaceFrom(
        *fb, winSurf->w, winSurf->h, 32, winSurf->w * 4,
        0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u
    );
    if (srcSurf) {
        SDL_BlitSurface(srcSurf, NULL, winSurf, NULL);
        SDL_FreeSurface(srcSurf);
        SDL_UpdateWindowSurface(win);
    }
}

/* Runs strictly inside the forked child process */
static void a7_Dgui_run_sdl2_child(int cmdWriteFd, int stateReadFd) {
    SDL_Window *win;
    A7DguiGlState st;
    unsigned char rxBuf[A7_DGUI_STATE_BYTES_];
    int rxLen = 0;
    unsigned char *fb = NULL;
    int fbCap = 0;
    int running = 1;
    int dirty = 1;

    st.cullFaceEnabled = 0;
    st.cullMode = A7_DGUI_GL_BACK;
    st.polyMode = A7_DGUI_GL_FILL;
    st.depthTestEnabled = 0;

    a7_Dgui_set_nonblock(stateReadFd);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("[DGUI Child] SDL_Init failed: %s\n", SDL_GetError());
        fflush(stdout);
        _exit(1);
    }

    win = SDL_CreateWindow(
        "A7 DGUI (Child Process - Press C/M/P/D)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        420, 120, SDL_WINDOW_RESIZABLE
    );
    if (!win) {
        SDL_Quit();
        _exit(1);
    }

    printf("[DGUI Child pid=%d] Keys: [C] CullFace | [M] CullMode | [P] PolyMode | [D] DepthTest\n",
           (int)getpid());
    fflush(stdout);

    while (running) {
        SDL_Event ev;
        ssize_t n;

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_WINDOWEVENT &&
                       (ev.window.event == SDL_WINDOWEVENT_EXPOSED ||
                        ev.window.event == SDL_WINDOWEVENT_RESIZED)) {
                dirty = 1;
            } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                A7DguiCmd cmd = A7_Dgui_CmdNone;
                SDL_Keycode key = ev.key.keysym.sym;

                if (key == SDLK_ESCAPE) running = 0;
                else if (key == SDLK_c) cmd = A7_Dgui_CmdToggleCullFace;
                else if (key == SDLK_m) cmd = A7_Dgui_CmdCycleCullMode;
                else if (key == SDLK_p) cmd = A7_Dgui_CmdCyclePolyMode;
                else if (key == SDLK_d) cmd = A7_Dgui_CmdToggleDepthTest;

                if (cmd != A7_Dgui_CmdNone) {
                    unsigned char pkt[A7_DGUI_CMD_BYTES_];
                    a7_Dgui_write_i32(pkt, (int)cmd);
                    if (write(cmdWriteFd, pkt, A7_DGUI_CMD_BYTES_) <= 0) {
                        running = 0;
                    }
                }
            }
        }

        /* Read updated OpenGL state from parent app process */
        for (;;) {
            n = read(stateReadFd, rxBuf + rxLen, (size_t)(A7_DGUI_STATE_BYTES_ - rxLen));
            if (n > 0) {
                rxLen += (int)n;
                if (rxLen == A7_DGUI_STATE_BYTES_) {
                    st.cullFaceEnabled  = a7_Dgui_read_i32(rxBuf + 0);
                    st.cullMode         = a7_Dgui_read_i32(rxBuf + 4);
                    st.polyMode         = a7_Dgui_read_i32(rxBuf + 8);
                    st.depthTestEnabled = a7_Dgui_read_i32(rxBuf + 12);
                    rxLen = 0;
                    dirty = 1;

                    printf("[DGUI Child] GL State -> CULL=%s | MODE=%s | POLY=%s | DEPTH=%s\n",
                           st.cullFaceEnabled ? "ON" : "OFF",
                           a7_dgui_cull_mode_name(st.cullMode),
                           a7_dgui_poly_mode_name(st.polyMode),
                           st.depthTestEnabled ? "ON" : "OFF");
                    fflush(stdout);
                }
            } else if (n == 0) {
                /* Parent exited */
                running = 0;
                break;
            } else {
                break;
            }
        }

        if (dirty) {
            a7_Dgui_present_child(win, &st, &fb, &fbCap);
            dirty = 0;
        }

        SDL_Delay(16);
    }

    free(fb);
    close(cmdWriteFd);
    close(stateReadFd);
    SDL_DestroyWindow(win);
    SDL_Quit();
    _exit(0);
}

/* --- Public API --- */
A7DguiCtx *a7_dgui_ctx_create(void) {
    int cmdPipe[2];
    int statePipe[2];
    pid_t pid;
    A7DguiCtx *ctx;

    if (pipe(cmdPipe) != 0) {
        return NULL;
    }
    if (pipe(statePipe) != 0) {
        close(cmdPipe[0]);
        close(cmdPipe[1]);
        return NULL;
    }

    pid = fork();
    if (pid < 0) {
        close(cmdPipe[0]);
        close(cmdPipe[1]);
        close(statePipe[0]);
        close(statePipe[1]);
        return NULL;
    }

    if (pid == 0) {
        /* Child process: run SDL2 window loop (never returns) */
        close(cmdPipe[0]);
        close(statePipe[1]);
        a7_Dgui_run_sdl2_child(cmdPipe[1], statePipe[0]);
        _exit(0);
    }

    /* Parent process (OpenGL app) */
    signal(SIGPIPE, SIG_IGN);
    close(cmdPipe[1]);
    close(statePipe[0]);
    a7_Dgui_set_nonblock(cmdPipe[0]);

    ctx = (A7DguiCtx *)calloc(1, sizeof(A7DguiCtx));
    if (!ctx) {
        close(cmdPipe[0]);
        close(statePipe[1]);
        return NULL;
    }

    ctx->cmdReadFd_ = cmdPipe[0];
    ctx->stateWriteFd_ = statePipe[1];
    ctx->childPid_ = pid;
    return ctx;
}

void a7_dgui_ctx_destroy(A7DguiCtx *ctx) {
    if (!ctx) {
        return;
    }
    if (ctx->cmdReadFd_ >= 0) {
        close(ctx->cmdReadFd_);
    }
    if (ctx->stateWriteFd_ >= 0) {
        close(ctx->stateWriteFd_);
    }
    if (ctx->childPid_ > 0) {
        waitpid(ctx->childPid_, NULL, 0);
    }
    free(ctx);
}

void a7_dgui_gl_set_procs(A7DguiCtx *ctx, const A7DguiGlProcs *procs) {
    if (!ctx || !procs) {
        return;
    }
    ctx->glProcs_ = *procs;
    ctx->hasGl_ = 1;
    a7_Dgui_send_state(ctx);
}

void a7_dgui_gl_update(A7DguiCtx *ctx) {
    ssize_t n;

    if (!ctx || !ctx->hasGl_ || ctx->cmdReadFd_ < 0) {
        return;
    }

    for (;;) {
        n = read(ctx->cmdReadFd_,
                 ctx->rxBuf_ + ctx->rxLen_,
                 (size_t)(A7_DGUI_CMD_BYTES_ - ctx->rxLen_));
        if (n > 0) {
            ctx->rxLen_ += (int)n;
            if (ctx->rxLen_ == A7_DGUI_CMD_BYTES_) {
                A7DguiCmd cmd = (A7DguiCmd)a7_Dgui_read_i32(ctx->rxBuf_);
                ctx->rxLen_ = 0;
                a7_Dgui_apply_cmd(&ctx->glProcs_, cmd);
                a7_Dgui_send_state(ctx);
            }
        } else if (n == 0) {
            /* Child debug window closed */
            close(ctx->cmdReadFd_);
            ctx->cmdReadFd_ = -1;
            break;
        } else {
            break;
        }
    }
}

const char *a7_dgui_cull_mode_name(int mode) {
    if (mode == A7_DGUI_GL_FRONT) return "GL_FRONT";
    if (mode == A7_DGUI_GL_BACK) return "GL_BACK";
    if (mode == A7_DGUI_GL_FRONT_AND_BACK) return "GL_FRONT_AND_BACK";
    return "UNKNOWN";
}

const char *a7_dgui_poly_mode_name(int mode) {
    if (mode == A7_DGUI_GL_POINT) return "GL_POINT";
    if (mode == A7_DGUI_GL_LINE) return "GL_LINE";
    if (mode == A7_DGUI_GL_FILL) return "GL_FILL";
    return "UNKNOWN";
}

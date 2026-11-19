#include <a7/dgui.h>
#include <glfw/son8.hxx>
#include <glad/son8/gl/define.h>
#include <glad/son8/gl/loader.h>

int main( void ) {
    A7DguiCtx *dgui;
    GLFWwindow *window;

    /* NOTE: BEFORE `glfwInit()` */
    dgui = a7_dgui_ctx_create( );

    if ( !glfwInit( )) {
        a7_dgui_ctx_destroy( dgui );
        return 1;
    }

    window = glfwCreateWindow( 640, 360, "Main OpenGL App", NULL, NULL );
    glfwMakeContextCurrent( window );

    gladLoadGL( glfwGetProcAddress );

    /* bind `OpenGL` functions (should work with `GLAD/GLEW/raw GL`) */
    A7_DGUI_BIND_GL( dgui );

    while ( !glfwWindowShouldClose( window )) {
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize( window, &fbW, &fbH );
        glViewport( 0, 0, fbW, fbH );
        glClearColor( 0.12f, 0.13f, 0.16f, 1.0f );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

        glBegin( GL_TRIANGLES ); {
            /* yellow, front-facing CCW, closer */
            glColor3f( .9f, .8f, .2f );
            glVertex3f(-.6f,-.4f, -.3f );
            glVertex3f( .3f,-.4f, -.3f);
            glVertex3f(-.1f, .6f, -.3f );
            /* blue, back-facing CW, farther z = +0.3 */
            glColor3f( 0.20f, 0.55f, 0.95f );
            glVertex3f( -0.3f, -0.5f,  .3f );
            glVertex3f( 0.15f, 0.5f,  .3f );
            glVertex3f( 0.6f, -0.5f,  .3f );
        } glEnd();

        a7_dgui_gl_update( dgui );

        glfwSwapBuffers( window );
        glfwPollEvents( );
    }
    /* clean */
    a7_dgui_ctx_destroy( dgui );
    glfwDestroyWindow( window );
    glfwTerminate( );

    return 0;
}

/* Apache License 2.0
 * NO WARRANTY OF ANY KIND see <http://www.apache.org/licenses/LICENSE-2.0>
 * SPDX-License-Identifier: Apache-2.0
 * lib: [`NAME`] [LEGEND]
 * Ⓒ Copyright (c) 2026 Oleg'Ease'Kharchuk ᦒ */

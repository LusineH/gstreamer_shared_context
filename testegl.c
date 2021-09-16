
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <iostream>
#include <fstream>


#define HAVE_X11
#define GST_GL_HAVE_WINDOW_X11 1

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <thread>


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#define GST_USE_UNSTABLE_API
#include <gst/gl/gl.h>
#include <gst/gl/egl/gstgldisplay_egl.h>
#include <gst/gl/egl/gstglcontext_egl.h>
#include <gst/gl/x11/gstgldisplay_x11.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "cube_texture_and_coords.h"

static gpointer render_func(gpointer data);

/* *INDENT-OFF* */

/* vertex source */
static const gchar *cube_v_src =
    "attribute vec4 a_position;                          \n"
    "attribute vec2 a_texCoord;                          \n"
    "uniform float u_rotx;                               \n"
    "uniform float u_roty;                               \n"
    "uniform float u_rotz;                               \n"
    "uniform mat4 u_modelview;                           \n"
    "uniform mat4 u_projection;                          \n"
    "varying vec2 v_texCoord;                            \n"
    "void main()                                         \n"
    "{                                                   \n"
    "   float PI = 3.14159265;                           \n"
    "   float xrot = u_rotx*2.0*PI/360.0;                \n"
    "   float yrot = u_roty*2.0*PI/360.0;                \n"
    "   float zrot = u_rotz*2.0*PI/360.0;                \n"
    "   mat4 matX = mat4 (                               \n"
    "            1.0,        0.0,        0.0, 0.0,       \n"
    "            0.0,  cos(xrot),  sin(xrot), 0.0,       \n"
    "            0.0, -sin(xrot),  cos(xrot), 0.0,       \n"
    "            0.0,        0.0,        0.0, 1.0 );     \n"
    "   mat4 matY = mat4 (                               \n"
    "      cos(yrot),        0.0, -sin(yrot), 0.0,       \n"
    "            0.0,        1.0,        0.0, 0.0,       \n"
    "      sin(yrot),        0.0,  cos(yrot), 0.0,       \n"
    "            0.0,        0.0,       0.0,  1.0 );     \n"
    "   mat4 matZ = mat4 (                               \n"
    "      cos(zrot),  sin(zrot),        0.0, 0.0,       \n"
    "     -sin(zrot),  cos(zrot),        0.0, 0.0,       \n"
    "            0.0,        0.0,        1.0, 0.0,       \n"
    "            0.0,        0.0,        0.0, 1.0 );     \n"
    "   gl_Position = u_projection * u_modelview * matZ * matY * matX * a_position;\n"
    "   v_texCoord = a_texCoord;                         \n"
    "}                                                   \n";

/* fragment source */
static const gchar *cube_f_src =
    "precision mediump float;                            \n"
    "varying vec2 v_texCoord;                            \n"
    "uniform sampler2D s_texture;                        \n"
    "void main()                                         \n"
    "{                                                   \n"
    "  gl_FragColor = vec4(1.0,0.0,1.0,1.0);             \n"
    "}                                                   \n";
/* *INDENT-ON* */

typedef struct
{
  uint32_t screen_width;
  uint32_t screen_height;
  gboolean animate;

  GstCaps *caps;

  /* OpenGL|ES objects */
  EGLDisplay egl_display;
  EGLSurface surface;
  EGLContext context;
  EGLContext m_LocalThreadContext;
  GLuint tex;
  EGLConfig config;

  GLint vshader;
  GLint fshader;
  GLint program;

  GLint u_modelviewmatrix;
  GLint u_projectionmatrix;
  GLint s_texture;
  GLint u_rotx;
  GLint u_roty;
  GLint u_rotz;

  GstGLMatrix modelview;
  GstGLMatrix projection;
  GLfloat fov;
  GLfloat aspect;

  /* model rotation vector and direction */
  GLfloat rot_angle_x_inc;
  GLfloat rot_angle_y_inc;
  GLfloat rot_angle_z_inc;

  /* current model rotation angles */
  GLfloat rot_angle_x;
  GLfloat rot_angle_y;
  GLfloat rot_angle_z;

  /* current distance from camera */
  GLfloat distance;
  GLfloat distance_inc;

  /* GStreamer related resources */
  GstElement *pipeline;
  GstElement *vsink;
  GstElement *appsrc;
  GstElement *gldownload;
  GstGLDisplayEGL *gst_display = nullptr;
  GstGLDisplayX11 *x_gst_display = nullptr;
  GstGLContext *gl_context = nullptr;
  GstGLContext* sharedContext = nullptr;

  GstContext* egl_context = nullptr;
  GstContext* gst_context = nullptr;

  /* Rendering thread state */
  gboolean running;

  Display *xdisplay;
  Window xwindow;

} APP_STATE_T;

static void init_egl(APP_STATE_T * state);
static void init_shaders(APP_STATE_T * state);
static void draw_triangle(APP_STATE_T * state);
static APP_STATE_T _state, *state = &_state;


static bool save_tga(APP_STATE_T * state, std::string name)
{
    int* buffer1 = new int[ 300 * 300 * 4 ];
    glReadPixels( 0, 0, 300, 300, GL_RGBA, GL_UNSIGNED_BYTE, buffer1 );

    std::string file = "/root/picsart/ve-linux-plugins/pi/test/" + name;
    FILE *out = fopen(file.c_str(), "w");

    short  TGAhead[] = {0, 2, 0, 0, 0, 0, 300, 300, 32};
    fwrite(&TGAhead, sizeof(TGAhead), 1, out);
    fwrite(buffer1, 4 * 300 * 300, 1, out);
    fclose(out);

    g_print("save_tga %s succeed!\n", name.c_str());
    return true;
}

static bool save_gst_tga(GstGLContext * context, APP_STATE_T * state)
{
    if (!gst_gl_context_activate(state->sharedContext, true))
        g_print("gst_gl_context_activate(true) returned error %d\n", eglGetError());

    g_print("gst_gl_context_activate(sharedContext) is successful\n");
    return save_tga(state, "tga_file_gst.tga");
}

static void gst_context_activate(APP_STATE_T * state)
{
    if (!gst_gl_context_activate(state->sharedContext, true))
    {
        g_print("gst_gl_context_activate(false) returned error %d\n", eglGetError());
    }
}


/***********************************************************
 * Name: init_ogl
 *
 * Arguments:
 *       APP_STATE_T *state - holds OGLES model info
 *
 * Description: Sets the display, OpenGL|ES context and screen stuff
 *
 * Returns: void
 *
 ***********************************************************/
static void init_egl(APP_STATE_T * state)
{

  g_print("-------------------------init_ogl function begin--------------------\n");

  gint screen_num = 0;
  gulong black_pixel = 0;

  EGLBoolean result;
  EGLint num_config;
  EGLNativeWindowType window_handle = (EGLNativeWindowType) 0;

  static const EGLint attribute_list[] = {
    EGL_DEPTH_SIZE, 16,
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT ,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
  };

  static const EGLint context_attributes[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  state->screen_width = 300;
  state->screen_height = 300;
  state->xdisplay = XOpenDisplay (NULL);
  screen_num = DefaultScreen (state->xdisplay);
  black_pixel = XBlackPixel (state->xdisplay, screen_num);
  state->xwindow = XCreateSimpleWindow (state->xdisplay,
      DefaultRootWindow (state->xdisplay), 0, 0, state->screen_width,
      state->screen_height, 0, 0, black_pixel);
  XSetWindowBackgroundPixmap (state->xdisplay, state->xwindow, None);
  XMapRaised (state->xdisplay, state->xwindow);
  XSync (state->xdisplay, FALSE);
  window_handle = state->xwindow;

  EGLNativeDisplayType nativeDisplay = state->xdisplay;

  // get an EGL display connection
  state->egl_display = eglGetDisplay (nativeDisplay);
  assert (state->egl_display != EGL_NO_DISPLAY);

  g_print ("Display created!\n");
  // initialize the EGL display connection
  result = eglInitialize (state->egl_display, NULL, NULL);
  assert (EGL_FALSE != result);


  static const EGLint contextAttrs[] = {12440, 3, 12539, 2, 12540, 1, 12344, 1, 12323, 1, 12322, 1, 12321, 1, 12344, 32767, -150598962, 32767, -140972593, 32767};
  static const EGLint attribute_list1[] =   {12339, 4, 12352, 64, 12325, 16, 12324, 1, 12323, 1, 12322, 1, 12321, 1, 12344, 32767, -150598930, 32767, -140972593, 32767};
  result =
      eglChooseConfig(state->egl_display, attribute_list1, &(state->config), 1, &num_config);

  g_print("num_config %d\n", num_config);

  // create an EGL rendering context
  state->context =
      eglCreateContext (state->egl_display, state->config, EGL_NO_CONTEXT, context_attributes);
  assert (state->context != EGL_NO_CONTEXT);

  state->surface =
      eglCreateWindowSurface(state->egl_display, state->config, window_handle, NULL);
  assert (state->surface != EGL_NO_SURFACE);

  // connect the context to the surface
  result =
      eglMakeCurrent (state->egl_display, state->surface, state->surface, state->context);
  assert (EGL_FALSE != result);

  g_print("-------------------------init_ogl function end--------------------\n");
}


static void create_shared_context(APP_STATE_T * state)
{
    static const EGLint contextAttrs[] = {12440, 3, 12539, 2, 12540, 1, 12344, 1, 12323, 1, 12322, 1, 12321, 1, 12344, 32767, -150598962, 32767, -140972593, 32767};

    state->m_LocalThreadContext = eglCreateContext(state->egl_display, state->config, state->context, contextAttrs);
    if (!state->m_LocalThreadContext)
        g_print("create_shared_context returned error %d", eglGetError());

    g_print("m_LocalThreadContext successfully created\n");

}


static void create_gst_shared_context(APP_STATE_T * state)
{
    state->gst_display = gst_gl_display_egl_new_with_egl_display(state->egl_display);
    state->x_gst_display = gst_gl_display_x11_new_with_display(state->xdisplay);

    state->gl_context =
        gst_gl_context_new_wrapped(GST_GL_DISPLAY(state->x_gst_display),
        (guintptr)state->context, GST_GL_PLATFORM_EGL, GST_GL_API_GLES2);

    if (!state->gl_context)
        g_print("Error: state->gl_context isn't created!\n");

    GError *error = NULL;
    if (!gst_gl_display_create_context(GST_GL_DISPLAY(state->x_gst_display), state->gl_context, &state->sharedContext, &error))
        g_print("Failed to create new context %s\n", error->message);

    if ( !gst_gl_display_add_context(GST_GL_DISPLAY(state->x_gst_display), state->sharedContext))
        g_print("Failed to add new context to display\n");
}



/***********************************************************
 * Name: init_shaders
 *
 * Arguments:
 *       APP_STATE_T *state - holds OGLES model info
 *
 * Description: Sets the OpenGL|ES model to default values
 *
 * Returns: void
 *
 ***********************************************************/
static void init_shaders(APP_STATE_T * state)
{
  GLint ret = 0;

  state->vshader = glCreateShader (GL_VERTEX_SHADER);

  glShaderSource (state->vshader, 1, &cube_v_src, NULL);
  glCompileShader (state->vshader);
  assert (glGetError () == GL_NO_ERROR);

  state->fshader = glCreateShader (GL_FRAGMENT_SHADER);

  glShaderSource (state->fshader, 1, &cube_f_src, NULL);
  glCompileShader (state->fshader);
  assert (glGetError () == GL_NO_ERROR);

  state->program = glCreateProgram ();

  glAttachShader (state->program, state->vshader);
  glAttachShader (state->program, state->fshader);

  glBindAttribLocation (state->program, 0, "a_position");
  glBindAttribLocation (state->program, 1, "a_texCoord");

  glLinkProgram (state->program);

  glGetProgramiv (state->program, GL_LINK_STATUS, &ret);
  assert (ret == GL_TRUE);

  glUseProgram (state->program);

  state->u_rotx = glGetUniformLocation (state->program, "u_rotx");
  state->u_roty = glGetUniformLocation (state->program, "u_roty");
  state->u_rotz = glGetUniformLocation (state->program, "u_rotz");

  state->u_modelviewmatrix =
      glGetUniformLocation (state->program, "u_modelview");

  state->u_projectionmatrix =
      glGetUniformLocation (state->program, "u_projection");

  state->s_texture = glGetUniformLocation (state->program, "s_texture");

  glViewport (0, 0, (GLsizei) state->screen_width,
      (GLsizei) state->screen_height);

  state->fov = 45.0f;
  state->distance = 5.0f;
  state->aspect =
      (GLfloat) state->screen_width / (GLfloat) state->screen_height;

  gst_gl_matrix_load_identity (&state->projection);
  gst_gl_matrix_perspective (&state->projection, state->fov, state->aspect, 1.0f, 100.0f);

  gst_gl_matrix_load_identity (&state->modelview);
  gst_gl_matrix_translate (&state->modelview, 0.0f, 0.0f, -state->distance);

}



/***********************************************************
 * Name: draw_triangle
 *
 * Arguments:
 *       APP_STATE_T *state - holds OGLES model info
 *
 * Description:   Draws the model and calls eglSwapBuffers
 *                to render to screen
 *
 * Returns: void
 *
 ***********************************************************/
static void draw_triangle(APP_STATE_T * state)
{
  glBindFramebuffer (GL_FRAMEBUFFER, 0);

  glEnable (GL_CULL_FACE);
  glEnable (GL_DEPTH_TEST);

  /* Set background color and clear buffers */
  glClearColor (1.0f, 1.0f, 1.0f, 1.0f);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glUseProgram (state->program);

  glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, 0, quadx);
  glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 0, texCoords);

  glEnableVertexAttribArray (0);
  glEnableVertexAttribArray (1);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

  glGenTextures(1, &state->tex);
  glBindTexture(GL_TEXTURE_2D, state->tex);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, state->screen_width, state->screen_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state->tex, 0);

  GLenum status;
  if ((status = glCheckFramebufferStatus(GL_FRAMEBUFFER)) != GL_FRAMEBUFFER_COMPLETE) {
			g_print("glCheckFramebufferStatus: error %d", status );
		}
  glUniform1i (state->s_texture, 0);

  glUniform1f (state->u_rotx, state->rot_angle_x);
  glUniform1f (state->u_roty, state->rot_angle_y);
  glUniform1f (state->u_rotz, state->rot_angle_z);

  glUniformMatrix4fv (state->u_modelviewmatrix, 1, GL_FALSE,  &state->modelview.m[0][0]);

  glUniformMatrix4fv (state->u_projectionmatrix, 1, GL_FALSE, &state->projection.m[0][0]);

  /* draw first 4 vertices */
  glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
  glDrawArrays (GL_TRIANGLE_STRIP, 4, 4);
  glDrawArrays (GL_TRIANGLE_STRIP, 8, 4);
  glDrawArrays (GL_TRIANGLE_STRIP, 12, 4);
  glDrawArrays (GL_TRIANGLE_STRIP, 16, 4);
  glDrawArrays (GL_TRIANGLE_STRIP, 20, 4);

  if (!eglSwapBuffers (state->egl_display, state->surface)) {
    //g_main_loop_quit (state->main_loop);
    return;
  }

  glDisable (GL_DEPTH_TEST);
  glDisable (GL_CULL_FACE);

}


/*static gpointer render_func(gpointer data)
{
    if (!eglMakeCurrent(state->display, state->surface, state->surface, state->m_LocalThreadContext))
    {
        g_print("eglMakeCurrent(m_LocalThreadContext) returned error %d", eglGetError());
    }
    g_print("eglMakeCurrent(m_LocalThreadContext) successfull\n");

   // Save the texture to tga file.
    save_tga(state, "tga_file2.tga");
    return NULL;
}*/

int main (int argc, char **argv)
{
    GstBus *bus;
    GOptionContext *ctx;
    GIOChannel *io_stdin;
    GError *err = NULL;
    gboolean res;
    GOptionEntry options[] = {
    {NULL}
    };
    GThread *rthread;

    XInitThreads();
    // Clear application state.
    memset (state, 0, sizeof (*state));
    state->animate = TRUE;
    state->caps = NULL;

    // Initialize GStreamer.
    gst_init (NULL, NULL);
    g_print("gstreamer is initialized\n");

    // Create surface and gl context.
    init_egl(state);

    //create_shared_context(state);
    create_gst_shared_context(state);

    init_shaders(state);
    draw_triangle(state);

    //if (!eglMakeCurrent (state->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT))
    //    g_print("eglMakeCurrent(EGL_NO_SURFACE) returned error %d\n", eglGetError());

    // Save the texture to tga file.
    save_tga(state, "tga_file.tga");

     gst_gl_context_thread_add(
        state->sharedContext,
        (GstGLContextThreadFunc)save_gst_tga,
        state
    );


   /*if (!(rthread = g_thread_new("render", (GThreadFunc)render_func, NULL)))
   {
       g_print ("Render thread create failed\n");
       exit (1);
   }
   g_thread_join (rthread);*/

    sleep(2);

  return 0;
}

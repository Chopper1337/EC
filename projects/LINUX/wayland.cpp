//
// Native Wayland overlay backend.
//
// Renders through the wlr-layer-shell protocol: an OVERLAY-layer surface anchored to
// all edges of one output, empty input region (click-through), no keyboard
// interactivity. The compositor guarantees the surface sits above all normal windows
// including fullscreen clients, so no override-redirect/restacking/compositor
// transparency hacks are needed (unlike the SDL/X11 path).
//
// Drawing is delegated to a minimal OpenGL rect renderer fed by client::DrawRect /
// client::DrawFillRect (see main.cpp routing).
//

#include "wayland.h"

#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <stdio.h>
#include <wayland-client.h>
#include <wayland-egl.h>

namespace wl {

//
// wayland objects
//
static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_registry *registry;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wl_surface *surface;
static struct zwlr_layer_surface_v1 *layer_surface;
static struct wl_egl_window *egl_window;

struct output_info {
  struct wl_output *output;
  int32_t x, y;
  int32_t width, height;
};

static struct output_info outputs[16];
static int num_outputs;

static int32_t origin_x, origin_y;
static int width, height;

static volatile BOOL configured;
static volatile BOOL closed_flag;
static volatile BOOL resize_pending;

//
// egl
//
static EGLDisplay egl_display;
static EGLSurface egl_surface;
static EGLContext egl_context;

//
// gl rect renderer
//
static GLuint program;
static GLint uniform_resolution;
static GLint uniform_color;
static GLint attribute_position;

//
// forward declarations
//
static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version);
static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name);
static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t w, uint32_t h);
static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *surface);

static const struct wl_registry_listener registry_listener = {
    registry_global, registry_global_remove};

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    layer_surface_configure, layer_surface_closed};

static void output_geometry(void *data, struct wl_output *output, int32_t fx,
                            int32_t fy, int32_t pw, int32_t ph, int32_t subpixel,
                            const char *make, const char *model,
                            int32_t transform);

static void output_mode(void *data, struct wl_output *output, uint32_t flags,
                        int32_t w, int32_t h, int32_t refresh);

static void output_done(void *data, struct wl_output *output);
static void output_scale(void *data, struct wl_output *output, int32_t factor);

static const struct wl_output_listener output_listener = {
    output_geometry, output_mode, output_done, output_scale};

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
                            const char *interface, uint32_t version) {
  if (!strcmp(interface, "wl_compositor")) {
    compositor = (struct wl_compositor *)wl_registry_bind(
        reg, name, &wl_compositor_interface, version < 5 ? version : 5);
  } else if (!strcmp(interface, "zwlr_layer_shell_v1")) {
    layer_shell = (struct zwlr_layer_shell_v1 *)wl_registry_bind(
        reg, name, &zwlr_layer_shell_v1_interface, version < 4 ? version : 4);
  } else if (!strcmp(interface, "wl_output") &&
             num_outputs < (int)(sizeof(outputs) / sizeof(*outputs))) {
    struct output_info *info = &outputs[num_outputs++];
    memset(info, 0, sizeof(*info));
    info->output = (struct wl_output *)wl_registry_bind(
        reg, name, &wl_output_interface, version < 3 ? version : 3);
    wl_output_add_listener(info->output, &output_listener, info);
  }
}

static void registry_global_remove(void *data, struct wl_registry *reg,
                                   uint32_t name) {}

static void output_geometry(void *data, struct wl_output *output, int32_t fx,
                            int32_t fy, int32_t pw, int32_t ph, int32_t subpixel,
                            const char *make, const char *model,
                            int32_t transform) {
  struct output_info *info = (struct output_info *)data;
  info->x = fx;
  info->y = fy;
}

static void output_mode(void *data, struct wl_output *output, uint32_t flags,
                        int32_t w, int32_t h, int32_t refresh) {
  //
  // several modes are advertised; only the current one is the live resolution
  //
  if (flags & WL_OUTPUT_MODE_CURRENT) {
    struct output_info *info = (struct output_info *)data;
    info->width = w;
    info->height = h;
  }
}

static void output_done(void *data, struct wl_output *output) {}

static void output_scale(void *data, struct wl_output *output, int32_t factor) {}

static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *lsurface,
                                    uint32_t serial, uint32_t w, uint32_t h) {
  zwlr_layer_surface_v1_ack_configure(lsurface, serial);

  if ((int)w > 0 && (int)h > 0 && ((int)w != width || (int)h != height)) {
    width = (int)w;
    height = (int)h;
    resize_pending = 1;
  }

  configured = 1;
}

static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *lsurface) {
  closed_flag = 1;
}

static GLuint compile_shader(GLenum type, const char *source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, 0);
  glCompileShader(shader);

  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static BOOL init_renderer(void) {
  const char *vertex =
      "attribute vec2 position;"
      "uniform vec2 resolution;"
      "void main() {"
      "  vec2 ndc = position / resolution * 2.0;"
      "  gl_Position = vec4(ndc.x - 1.0, 1.0 - ndc.y, 0.0, 1.0);"
      "}";

  const char *fragment =
      "uniform vec4 color;"
      "void main() {"
      "  gl_FragColor = color;"
      "}";

  GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex);
  GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment);
  if (!vs || !fs) {
    return 0;
  }

  program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    return 0;
  }

  uniform_resolution = glGetUniformLocation(program, "resolution");
  uniform_color = glGetUniformLocation(program, "color");
  attribute_position = glGetAttribLocation(program, "position");

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  return 1;
}

static void draw_rect_internal(float x, float y, float w, float h,
                               unsigned char r, unsigned char g, unsigned char b,
                               unsigned char a, BOOL outline) {
  //
  // output-local coordinates
  //
  x -= (float)origin_x;
  y -= (float)origin_y;

  float verts[12];
  int count;

  if (outline) {
    float quad[8] = {x,     y,     x + w, y,
                     x + w, y + h, x,     y + h};
    memcpy(verts, quad, sizeof(quad));
    count = 4;
  } else {
    float tri[12] = {x,     y,     x + w, y,     x,     y + h,
                     x,     y + h, x + w, y,     x + w, y + h};
    memcpy(verts, tri, sizeof(tri));
    count = 6;
  }

  glUseProgram(program);
  glUniform2f(uniform_resolution, (float)width, (float)height);
  glUniform4f(uniform_color, (float)r / 255.0f, (float)g / 255.0f,
              (float)b / 255.0f, (float)a / 255.0f);

  glVertexAttribPointer(attribute_position, 2, GL_FLOAT, GL_FALSE, 0, verts);
  glEnableVertexAttribArray(attribute_position);
  glDrawArrays(outline ? GL_LINE_LOOP : GL_TRIANGLES, 0, count);
  glDisableVertexAttribArray(attribute_position);
}

BOOL init(void) {
  const char *session = getenv("XDG_SESSION_TYPE");
  if (!getenv("WAYLAND_DISPLAY") &&
      !(session && !strcmp(session, "wayland"))) {
    return 0;
  }

  display = wl_display_connect(0);
  if (!display) {
    return 0;
  }

  registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, 0);

  //
  // roundtrip 1: bind globals, roundtrip 2: output geometry/mode events
  //
  if (wl_display_roundtrip(display) < 0) {
    return 0;
  }
  if (!compositor || !layer_shell) {
    printf("[EC] wayland: compositor or wlr-layer-shell unavailable\n");
    return 0;
  }
  if (wl_display_roundtrip(display) < 0) {
    return 0;
  }

  //
  // pick target output: EC_WAYLAND_OUTPUT (0-based), default 0
  //
  int target = 0;
  {
    const char *env = getenv("EC_WAYLAND_OUTPUT");
    if (env) {
      int value = atoi(env);
      if (value >= 0 && value < num_outputs) {
        target = value;
      }
    }
  }

  printf("[EC] wayland: %d output(s), using output %d\n", num_outputs, target);
  for (int i = 0; i < num_outputs; i++) {
    printf("[EC] wayland output [%d]: %dx%d at (%d, %d)%s\n", i, outputs[i].width,
        outputs[i].height, outputs[i].x, outputs[i].y,
        i == target ? " (selected)" : "");
  }

  if (target >= num_outputs) {
    return 0;
  }

  origin_x = outputs[target].x;
  origin_y = outputs[target].y;

  surface = wl_compositor_create_surface(compositor);
  if (!surface) {
    return 0;
  }

  layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      layer_shell, surface, outputs[target].output,
      ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "ec.overlay");
  if (!layer_surface) {
    return 0;
  }
  zwlr_layer_surface_v1_add_listener(layer_surface, &layer_surface_listener, 0);

  //
  // span the whole output; a zero size with all four anchors sizes us to the
  // output. negative exclusive zone keeps us over other surfaces without
  // reserving space, and no keyboard interactivity keeps focus with the game.
  //
  zwlr_layer_surface_v1_set_anchor(
      layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                         ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                         ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                         ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  zwlr_layer_surface_v1_set_size(layer_surface, 0, 0);
  zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, -1);
  zwlr_layer_surface_v1_set_keyboard_interactivity(
      layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

  //
  // empty input region => all input passes through to the game underneath
  //
  struct wl_region *region = wl_compositor_create_region(compositor);
  wl_surface_set_input_region(surface, region);
  wl_region_destroy(region);

  wl_surface_commit(surface);

  while (!configured && !closed_flag) {
    if (wl_display_dispatch(display) < 0) {
      return 0;
    }
  }
  if (closed_flag) {
    return 0;
  }

  if (width <= 0 || height <= 0) {
    width = outputs[target].width;
    height = outputs[target].height;
  }
  if (width <= 0 || height <= 0) {
    printf("[EC] wayland: compositor configured a zero-sized surface\n");
    return 0;
  }
  printf("[EC] wayland: layer surface configured to %dx%d\n", width, height);

  //
  // egl
  //
  egl_display = eglGetDisplay((EGLNativeDisplayType)display);
  if (egl_display == EGL_NO_DISPLAY) {
    return 0;
  }

  EGLint major, minor;
  if (!eglInitialize(egl_display, &major, &minor)) {
    return 0;
  }

  const EGLint config_attribs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                   EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                   EGL_RED_SIZE, 8,
                                   EGL_GREEN_SIZE, 8,
                                   EGL_BLUE_SIZE, 8,
                                   EGL_ALPHA_SIZE, 8,
                                   EGL_NONE};

  EGLConfig config;
  EGLint num_configs;
  if (!eglChooseConfig(egl_display, config_attribs, &config, 1, &num_configs) ||
      num_configs == 0) {
    return 0;
  }

  if (!eglBindAPI(EGL_OPENGL_API)) {
    return 0;
  }

  const EGLint context_attribs[] = {EGL_CONTEXT_MAJOR_VERSION, 2,
                                    EGL_CONTEXT_MINOR_VERSION, 1,
                                    EGL_NONE};

  egl_context =
      eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);
  if (egl_context == EGL_NO_CONTEXT) {
    return 0;
  }

  egl_window = wl_egl_window_create(surface, width, height);
  if (!egl_window) {
    return 0;
  }

  egl_surface = eglCreateWindowSurface(
      egl_display, config, (EGLNativeWindowType)egl_window, 0);
  if (egl_surface == EGL_NO_SURFACE) {
    return 0;
  }

  if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
    return 0;
  }

  //
  // the render loop paces itself
  //
  eglSwapInterval(egl_display, 0);

  if (!init_renderer()) {
    printf("[EC] wayland: renderer init failed\n");
    return 0;
  }

  resize_pending = 0;
  return 1;
}

void shutdown(void) {
  if (egl_display != EGL_NO_DISPLAY) {
    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    if (egl_surface != EGL_NO_SURFACE) {
      eglDestroySurface(egl_display, egl_surface);
    }
    if (egl_context != EGL_NO_CONTEXT) {
      eglDestroyContext(egl_display, egl_context);
    }
    eglTerminate(egl_display);
    egl_display = EGL_NO_DISPLAY;
  }

  if (egl_window) {
    wl_egl_window_destroy(egl_window);
    egl_window = 0;
  }

  if (layer_surface) {
    zwlr_layer_surface_v1_destroy(layer_surface);
    layer_surface = 0;
  }
  if (layer_shell) {
    zwlr_layer_shell_v1_destroy(layer_shell);
    layer_shell = 0;
  }
  if (surface) {
    wl_surface_destroy(surface);
    surface = 0;
  }
  for (int i = 0; i < num_outputs; i++) {
    if (outputs[i].output) {
      wl_output_destroy(outputs[i].output);
    }
  }
  num_outputs = 0;
  if (registry) {
    wl_registry_destroy(registry);
    registry = 0;
  }
  if (display) {
    wl_display_disconnect(display);
    display = 0;
  }
}

void pump(void) {
  if (!display) {
    return;
  }

  //
  // non-blocking event pump: flush requests, drain queued events, then read
  // pending socket data only if it is ready (never block the render loop)
  //
  wl_display_flush(display);
  wl_display_dispatch_pending(display);

  if (wl_display_prepare_read(display) != 0) {
    wl_display_dispatch_pending(display);
    return;
  }

  struct pollfd pfd;
  pfd.fd = wl_display_get_fd(display);
  pfd.events = POLLIN;
  pfd.revents = 0;

  if (poll(&pfd, 1, 0) > 0) {
    wl_display_read_events(display);
    wl_display_dispatch_pending(display);
  } else {
    wl_display_cancel_read(display);
  }

  if (resize_pending && egl_window) {
    resize_pending = 0;
    wl_egl_window_resize(egl_window, width, height, 0, 0);
    glViewport(0, 0, width, height);
    printf("[EC] wayland: layer surface resized to %dx%d\n", width, height);
  }
}

BOOL should_close(void) { return closed_flag; }

void frame_begin(void) {
  glViewport(0, 0, width, height);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);
}

void frame_end(void) {
  eglSwapBuffers(egl_display, egl_surface);
  wl_display_flush(display);
}

void draw_fill_rect(float x, float y, float w, float h, unsigned char r,
                    unsigned char g, unsigned char b) {
  draw_rect_internal(x, y, w, h, r, g, b, 255, 0);
}

void draw_rect(float x, float y, float w, float h, unsigned char r,
               unsigned char g, unsigned char b) {
  //
  // matches the SDL path: translucent fill + solid border
  //
  draw_rect_internal(x, y, w, h, r, g, b, 50, 0);

  glLineWidth(1);
  draw_rect_internal(x, y, w, h, 0, 0, 0, 255, 1);
}

} // namespace wl

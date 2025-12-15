#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>
#include <android/hardware_buffer.h>
#include <string.h>
#include "renderer.h"
#include "atlas.inl"

#define BUFFER_SIZE 16384

static GLfloat   tex_buf[BUFFER_SIZE *  8];
static GLfloat  vert_buf[BUFFER_SIZE *  8];
static GLubyte color_buf[BUFFER_SIZE * 16];
static GLuint  index_buf[BUFFER_SIZE *  6];

static int width  = 800;
static int height = 600;
static int buf_idx;
static float ui_scale = 2.0f;

static EGLDisplay egl_display;
static EGLSurface egl_surface;
static EGLContext egl_context;

static GLuint shader_program;
static GLuint vao, vbo_vert, vbo_tex, vbo_color, ebo;
static GLuint texture_id;

static const char *vertex_shader_src =
    "#version 300 es\n"
    "layout (location = 0) in vec2 aPos;\n"
    "layout (location = 1) in vec2 aTexCoord;\n"
    "layout (location = 2) in vec4 aColor;\n"
    "out vec2 TexCoord;\n"
    "out vec4 Color;\n"
    "uniform mat4 projection;\n"
    "void main() {\n"
    "    gl_Position = projection * vec4(aPos, 0.0, 1.0);\n"
    "    TexCoord = aTexCoord;\n"
    "    Color = aColor;\n"
    "}\n";

static const char *fragment_shader_src =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 TexCoord;\n"
    "in vec4 Color;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D tex;\n"
    "void main() {\n"
    "    float alpha = texture(tex, TexCoord).r;\n"
    "    FragColor = vec4(Color.rgb, Color.a * alpha);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    return shader;
}

static void setup_shader(void) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);
    shader_program = glCreateProgram();
    glAttachShader(shader_program, vs);
    glAttachShader(shader_program, fs);
    glLinkProgram(shader_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
}

static void setup_buffers(void) {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo_vert);
    glGenBuffers(1, &vbo_tex);
    glGenBuffers(1, &vbo_color);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_vert);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vert_buf), NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_tex);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tex_buf), NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_color);
    glBufferData(GL_ARRAY_BUFFER, sizeof(color_buf), NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index_buf), NULL, GL_DYNAMIC_DRAW);

    glBindVertexArray(0);
}

void r_get_size(int *w, int *h) {
    *w = width;
    *h = height;
}

void r_set_size(int w, int h) {
    width = w;
    height = h;
}

void r_set_scale(float scale) {
    ui_scale = scale;
}

void r_get_logical_size(int *w, int *h) {
    *w = (int)(width / ui_scale);
    *h = (int)(height / ui_scale);
}

int r_init(EGLNativeDisplayType native_display, EGLNativeWindowType native_window) {
    ANativeWindow_setBuffersGeometry(native_window, 0, 0, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);

    egl_display = eglGetDisplay(native_display);
    if (egl_display == EGL_NO_DISPLAY) return -1;

    if (!eglInitialize(egl_display, NULL, NULL)) return -1;

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs;
    if (!eglChooseConfig(egl_display, config_attribs, &config, 1, &num_configs)) return -1;

    EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) return -1;

    egl_surface = eglCreateWindowSurface(egl_display, config, native_window, NULL);
    if (egl_surface == EGL_NO_SURFACE) return -1;

    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

    eglSwapInterval(egl_display, 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);

    setup_shader();
    setup_buffers();

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_WIDTH, ATLAS_HEIGHT, 0,
        GL_RED, GL_UNSIGNED_BYTE, atlas_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    return 0;
}

void r_destroy(void) {
    glDeleteTextures(1, &texture_id);
    glDeleteBuffers(1, &vbo_vert);
    glDeleteBuffers(1, &vbo_tex);
    glDeleteBuffers(1, &vbo_color);
    glDeleteBuffers(1, &ebo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(shader_program);
    eglDestroySurface(egl_display, egl_surface);
    eglDestroyContext(egl_display, egl_context);
    eglTerminate(egl_display);
}

static void flush(void) {
    if (buf_idx == 0) return;

    glViewport(0, 0, width, height);
    glUseProgram(shader_program);

    float sw = width / ui_scale;
    float sh = height / ui_scale;
    float projection[16] = {
        2.0f/sw, 0, 0, 0,
        0, -2.0f/sh, 0, 0,
        0, 0, -1, 0,
        -1, 1, 0, 1
    };
    glUniformMatrix4fv(glGetUniformLocation(shader_program, "projection"), 1, GL_FALSE, projection);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_vert);
    glBufferSubData(GL_ARRAY_BUFFER, 0, buf_idx * 8 * sizeof(GLfloat), vert_buf);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_tex);
    glBufferSubData(GL_ARRAY_BUFFER, 0, buf_idx * 8 * sizeof(GLfloat), tex_buf);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_color);
    glBufferSubData(GL_ARRAY_BUFFER, 0, buf_idx * 16 * sizeof(GLubyte), color_buf);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, buf_idx * 6 * sizeof(GLuint), index_buf);

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, buf_idx * 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    buf_idx = 0;
}

static void push_quad(mu_Rect dst, mu_Rect src, mu_Color color) {
    if (buf_idx == BUFFER_SIZE) flush();

    int texvert_idx = buf_idx * 8;
    int color_idx = buf_idx * 16;
    int element_idx = buf_idx * 4;
    int index_idx = buf_idx * 6;
    buf_idx++;

    float x = src.x / (float)ATLAS_WIDTH;
    float y = src.y / (float)ATLAS_HEIGHT;
    float w = src.w / (float)ATLAS_WIDTH;
    float h = src.h / (float)ATLAS_HEIGHT;

    tex_buf[texvert_idx + 0] = x;     tex_buf[texvert_idx + 1] = y;
    tex_buf[texvert_idx + 2] = x + w; tex_buf[texvert_idx + 3] = y;
    tex_buf[texvert_idx + 4] = x;     tex_buf[texvert_idx + 5] = y + h;
    tex_buf[texvert_idx + 6] = x + w; tex_buf[texvert_idx + 7] = y + h;

    vert_buf[texvert_idx + 0] = dst.x;         vert_buf[texvert_idx + 1] = dst.y;
    vert_buf[texvert_idx + 2] = dst.x + dst.w; vert_buf[texvert_idx + 3] = dst.y;
    vert_buf[texvert_idx + 4] = dst.x;         vert_buf[texvert_idx + 5] = dst.y + dst.h;
    vert_buf[texvert_idx + 6] = dst.x + dst.w; vert_buf[texvert_idx + 7] = dst.y + dst.h;

    memcpy(color_buf + color_idx +  0, &color, 4);
    memcpy(color_buf + color_idx +  4, &color, 4);
    memcpy(color_buf + color_idx +  8, &color, 4);
    memcpy(color_buf + color_idx + 12, &color, 4);

    index_buf[index_idx + 0] = element_idx + 0;
    index_buf[index_idx + 1] = element_idx + 1;
    index_buf[index_idx + 2] = element_idx + 2;
    index_buf[index_idx + 3] = element_idx + 2;
    index_buf[index_idx + 4] = element_idx + 3;
    index_buf[index_idx + 5] = element_idx + 1;
}

void r_draw_rect(mu_Rect rect, mu_Color color) {
    push_quad(rect, atlas[ATLAS_WHITE], color);
}

void r_draw_text(const char *text, mu_Vec2 pos, mu_Color color) {
    mu_Rect dst = { pos.x, pos.y, 0, 0 };
    for (const char *p = text; *p; p++) {
        if ((*p & 0xc0) == 0x80) continue;
        int chr = mu_min((unsigned char)*p, 127);
        mu_Rect src = atlas[ATLAS_FONT + chr];
        dst.w = src.w;
        dst.h = src.h;
        push_quad(dst, src, color);
        dst.x += dst.w;
    }
}

void r_draw_icon(int id, mu_Rect rect, mu_Color color) {
    mu_Rect src = atlas[id];
    int x = rect.x + (rect.w - src.w) / 2;
    int y = rect.y + (rect.h - src.h) / 2;
    push_quad(mu_rect(x, y, src.w, src.h), src, color);
}

int r_get_text_width(const char *text, int len) {
    int res = 0;
    for (const char *p = text; *p && len--; p++) {
        if ((*p & 0xc0) == 0x80) continue;
        int chr = mu_min((unsigned char)*p, 127);
        res += atlas[ATLAS_FONT + chr].w;
    }
    return res;
}

int r_get_text_height(void) { return 18; }

void r_set_clip_rect(mu_Rect rect) {
    flush();
    glScissor(rect.x * ui_scale, height - (rect.y + rect.h) * ui_scale, rect.w * ui_scale, rect.h * ui_scale);
}

void r_clear(mu_Color clr) {
    flush();
    glClearColor(clr.r / 255.f, clr.g / 255.f, clr.b / 255.f, clr.a / 255.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void r_present(void) {
    flush();
    eglSwapBuffers(egl_display, egl_surface);
}

#include "renderer.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cctype>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "ArcadeRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) do { printf("[ArcadeRenderer INFO] " __VA_ARGS__); printf("\n"); } while(0)
#define LOGE(...) do { fprintf(stderr, "[ArcadeRenderer ERROR] " __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace arcade {

ViewportRect calculate_arcade_viewport(int screen_w, int screen_h, float aspect_ratio) {
    ViewportRect vp;
    if (screen_w <= 0 || screen_h <= 0) return vp;
    if (aspect_ratio <= 0.0f) aspect_ratio = 4.0f / 3.0f;

    float screen_ar = static_cast<float>(screen_w) / static_cast<float>(screen_h);
    if (screen_ar > aspect_ratio) {
        // Ultrawide screen (18:9, 19.5:9, 20:9) -> Pillarbox (bars left & right)
        vp.height = screen_h;
        vp.width = static_cast<int>(std::round(screen_h * aspect_ratio));
        vp.x = (screen_w - vp.width) / 2;
        vp.y = 0;
    } else {
        // Taller screen -> Letterbox (bars top & bottom)
        vp.width = screen_w;
        vp.height = static_cast<int>(std::round(screen_w / aspect_ratio));
        vp.x = 0;
        vp.y = (screen_h - vp.height) / 2;
    }
    return vp;
}

Renderer::Renderer() {
    game_viewport_ = calculate_arcade_viewport(screen_w_, screen_h_, game_aspect_ratio_);
}

Renderer::~Renderer() {
    shutdown();
}

#ifdef __ANDROID__

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char info_log[512];
        glGetShaderInfoLog(shader, sizeof(info_log), nullptr, info_log);
        LOGE("Shader compilation error: %s", info_log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char info_log[512];
        glGetProgramInfoLog(program, sizeof(info_log), nullptr, info_log);
        LOGE("Program link error: %s", info_log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

#endif // __ANDROID__

void Renderer::init_game_pipeline() {
#ifdef __ANDROID__
    const char* vs_src = R"glsl(#version 300 es
    layout(location = 0) in vec2 a_pos;
    layout(location = 1) in vec2 a_texcoord;
    out vec2 v_texcoord;
    void main() {
        gl_Position = vec4(a_pos, 0.0, 1.0);
        v_texcoord = a_texcoord;
    }
    )glsl";

    const char* fs_src = R"glsl(#version 300 es
    precision mediump float;
    in vec2 v_texcoord;
    out vec4 fragColor;
    uniform sampler2D u_texture;
    uniform int u_format; // 0 = 0RGB1555, 1 = XRGB8888, 2 = RGB565

    void main() {
        vec4 color = texture(u_texture, v_texcoord);
        if (u_format == 1) {
            // XRGB8888 -> ARGB/BGRA conversion if needed
            fragColor = vec4(color.b, color.g, color.r, 1.0);
        } else {
            fragColor = vec4(color.rgb, 1.0);
        }
    }
    )glsl";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    game_program_ = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    u_game_texture_loc_ = glGetUniformLocation(game_program_, "u_texture");
    u_game_format_loc_ = glGetUniformLocation(game_program_, "u_format");

    // Fullscreen quad vertices (pos.xy, texcoord.xy)
    static const float quad_vertices[] = {
        // pos.x, pos.y,  tex.u, tex.v
        -1.0f, -1.0f,    0.0f, 1.0f,
         1.0f, -1.0f,    1.0f, 1.0f,
        -1.0f,  1.0f,    0.0f, 0.0f,
         1.0f,  1.0f,    1.0f, 0.0f,
    };

    glGenVertexArrays(1, &game_vao_);
    glGenBuffers(1, &game_vbo_);

    glBindVertexArray(game_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, game_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);

    // Create game screen texture
    glGenTextures(1, &game_texture_);
    glBindTexture(GL_TEXTURE_2D, game_texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // Sharp arcade pixel art
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
#endif
}

void Renderer::init_overlay_pipeline() {
#ifdef __ANDROID__
    const char* vs_src = R"glsl(#version 300 es
    layout(location = 0) in vec2 a_pos;
    uniform mat4 u_proj;
    void main() {
        gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
    }
    )glsl";

    const char* fs_src = R"glsl(#version 300 es
    precision mediump float;
    uniform vec4 u_color;
    out vec4 fragColor;
    void main() {
        fragColor = u_color;
    }
    )glsl";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    overlay_program_ = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    u_overlay_proj_loc_ = glGetUniformLocation(overlay_program_, "u_proj");
    u_overlay_color_loc_ = glGetUniformLocation(overlay_program_, "u_color");

    glGenVertexArrays(1, &overlay_vao_);
    glGenBuffers(1, &overlay_vbo_);

    glBindVertexArray(overlay_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, overlay_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindVertexArray(0);
#endif
}

bool Renderer::init(int screen_w, int screen_h) {
    screen_w_ = screen_w;
    screen_h_ = screen_h;
    game_viewport_ = calculate_arcade_viewport(screen_w_, screen_h_, game_aspect_ratio_);

    init_game_pipeline();
    init_overlay_pipeline();

    initialized_ = true;
    LOGI("Arcade Renderer initialized: Screen (%dx%d), Viewport (%d, %d, %dx%d)",
         screen_w_, screen_h_, game_viewport_.x, game_viewport_.y, game_viewport_.width, game_viewport_.height);
    return true;
}

void Renderer::shutdown() {
#ifdef __ANDROID__
    if (game_texture_) { glDeleteTextures(1, &game_texture_); game_texture_ = 0; }
    if (game_vbo_) { glDeleteBuffers(1, &game_vbo_); game_vbo_ = 0; }
    if (game_vao_) { glDeleteVertexArrays(1, &game_vao_); game_vao_ = 0; }
    if (game_program_) { glDeleteProgram(game_program_); game_program_ = 0; }

    if (overlay_vbo_) { glDeleteBuffers(1, &overlay_vbo_); overlay_vbo_ = 0; }
    if (overlay_vao_) { glDeleteVertexArrays(1, &overlay_vao_); overlay_vao_ = 0; }
    if (overlay_program_) { glDeleteProgram(overlay_program_); overlay_program_ = 0; }
#endif
    initialized_ = false;
}

void Renderer::on_surface_changed(int screen_w, int screen_h) {
    screen_w_ = screen_w;
    screen_h_ = screen_h;
    game_viewport_ = calculate_arcade_viewport(screen_w_, screen_h_, game_aspect_ratio_);
}

void Renderer::set_pixel_format(enum retro_pixel_format format) {
    pixel_format_ = format;
}

void Renderer::set_aspect_ratio(float aspect_ratio) {
    if (aspect_ratio > 0.0f) {
        game_aspect_ratio_ = aspect_ratio;
        game_viewport_ = calculate_arcade_viewport(screen_w_, screen_h_, game_aspect_ratio_);
    }
}

void Renderer::update_frame(const void* data, unsigned width, unsigned height, size_t pitch) {
    // SEC-03: Reject NULL, invalid pointers, and hardware rendering sentinel
    if (!data || data == RETRO_HW_FRAME_BUFFER_VALID || width == 0 || height == 0) return;

    frame_width_ = width;
    frame_height_ = height;
    has_new_frame_ = true;

#ifdef __ANDROID__
    glBindTexture(GL_TEXTURE_2D, game_texture_);

    GLenum gl_format = GL_RGB;
    GLenum gl_type = GL_UNSIGNED_SHORT_5_6_5;

    if (pixel_format_ == RETRO_PIXEL_FORMAT_XRGB8888) {
        gl_format = GL_RGBA;
        gl_type = GL_UNSIGNED_BYTE;
    } else if (pixel_format_ == RETRO_PIXEL_FORMAT_0RGB1555) {
        gl_format = GL_RGBA;
        gl_type = GL_UNSIGNED_SHORT_5_5_5_1;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / (gl_type == GL_UNSIGNED_BYTE ? 4 : 2));

    if (width != last_tex_w_ || height != last_tex_h_ || gl_format != last_tex_format_) {
        glTexImage2D(GL_TEXTURE_2D, 0, gl_format, width, height, 0, gl_format, gl_type, data);
        last_tex_w_ = width;
        last_tex_h_ = height;
        last_tex_format_ = gl_format;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, gl_format, gl_type, data);
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
#else
    (void)pitch;
#endif
}

void Renderer::draw_circle(float cx, float cy, float radius, uint32_t color_rgba, float alpha_mult) {
#ifdef __ANDROID__
    constexpr int SEGMENTS = 32;
    float vertices[(SEGMENTS + 2) * 2];

    vertices[0] = cx;
    vertices[1] = cy;
    for (int i = 0; i <= SEGMENTS; ++i) {
        float angle = (float)i * 2.0f * 3.14159265f / (float)SEGMENTS;
        vertices[(i + 1) * 2 + 0] = cx + std::cos(angle) * radius;
        vertices[(i + 1) * 2 + 1] = cy + std::sin(angle) * radius;
    }

    float r = ((color_rgba >> 24) & 0xFF) / 255.0f;
    float g = ((color_rgba >> 16) & 0xFF) / 255.0f;
    float b = ((color_rgba >> 8) & 0xFF) / 255.0f;
    float a = ((color_rgba) & 0xFF) / 255.0f * alpha_mult;

    glUniform4f(u_overlay_color_loc_, r, g, b, a);

    glBindBuffer(GL_ARRAY_BUFFER, overlay_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLE_FAN, 0, SEGMENTS + 2);
#else
    (void)cx; (void)cy; (void)radius; (void)color_rgba; (void)alpha_mult;
#endif
}

void Renderer::draw_circle_ring(float cx, float cy, float inner_r, float outer_r, uint32_t color_rgba, float alpha_mult) {
#ifdef __ANDROID__
    if (inner_r < 0.0f) inner_r = 0.0f;
    if (outer_r <= inner_r) return;

    constexpr int SEGMENTS = 32;
    float vertices[(SEGMENTS + 1) * 4];

    for (int i = 0; i <= SEGMENTS; ++i) {
        float angle = (float)i * 2.0f * 3.14159265f / (float)SEGMENTS;
        float c = std::cos(angle);
        float s = std::sin(angle);
        vertices[i * 4 + 0] = cx + c * inner_r;
        vertices[i * 4 + 1] = cy + s * inner_r;
        vertices[i * 4 + 2] = cx + c * outer_r;
        vertices[i * 4 + 3] = cy + s * outer_r;
    }

    float r = ((color_rgba >> 24) & 0xFF) / 255.0f;
    float g = ((color_rgba >> 16) & 0xFF) / 255.0f;
    float b = ((color_rgba >> 8) & 0xFF) / 255.0f;
    float a = ((color_rgba) & 0xFF) / 255.0f * alpha_mult;

    glUniform4f(u_overlay_color_loc_, r, g, b, a);

    glBindBuffer(GL_ARRAY_BUFFER, overlay_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, (SEGMENTS + 1) * 2);
#else
    (void)cx; (void)cy; (void)inner_r; (void)outer_r; (void)color_rgba; (void)alpha_mult;
#endif
}

void Renderer::draw_rect(float x, float y, float w, float h, uint32_t color_rgba, float alpha_mult) {
#ifdef __ANDROID__
    float vertices[8] = {
        x,     y,
        x + w, y,
        x,     y + h,
        x + w, y + h
    };

    float r = ((color_rgba >> 24) & 0xFF) / 255.0f;
    float g = ((color_rgba >> 16) & 0xFF) / 255.0f;
    float b = ((color_rgba >> 8) & 0xFF) / 255.0f;
    float a = ((color_rgba) & 0xFF) / 255.0f * alpha_mult;

    glUniform4f(u_overlay_color_loc_, r, g, b, a);

    glBindBuffer(GL_ARRAY_BUFFER, overlay_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
#else
    (void)x; (void)y; (void)w; (void)h; (void)color_rgba; (void)alpha_mult;
#endif
}

void Renderer::draw_line(float x1, float y1, float x2, float y2, float thickness, uint32_t color_rgba, float alpha_mult) {
#ifdef __ANDROID__
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.0001f) return;

    float nx = -dy / len * (thickness * 0.5f);
    float ny =  dx / len * (thickness * 0.5f);

    float ex = dx / len * (thickness * 0.25f);
    float ey = dy / len * (thickness * 0.25f);

    float sx = x1 - ex;
    float sy = y1 - ey;
    float ex_pt = x2 + ex;
    float ey_pt = y2 + ey;

    float vertices[8] = {
        sx - nx, sy - ny,
        sx + nx, sy + ny,
        ex_pt - nx, ey_pt - ny,
        ex_pt + nx, ey_pt + ny
    };

    float r = ((color_rgba >> 24) & 0xFF) / 255.0f;
    float g = ((color_rgba >> 16) & 0xFF) / 255.0f;
    float b = ((color_rgba >> 8) & 0xFF) / 255.0f;
    float a = ((color_rgba) & 0xFF) / 255.0f * alpha_mult;

    glUniform4f(u_overlay_color_loc_, r, g, b, a);

    glBindBuffer(GL_ARRAY_BUFFER, overlay_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
#else
    (void)x1; (void)y1; (void)x2; (void)y2; (void)thickness; (void)color_rgba; (void)alpha_mult;
#endif
}

void Renderer::draw_triangle(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color_rgba, float alpha_mult) {
#ifdef __ANDROID__
    float vertices[6] = {
        x1, y1,
        x2, y2,
        x3, y3
    };

    float r = ((color_rgba >> 24) & 0xFF) / 255.0f;
    float g = ((color_rgba >> 16) & 0xFF) / 255.0f;
    float b = ((color_rgba >> 8) & 0xFF) / 255.0f;
    float a = ((color_rgba) & 0xFF) / 255.0f * alpha_mult;

    glUniform4f(u_overlay_color_loc_, r, g, b, a);

    glBindBuffer(GL_ARRAY_BUFFER, overlay_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 3);
#else
    (void)x1; (void)y1; (void)x2; (void)y2; (void)x3; (void)y3; (void)color_rgba; (void)alpha_mult;
#endif
}

void Renderer::draw_vector_char(char c, float cx, float cy, float w, float h, float thickness, uint32_t color_rgba, float alpha_mult) {
#ifdef __ANDROID__
    float x0 = cx - w * 0.5f;
    float y0 = cy - h * 0.5f;

    auto seg = [&](float u1, float v1, float u2, float v2) {
        draw_line(x0 + u1 * w, y0 + v1 * h, x0 + u2 * w, y0 + v2 * h, thickness, color_rgba, alpha_mult);
    };

    char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    switch (upper) {
        case 'A':
            seg(0.12f, 1.0f, 0.50f, 0.0f);
            seg(0.50f, 0.0f, 0.88f, 1.0f);
            seg(0.25f, 0.62f, 0.75f, 0.62f);
            break;
        case 'B':
            seg(0.18f, 0.0f, 0.18f, 1.0f);
            seg(0.18f, 0.0f, 0.65f, 0.0f);
            seg(0.65f, 0.0f, 0.82f, 0.25f);
            seg(0.82f, 0.25f, 0.65f, 0.50f);
            seg(0.18f, 0.50f, 0.65f, 0.50f);
            seg(0.65f, 0.50f, 0.85f, 0.75f);
            seg(0.85f, 0.75f, 0.65f, 1.0f);
            seg(0.18f, 1.0f, 0.65f, 1.0f);
            break;
        case 'C':
            seg(0.85f, 0.15f, 0.45f, 0.0f);
            seg(0.45f, 0.0f, 0.18f, 0.25f);
            seg(0.18f, 0.25f, 0.18f, 0.75f);
            seg(0.18f, 0.75f, 0.45f, 1.0f);
            seg(0.45f, 1.0f, 0.85f, 0.85f);
            break;
        case 'D':
            seg(0.18f, 0.0f, 0.18f, 1.0f);
            seg(0.18f, 0.0f, 0.55f, 0.0f);
            seg(0.55f, 0.0f, 0.82f, 0.28f);
            seg(0.82f, 0.28f, 0.82f, 0.72f);
            seg(0.82f, 0.72f, 0.55f, 1.0f);
            seg(0.18f, 1.0f, 0.55f, 1.0f);
            break;
        case 'E':
            seg(0.18f, 0.0f, 0.18f, 1.0f);
            seg(0.18f, 0.0f, 0.82f, 0.0f);
            seg(0.18f, 0.50f, 0.68f, 0.50f);
            seg(0.18f, 1.0f, 0.82f, 1.0f);
            break;
        case 'F':
            seg(0.18f, 0.0f, 0.18f, 1.0f);
            seg(0.18f, 0.0f, 0.82f, 0.0f);
            seg(0.18f, 0.50f, 0.65f, 0.50f);
            break;
        case 'G':
            seg(0.85f, 0.15f, 0.45f, 0.0f);
            seg(0.45f, 0.0f, 0.18f, 0.25f);
            seg(0.18f, 0.25f, 0.18f, 0.75f);
            seg(0.18f, 0.75f, 0.45f, 1.0f);
            seg(0.45f, 1.0f, 0.82f, 1.0f);
            seg(0.82f, 1.0f, 0.82f, 0.50f);
            seg(0.50f, 0.50f, 0.82f, 0.50f);
            break;
        case 'H':
            seg(0.18f, 0.0f, 0.18f, 1.0f);
            seg(0.82f, 0.0f, 0.82f, 1.0f);
            seg(0.18f, 0.50f, 0.82f, 0.50f);
            break;
        case 'I':
            seg(0.50f, 0.0f, 0.50f, 1.0f);
            seg(0.25f, 0.0f, 0.75f, 0.0f);
            seg(0.25f, 1.0f, 0.75f, 1.0f);
            break;
        case 'J':
            seg(0.30f, 0.0f, 0.80f, 0.0f);
            seg(0.68f, 0.0f, 0.68f, 0.75f);
            seg(0.68f, 0.75f, 0.45f, 1.0f);
            seg(0.45f, 1.0f, 0.20f, 0.80f);
            seg(0.20f, 0.80f, 0.18f, 0.60f);
            break;
        case 'K':
            seg(0.18f, 0.0f, 0.18f, 1.0f);
            seg(0.82f, 0.0f, 0.18f, 0.55f);
            seg(0.35f, 0.42f, 0.82f, 1.0f);
            break;
        case 'L':
            seg(0.18f, 0.0f, 0.18f, 1.0f);
            seg(0.18f, 1.0f, 0.82f, 1.0f);
            break;
        case 'M':
            seg(0.18f, 1.0f, 0.18f, 0.0f);
            seg(0.18f, 0.0f, 0.50f, 0.55f);
            seg(0.50f, 0.55f, 0.82f, 0.0f);
            seg(0.82f, 0.0f, 0.82f, 1.0f);
            break;
        case 'N':
            seg(0.18f, 1.0f, 0.18f, 0.0f);
            seg(0.18f, 0.0f, 0.82f, 1.0f);
            seg(0.82f, 1.0f, 0.82f, 0.0f);
            break;
        case 'O':
            seg(0.35f, 0.0f, 0.65f, 0.0f);
            seg(0.65f, 0.0f, 0.82f, 0.22f);
            seg(0.82f, 0.22f, 0.82f, 0.78f);
            seg(0.82f, 0.78f, 0.65f, 1.0f);
            seg(0.65f, 1.0f, 0.35f, 1.0f);
            seg(0.35f, 1.0f, 0.18f, 0.78f);
            seg(0.18f, 0.78f, 0.18f, 0.22f);
            seg(0.18f, 0.22f, 0.35f, 0.0f);
            break;
        case 'P':
            seg(0.18f, 0.0f, 0.18f, 1.0f);
            seg(0.18f, 0.0f, 0.65f, 0.0f);
            seg(0.65f, 0.0f, 0.82f, 0.25f);
            seg(0.82f, 0.25f, 0.65f, 0.50f);
            seg(0.18f, 0.50f, 0.65f, 0.50f);
            break;
        case 'R':
            seg(0.18f, 0.0f, 0.18f, 1.0f);
            seg(0.18f, 0.0f, 0.65f, 0.0f);
            seg(0.65f, 0.0f, 0.82f, 0.25f);
            seg(0.82f, 0.25f, 0.65f, 0.50f);
            seg(0.18f, 0.50f, 0.65f, 0.50f);
            seg(0.48f, 0.50f, 0.82f, 1.0f);
            break;
        case 'S':
            seg(0.82f, 0.15f, 0.60f, 0.0f);
            seg(0.60f, 0.0f, 0.35f, 0.0f);
            seg(0.35f, 0.0f, 0.18f, 0.20f);
            seg(0.18f, 0.20f, 0.30f, 0.48f);
            seg(0.30f, 0.48f, 0.70f, 0.52f);
            seg(0.70f, 0.52f, 0.82f, 0.78f);
            seg(0.82f, 0.78f, 0.65f, 1.0f);
            seg(0.65f, 1.0f, 0.35f, 1.0f);
            seg(0.35f, 1.0f, 0.18f, 0.85f);
            break;
        case 'T':
            seg(0.10f, 0.0f, 0.90f, 0.0f);
            seg(0.50f, 0.0f, 0.50f, 1.0f);
            break;
        case 'U':
            seg(0.18f, 0.0f, 0.18f, 0.75f);
            seg(0.18f, 0.75f, 0.40f, 1.0f);
            seg(0.40f, 1.0f, 0.60f, 1.0f);
            seg(0.60f, 1.0f, 0.82f, 0.75f);
            seg(0.82f, 0.75f, 0.82f, 0.0f);
            break;
        case 'V':
            seg(0.18f, 0.0f, 0.50f, 1.0f);
            seg(0.50f, 1.0f, 0.82f, 0.0f);
            break;
        case 'W':
            seg(0.15f, 0.0f, 0.25f, 1.0f);
            seg(0.25f, 1.0f, 0.50f, 0.35f);
            seg(0.50f, 0.35f, 0.75f, 1.0f);
            seg(0.75f, 1.0f, 0.85f, 0.0f);
            break;
        case 'X':
            seg(0.18f, 0.0f, 0.82f, 1.0f);
            seg(0.82f, 0.0f, 0.18f, 1.0f);
            break;
        case 'Y':
            seg(0.18f, 0.0f, 0.50f, 0.50f);
            seg(0.82f, 0.0f, 0.50f, 0.50f);
            seg(0.50f, 0.50f, 0.50f, 1.0f);
            break;
        case 'Z':
            seg(0.18f, 0.0f, 0.82f, 0.0f);
            seg(0.82f, 0.0f, 0.18f, 1.0f);
            seg(0.18f, 1.0f, 0.82f, 1.0f);
            break;
        case '1':
            seg(0.30f, 0.22f, 0.50f, 0.0f);
            seg(0.50f, 0.0f, 0.50f, 1.0f);
            seg(0.25f, 1.0f, 0.75f, 1.0f);
            break;
        case '2':
            seg(0.18f, 0.25f, 0.40f, 0.0f);
            seg(0.40f, 0.0f, 0.75f, 0.0f);
            seg(0.75f, 0.0f, 0.82f, 0.25f);
            seg(0.82f, 0.25f, 0.18f, 1.0f);
            seg(0.18f, 1.0f, 0.82f, 1.0f);
            break;
        case '3':
            seg(0.18f, 0.0f, 0.82f, 0.0f);
            seg(0.82f, 0.0f, 0.45f, 0.45f);
            seg(0.45f, 0.45f, 0.75f, 0.50f);
            seg(0.75f, 0.50f, 0.82f, 0.75f);
            seg(0.82f, 0.75f, 0.60f, 1.0f);
            seg(0.60f, 1.0f, 0.18f, 1.0f);
            break;
        case '4':
            seg(0.72f, 0.0f, 0.72f, 1.0f);
            seg(0.72f, 0.0f, 0.18f, 0.65f);
            seg(0.18f, 0.65f, 0.85f, 0.65f);
            break;
        case ' ':
            break;
        default:
            seg(0.45f, 0.45f, 0.55f, 0.55f);
            break;
    }
#else
    (void)c; (void)cx; (void)cy; (void)w; (void)h; (void)thickness; (void)color_rgba; (void)alpha_mult;
#endif
}

void Renderer::draw_vector_text(const std::string& text, float cx, float cy, float char_h, float thickness, uint32_t color_rgba, float alpha_mult) {
#ifdef __ANDROID__
    if (text.empty() || char_h <= 0.0f) return;

    float char_w = char_h * 0.62f;
    float spacing = char_h * 0.16f;
    float total_w = (float)text.length() * char_w + (float)(text.length() - 1) * spacing;
    float start_x = cx - total_w * 0.5f + char_w * 0.5f;

    for (size_t i = 0; i < text.length(); ++i) {
        float cur_x = start_x + (float)i * (char_w + spacing);
        draw_vector_char(text[i], cur_x, cy, char_w, char_h, thickness, color_rgba, alpha_mult);
    }
#else
    (void)text; (void)cx; (void)cy; (void)char_h; (void)thickness; (void)color_rgba; (void)alpha_mult;
#endif
}

void Renderer::render_game() {
#ifdef __ANDROID__
    glViewport(game_viewport_.x, game_viewport_.y, game_viewport_.width, game_viewport_.height);

    glUseProgram(game_program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, game_texture_);
    glUniform1i(u_game_texture_loc_, 0);
    glUniform1i(u_game_format_loc_, (int)pixel_format_);

    glBindVertexArray(game_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
#endif
}

void Renderer::render_touch_overlay(const TouchInput& touch_input) {
#ifdef __ANDROID__
    glViewport(0, 0, screen_w_, screen_h_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(overlay_program_);
    glBindVertexArray(overlay_vao_);

    // 2D Orthographic Projection matrix
    float left = 0.0f, right = (float)screen_w_;
    float bottom = (float)screen_h_, top = 0.0f;
    float near = -1.0f, far = 1.0f;

    float proj[16] = {
        2.0f / (right - left), 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / (top - bottom), 0.0f, 0.0f,
        0.0f, 0.0f, -2.0f / (far - near), 0.0f,
        -(right + left) / (right - left), -(top + bottom) / (top - bottom), -(far + near) / (far - near), 1.0f
    };
    glUniformMatrix4fv(u_overlay_proj_loc_, 1, GL_FALSE, proj);

    // 1. Render Virtual D-Pad
    RenderableDpad dpad = touch_input.get_renderable_dpad();

    // Outer neon rim (alpha ~0.35f - 0.40f)
    draw_circle_ring(dpad.center_x, dpad.center_y, dpad.base_radius * 0.94f, dpad.base_radius, 0x00E5FFFF, 0.38f);

    // Outer base subtle fill (alpha ~0.20f - 0.22f)
    draw_circle(dpad.center_x, dpad.center_y, dpad.base_radius * 0.94f, 0x1E2232FF, 0.21f);

    // Inner base fill (alpha ~0.15f - 0.20f)
    draw_circle(dpad.center_x, dpad.center_y, dpad.base_radius * 0.72f, 0x121420FF, 0.17f);
    draw_circle_ring(dpad.center_x, dpad.center_y, dpad.base_radius * 0.70f, dpad.base_radius * 0.72f, 0x00E5FFFF, 0.25f);

    // Cardinal directional arrows (UP, DOWN, LEFT, RIGHT)
    float arrow_dist = dpad.base_radius * 0.52f;
    float arrow_size = dpad.base_radius * 0.12f;

    // UP
    bool up_active = (dpad.active_directions & (1 << RETRO_DEVICE_ID_JOYPAD_UP)) != 0;
    draw_triangle(
        dpad.center_x, dpad.center_y - arrow_dist - arrow_size * 0.6f,
        dpad.center_x - arrow_size * 0.7f, dpad.center_y - arrow_dist + arrow_size * 0.6f,
        dpad.center_x + arrow_size * 0.7f, dpad.center_y - arrow_dist + arrow_size * 0.6f,
        up_active ? 0x00FFFFFF : 0x88A0B8FF, up_active ? 0.85f : 0.25f
    );

    // DOWN
    bool down_active = (dpad.active_directions & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)) != 0;
    draw_triangle(
        dpad.center_x, dpad.center_y + arrow_dist + arrow_size * 0.6f,
        dpad.center_x - arrow_size * 0.7f, dpad.center_y + arrow_dist - arrow_size * 0.6f,
        dpad.center_x + arrow_size * 0.7f, dpad.center_y + arrow_dist - arrow_size * 0.6f,
        down_active ? 0x00FFFFFF : 0x88A0B8FF, down_active ? 0.85f : 0.25f
    );

    // LEFT
    bool left_active = (dpad.active_directions & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT)) != 0;
    draw_triangle(
        dpad.center_x - arrow_dist - arrow_size * 0.6f, dpad.center_y,
        dpad.center_x - arrow_dist + arrow_size * 0.6f, dpad.center_y - arrow_size * 0.7f,
        dpad.center_x - arrow_dist + arrow_size * 0.6f, dpad.center_y + arrow_size * 0.7f,
        left_active ? 0x00FFFFFF : 0x88A0B8FF, left_active ? 0.85f : 0.25f
    );

    // RIGHT
    bool right_active = (dpad.active_directions & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)) != 0;
    draw_triangle(
        dpad.center_x + arrow_dist + arrow_size * 0.6f, dpad.center_y,
        dpad.center_x + arrow_dist - arrow_size * 0.6f, dpad.center_y - arrow_size * 0.7f,
        dpad.center_x + arrow_dist - arrow_size * 0.6f, dpad.center_y + arrow_size * 0.7f,
        right_active ? 0x00FFFFFF : 0x88A0B8FF, right_active ? 0.85f : 0.25f
    );

    // D-Pad knob: sleek translucent disc (alpha ~0.30f when idle, ~0.60f when active)
    float knob_alpha = dpad.active ? 0.60f : 0.30f;
    uint32_t knob_color = dpad.active ? 0x00C8FFFF : 0xCBD5E1FF;

    // Knob neon boundary ring
    draw_circle_ring(dpad.knob_x, dpad.knob_y, dpad.knob_radius * 0.88f, dpad.knob_radius, 0x00E5FFFF, dpad.active ? 0.85f : 0.45f);
    // Knob body
    draw_circle(dpad.knob_x, dpad.knob_y, dpad.knob_radius * 0.88f, knob_color, knob_alpha);
    // Knob center accent
    draw_circle(dpad.knob_x, dpad.knob_y, dpad.knob_radius * 0.38f, 0xFFFFFFFF, dpad.active ? 0.50f : 0.25f);

    // 2. Render Action Buttons
    const auto& buttons = touch_input.get_renderable_buttons();
    for (const auto& btn : buttons) {
        float body_alpha = btn.pressed ? 0.55f : 0.25f;
        float ring_alpha = btn.pressed ? 0.85f : 0.45f;
        float highlight_alpha = btn.pressed ? 0.60f : 0.25f;
        float text_alpha = btn.pressed ? 0.98f : 0.85f;

        float radius = btn.pressed ? (btn.radius * 0.94f) : btn.radius;

        // Outer neon boundary ring: alpha = btn.pressed ? 0.85f : 0.45f
        draw_circle_ring(btn.x, btn.y, radius * 0.94f, radius * 1.05f, btn.color_rgba, ring_alpha);

        // Translucent glassmorphic body: alpha = btn.pressed ? 0.55f : 0.25f
        draw_circle(btn.x, btn.y, radius * 0.94f, btn.color_rgba, body_alpha);

        // Inner highlight: subtle alpha btn.pressed ? 0.60f : 0.25f
        draw_circle(btn.x, btn.y - radius * 0.25f, radius * 0.35f, 0xFFFFFFFF, highlight_alpha);

        // Vector labels / glyphs
        if (!btn.sublabel.empty() && (btn.sublabel == "A" || btn.sublabel == "B" || btn.sublabel == "C" || btn.sublabel == "D")) {
            // Metal Slug style: sublabel is "A", "B", "C", label is "SHOOT", "JUMP", "BOMB"
            draw_vector_text(btn.sublabel, btn.x, btn.y - radius * 0.18f, radius * 0.58f, std::max(2.5f, radius * 0.08f), 0xFFFFFFFF, text_alpha);
            draw_vector_text(btn.label, btn.x, btn.y + radius * 0.42f, radius * 0.26f, std::max(2.0f, radius * 0.045f), 0xFFFFFFFF, text_alpha * 0.90f);
        } else if (!btn.sublabel.empty() && (btn.label == "A" || btn.label == "B" || btn.label == "C" || btn.label == "D")) {
            // Neo Geo 4-button fighter style: label is "A", "B", "C", "D", sublabel is "LP", "LK", etc.
            draw_vector_text(btn.label, btn.x, btn.y - radius * 0.15f, radius * 0.65f, std::max(2.5f, radius * 0.09f), 0xFFFFFFFF, text_alpha);
            draw_vector_text(btn.sublabel, btn.x, btn.y + radius * 0.44f, radius * 0.26f, std::max(2.0f, radius * 0.045f), 0xFFFFFFFF, text_alpha * 0.90f);
        } else if (btn.label.length() <= 2) {
            // Short label like "LP", "MP", "HP", "LK", "A", etc.
            if (!btn.sublabel.empty()) {
                draw_vector_text(btn.label, btn.x, btn.y - radius * 0.14f, radius * 0.55f, std::max(2.5f, radius * 0.08f), 0xFFFFFFFF, text_alpha);
                draw_vector_text(btn.sublabel, btn.x, btn.y + radius * 0.42f, radius * 0.26f, std::max(2.0f, radius * 0.045f), 0xFFFFFFFF, text_alpha * 0.85f);
            } else {
                draw_vector_text(btn.label, btn.x, btn.y, radius * 0.65f, std::max(2.5f, radius * 0.09f), 0xFFFFFFFF, text_alpha);
            }
        } else {
            // Longer label like "ATTACK", "JUMP"
            draw_vector_text(btn.label, btn.x, btn.y, radius * 0.32f, std::max(2.0f, radius * 0.05f), 0xFFFFFFFF, text_alpha);
        }
    }

    // 3. Render Top Bar Buttons (Coin, Start, Menu)
    const auto& topbar = touch_input.get_renderable_topbar();
    for (const auto& tb : topbar) {
        float alpha = tb.pressed ? 0.65f : 0.30f;
        float border_alpha = tb.pressed ? 0.70f : 0.45f;
        uint32_t bg_color = tb.is_menu_trigger ? 0xE65100FF : 0x1E88E5FF;

        float pill_radius = tb.height * 0.5f;

        // Subtle outer border: alpha ~0.45f
        float b_thick = 1.8f;
        draw_circle(tb.x + pill_radius, tb.y + pill_radius, pill_radius + b_thick, 0xFFFFFFFF, border_alpha);
        draw_circle(tb.x + tb.width - pill_radius, tb.y + pill_radius, pill_radius + b_thick, 0xFFFFFFFF, border_alpha);
        draw_rect(tb.x + pill_radius, tb.y - b_thick, tb.width - 2.0f * pill_radius, tb.height + 2.0f * b_thick, 0xFFFFFFFF, border_alpha);

        // Translucent pill background: alpha = tb.pressed ? 0.65f : 0.30f
        draw_circle(tb.x + pill_radius, tb.y + pill_radius, pill_radius, bg_color, alpha);
        draw_circle(tb.x + tb.width - pill_radius, tb.y + pill_radius, pill_radius, bg_color, alpha);
        draw_rect(tb.x + pill_radius, tb.y, tb.width - 2.0f * pill_radius, tb.height, bg_color, alpha);

        // Top edge glass highlight
        draw_rect(tb.x + pill_radius, tb.y + 2.0f, tb.width - 2.0f * pill_radius, tb.height * 0.22f, 0xFFFFFFFF, alpha * 0.35f);

        float cy = tb.y + tb.height * 0.5f;
        float text_alpha = tb.pressed ? 0.98f : 0.85f;

        if (tb.label == "COIN") {
            // Coin Button: icon + label
            float icon_cx = tb.x + pill_radius * 0.95f;
            float coin_r = pill_radius * 0.52f;
            draw_circle_ring(icon_cx, cy, coin_r * 0.70f, coin_r, 0xFFD700FF, text_alpha);
            draw_vector_char('C', icon_cx, cy, coin_r * 0.95f, coin_r * 0.95f, 2.0f, 0xFFFFFFFF, text_alpha);

            // Label "COIN"
            float label_x = tb.x + pill_radius * 1.55f + (tb.width - pill_radius * 2.0f) * 0.40f;
            draw_vector_text("COIN", label_x, cy, tb.height * 0.38f, std::max(2.0f, tb.height * 0.055f), 0xFFFFFFFF, text_alpha);
        } else if (tb.label == "START") {
            // Start Button: play triangle + label
            float icon_cx = tb.x + pill_radius * 0.95f;
            float tri_h = pill_radius * 0.50f;
            draw_triangle(
                icon_cx - tri_h * 0.45f, cy - tri_h,
                icon_cx - tri_h * 0.45f, cy + tri_h,
                icon_cx + tri_h * 0.75f, cy,
                0x00FF88FF, text_alpha
            );

            // Label "START"
            float label_x = tb.x + pill_radius * 1.55f + (tb.width - pill_radius * 2.0f) * 0.42f;
            draw_vector_text("START", label_x, cy, tb.height * 0.38f, std::max(2.0f, tb.height * 0.055f), 0xFFFFFFFF, text_alpha);
        } else if (tb.label == "MENU") {
            // Menu Button: 3 horizontal bars + label
            float icon_cx = tb.x + pill_radius * 0.90f;
            float bar_w = pill_radius * 0.65f;
            float bar_th = std::max(2.0f, tb.height * 0.055f);
            float bar_gap = tb.height * 0.16f;

            draw_line(icon_cx - bar_w * 0.5f, cy - bar_gap, icon_cx + bar_w * 0.5f, cy - bar_gap, bar_th, 0xFFFFFFFF, text_alpha);
            draw_line(icon_cx - bar_w * 0.5f, cy,           icon_cx + bar_w * 0.5f, cy,           bar_th, 0xFFFFFFFF, text_alpha);
            draw_line(icon_cx - bar_w * 0.5f, cy + bar_gap, icon_cx + bar_w * 0.5f, cy + bar_gap, bar_th, 0xFFFFFFFF, text_alpha);

            // Label "MENU"
            float label_x = tb.x + pill_radius * 1.50f + (tb.width - pill_radius * 2.0f) * 0.40f;
            draw_vector_text("MENU", label_x, cy, tb.height * 0.38f, std::max(2.0f, tb.height * 0.055f), 0xFFFFFFFF, text_alpha);
        } else {
            draw_vector_text(tb.label, tb.x + tb.width * 0.5f, cy, tb.height * 0.40f, std::max(2.0f, tb.height * 0.06f), 0xFFFFFFFF, text_alpha);
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
#else
    (void)touch_input;
#endif
}


void Renderer::render_splash_screen() {
#ifdef __ANDROID__
    glViewport(0, 0, screen_w_, screen_h_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(overlay_program_);
    glBindVertexArray(overlay_vao_);

    float left = 0.0f, right = (float)screen_w_;
    float bottom = (float)screen_h_, top = 0.0f;
    float near = -1.0f, far = 1.0f;

    float proj[16] = {
        2.0f / (right - left), 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / (top - bottom), 0.0f, 0.0f,
        0.0f, 0.0f, -2.0f / (far - near), 0.0f,
        -(right + left) / (right - left), -(top + bottom) / (top - bottom), -(far + near) / (far - near), 1.0f
    };
    glUniformMatrix4fv(u_overlay_proj_loc_, 1, GL_FALSE, proj);

    // Center splash / marquee card
    float card_w = screen_w_ * 0.44f;
    float card_h = screen_h_ * 0.36f;
    float card_x = (screen_w_ - card_w) * 0.5f;
    float card_y = (screen_h_ - card_h) * 0.5f;

    // Outer neon border
    draw_rect(card_x - 3.0f, card_y - 3.0f, card_w + 6.0f, card_h + 6.0f, 0x00E5FFCC, 0.75f);
    // Dark card background
    draw_rect(card_x, card_y, card_w, card_h, 0x14141EEE, 0.95f);
    // Top banner strip (Cyan accent)
    draw_rect(card_x, card_y, card_w, card_h * 0.16f, 0x00E5FF88, 0.90f);
    // Center arcade CRT panel
    draw_rect(card_x + 18.0f, card_y + card_h * 0.25f, card_w - 36.0f, card_h * 0.45f, 0x222233EE, 0.85f);
    // Center glowing power bar
    draw_rect(card_x + 36.0f, card_y + card_h * 0.45f, card_w - 72.0f, card_h * 0.06f, 0x00E5FFFF, 0.95f);
    // Bottom MENU instruction bar
    draw_rect(card_x + 18.0f, card_y + card_h * 0.76f, card_w - 36.0f, card_h * 0.15f, 0xE65100DD, 0.90f);

    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
#endif
}

void Renderer::render(const TouchInput& touch_input) {
#ifdef __ANDROID__
    // Clear letterbox / pillarbox background to deep arcade black
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 1. Render core game screen inside 4:3 letterbox/pillarbox viewport
    if (has_new_frame_) {
        render_game();
    } else {
        render_splash_screen();
    }

    // 2. Render virtual arcade touch overlay
    render_touch_overlay(touch_input);
#else
    (void)touch_input;
#endif
}

} // namespace arcade

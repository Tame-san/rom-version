#include "renderer.h"
#include <cmath>
#include <cstring>
#include <algorithm>

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
    glBufferData(GL_ARRAY_BUFFER, 128 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

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
    // Outer base circle
    draw_circle(dpad.center_x, dpad.center_y, dpad.base_radius, 0x2A2A3ACC, 0.75f);
    // Outer neon border
    draw_circle(dpad.center_x, dpad.center_y, dpad.base_radius * 0.97f, 0x00E5FFCC, 0.85f);
    // Inner base fill
    draw_circle(dpad.center_x, dpad.center_y, dpad.base_radius * 0.92f, 0x1A1A24CC, 0.80f);
    // Inner thumb knob (bright neon cyan when active, crisp silver when idle)
    uint32_t knob_color = dpad.active ? 0x00FFFFFF : 0xDDDDDDFF;
    draw_circle(dpad.knob_x, dpad.knob_y, dpad.knob_radius, knob_color, dpad.active ? 1.0f : 0.85f);
    draw_circle(dpad.knob_x, dpad.knob_y, dpad.knob_radius * 0.4f, 0x00E5FFFF, 0.95f);

    // 2. Render Action Buttons
    const auto& buttons = touch_input.get_renderable_buttons();
    for (const auto& btn : buttons) {
        float alpha = btn.pressed ? 1.0f : 0.85f;
        float radius = btn.pressed ? (btn.radius * 0.93f) : btn.radius;
        // Outer glow/ring
        draw_circle(btn.x, btn.y, radius * 1.08f, 0xFFFFFF66, 0.6f);
        // Button body
        draw_circle(btn.x, btn.y, radius, btn.color_rgba, alpha);
        // Inner core highlight
        draw_circle(btn.x, btn.y, radius * 0.38f, 0xFFFFFFFF, alpha * 0.9f);
    }

    // 3. Render Top Bar Buttons (Coin, Start, Menu)
    const auto& topbar = touch_input.get_renderable_topbar();
    for (const auto& tb : topbar) {
        float alpha = tb.pressed ? 1.0f : 0.85f;
        uint32_t bg_color = tb.is_menu_trigger ? 0xE65100EE : 0x1E88E5EE;
        draw_rect(tb.x - 2, tb.y - 2, tb.width + 4, tb.height + 4, 0xFFFFFF88, 0.8f);
        draw_rect(tb.x, tb.y, tb.width, tb.height, bg_color, alpha);
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

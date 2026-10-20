#ifndef RENDERER_H__
#define RENDERER_H__

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include "libretro.h"
#include "touch_input.h"

#ifdef __ANDROID__
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#else
// Mock / Fallback definitions for host unit testing
typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned int GLenum;
typedef float GLfloat;
#define GL_NO_ERROR 0
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#define GL_UNSIGNED_BYTE 0x1401
#endif

namespace arcade {

struct ViewportRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Calculates optimal 4:3 (or custom) pillarbox/letterbox viewport for modern ultrawide screens
ViewportRect calculate_arcade_viewport(int screen_w, int screen_h, float aspect_ratio = 4.0f / 3.0f);

class Renderer {
public:
    Renderer();
    ~Renderer();

    // Initializes GLES 3.0 shaders, textures, and vertex buffers
    bool init(int screen_w, int screen_h);
    void shutdown();

    // Updates screen dimensions
    void on_surface_changed(int screen_w, int screen_h);

    // Configures Libretro pixel format (RGB565, XRGB8888, 0RGB1555)
    void set_pixel_format(enum retro_pixel_format format);

    // Updates game core texture with new frame from retro_video_refresh callback
    void update_frame(const void* data, unsigned width, unsigned height, size_t pitch);

    // Renders the complete frame: game screen + semi-transparent touch overlay
    void render(const TouchInput& touch_input);

    const ViewportRect& get_game_viewport() const { return game_viewport_; }
    int get_screen_width() const { return screen_w_; }
    int get_screen_height() const { return screen_h_; }

private:
    void init_game_pipeline();
    void init_overlay_pipeline();
    void render_game();
    void render_touch_overlay(const TouchInput& touch_input);
    void draw_circle(float cx, float cy, float radius, uint32_t color_rgba, float alpha_mult = 1.0f);
    void draw_rect(float x, float y, float w, float h, uint32_t color_rgba, float alpha_mult = 1.0f);

    int screen_w_ = 1920;
    int screen_h_ = 1080;
    ViewportRect game_viewport_;
    float game_aspect_ratio_ = 4.0f / 3.0f;

    enum retro_pixel_format pixel_format_ = RETRO_PIXEL_FORMAT_RGB565;
    unsigned frame_width_ = 0;
    unsigned frame_height_ = 0;
    bool has_new_frame_ = false;

    // Converted frame buffer for non-native alignment / swizzle
    std::vector<uint32_t> converted_pixels_;

    // OpenGL Objects
    GLuint game_program_ = 0;
    GLuint overlay_program_ = 0;
    GLuint game_texture_ = 0;
    GLuint game_vao_ = 0;
    GLuint game_vbo_ = 0;
    GLuint overlay_vao_ = 0;
    GLuint overlay_vbo_ = 0;

    GLint u_game_texture_loc_ = -1;
    GLint u_game_format_loc_ = -1;
    GLint u_overlay_proj_loc_ = -1;
    GLint u_overlay_color_loc_ = -1;

    bool initialized_ = false;
};

} // namespace arcade

#endif // RENDERER_H__

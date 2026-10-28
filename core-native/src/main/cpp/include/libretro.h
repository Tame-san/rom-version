#ifndef LIBRETRO_H__
#define LIBRETRO_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RETRO_API_VERSION 1

#define RETRO_DEVICE_TYPE_SHIFT         8
#define RETRO_DEVICE_MASK               ((1 << RETRO_DEVICE_TYPE_SHIFT) - 1)
#define RETRO_DEVICE_SUBCLASS(base, id) (((id + 1) << RETRO_DEVICE_TYPE_SHIFT) | base)

#define RETRO_DEVICE_NONE         0
#define RETRO_DEVICE_JOYPAD       1
#define RETRO_DEVICE_MOUSE        2
#define RETRO_DEVICE_KEYBOARD     3
#define RETRO_DEVICE_LIGHTGUN     4
#define RETRO_DEVICE_ANALOG       5
#define RETRO_DEVICE_POINTER      6

#define RETRO_DEVICE_ID_JOYPAD_B        0
#define RETRO_DEVICE_ID_JOYPAD_Y        1
#define RETRO_DEVICE_ID_JOYPAD_SELECT   2
#define RETRO_DEVICE_ID_JOYPAD_START    3
#define RETRO_DEVICE_ID_JOYPAD_UP       4
#define RETRO_DEVICE_ID_JOYPAD_DOWN     5
#define RETRO_DEVICE_ID_JOYPAD_LEFT     6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT    7
#define RETRO_DEVICE_ID_JOYPAD_A        8
#define RETRO_DEVICE_ID_JOYPAD_X        9
#define RETRO_DEVICE_ID_JOYPAD_L        10
#define RETRO_DEVICE_ID_JOYPAD_R        11
#define RETRO_DEVICE_ID_JOYPAD_L2       12
#define RETRO_DEVICE_ID_JOYPAD_R2       13
#define RETRO_DEVICE_ID_JOYPAD_L3       14
#define RETRO_DEVICE_ID_JOYPAD_R3       15

#define RETRO_DEVICE_INDEX_ANALOG_LEFT   0
#define RETRO_DEVICE_INDEX_ANALOG_RIGHT  1
#define RETRO_DEVICE_INDEX_ANALOG_BUTTON 2

#define RETRO_DEVICE_ID_ANALOG_X         0
#define RETRO_DEVICE_ID_ANALOG_Y         1

#define RETRO_DEVICE_ID_JOYPAD_MASK      256

#define RETRO_HW_FRAME_BUFFER_VALID      ((void*)(intptr_t)-1)

#define RETRO_REGION_NTSC  0
#define RETRO_REGION_PAL   1

#define RETRO_MEMORY_SAVE_RAM    0
#define RETRO_MEMORY_RTC         1
#define RETRO_MEMORY_SYSTEM_RAM  2
#define RETRO_MEMORY_VIDEO_RAM   3

enum retro_pixel_format
{
   RETRO_PIXEL_FORMAT_0RGB1555 = 0,
   RETRO_PIXEL_FORMAT_XRGB8888 = 1,
   RETRO_PIXEL_FORMAT_RGB565   = 2,
   RETRO_PIXEL_FORMAT_UNKNOWN  = INT32_MAX
};

enum retro_log_level
{
   RETRO_LOG_DEBUG = 0,
   RETRO_LOG_INFO,
   RETRO_LOG_WARN,
   RETRO_LOG_ERROR,
   RETRO_LOG_DUMMY = INT32_MAX
};

/* Environment commands */
#define RETRO_ENVIRONMENT_SET_ROTATION                   1
#define RETRO_ENVIRONMENT_GET_OVERSCAN                   2
#define RETRO_ENVIRONMENT_GET_CAN_DUPE                   3
#define RETRO_ENVIRONMENT_SET_MESSAGE                    6
#define RETRO_ENVIRONMENT_SHUTDOWN                       7
#define RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL          8
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY           9
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT               10
#define RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS          11
#define RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK          12
#define RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE     13
#define RETRO_ENVIRONMENT_SET_HW_RENDER                  14
#define RETRO_ENVIRONMENT_GET_VARIABLE                   15
#define RETRO_ENVIRONMENT_SET_VARIABLES                  16
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE            17
#define RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME            18
#define RETRO_ENVIRONMENT_GET_LIBRETRO_PATH              19
#define RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK        21
#define RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK             22
#define RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE           23
#define RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES  24
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE              27
#define RETRO_ENVIRONMENT_GET_PERF_INTERFACE             28
#define RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY      30
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY             31
#define RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO             32
#define RETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK      33
#define RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO             34
#define RETRO_ENVIRONMENT_SET_CONTROLLER_INFO            35
#define RETRO_ENVIRONMENT_SET_MEMORY_MAPS                36
#define RETRO_ENVIRONMENT_SET_GEOMETRY                   37
#define RETRO_ENVIRONMENT_GET_USERNAME                   38
#define RETRO_ENVIRONMENT_GET_LANGUAGE                   39
#define RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER 40
#define RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE        41
#define RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS       42
#define RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE 43
#define RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS       44
#define RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT          45
#define RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE         47
#define RETRO_ENVIRONMENT_GET_FASTFORWARDING             49
#define RETRO_ENVIRONMENT_GET_INPUT_BITMASKS             51
#define RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION       52
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS               53
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL          54
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY       55
#define RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER        56
#define RETRO_ENVIRONMENT_GET_DISK_CONTROL_EXT_INTERFACE 57
#define RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION  60
#define RETRO_ENVIRONMENT_SET_MESSAGE_EXT                61
#define RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS            62

struct retro_message
{
   const char *msg;
   unsigned frames;
};

struct retro_system_info
{
   const char *library_name;
   const char *library_version;
   const char *valid_extensions;
   bool need_fullpath;
   bool block_extract;
};

struct retro_game_geometry
{
   unsigned base_width;
   unsigned base_height;
   unsigned max_width;
   unsigned max_height;
   float    aspect_ratio;
};

struct retro_system_timing
{
   double fps;
   double sample_rate;
};

struct retro_system_av_info
{
   struct retro_game_geometry geometry;
   struct retro_system_timing timing;
};

struct retro_variable
{
   const char *key;
   const char *value;
};

struct retro_game_info
{
   const char *path;
   const void *data;
   size_t      size;
   const char *meta;
};

typedef void (*retro_log_printf_t)(enum retro_log_level level, const char *fmt, ...);

struct retro_log_callback
{
   retro_log_printf_t log;
};

struct retro_input_descriptor
{
   unsigned port;
   unsigned device;
   unsigned index;
   unsigned id;
   const char *description;
};

struct retro_controller_description
{
   const char *desc;
   unsigned id;
};

struct retro_controller_info
{
   const struct retro_controller_description *types;
   unsigned num_types;
};

enum retro_hw_context_type
{
   RETRO_HW_CONTEXT_NONE             = 0,
   RETRO_HW_CONTEXT_OPENGL           = 1,
   RETRO_HW_CONTEXT_OPENGLES2        = 2,
   RETRO_HW_CONTEXT_OPENGL_CORE      = 3,
   RETRO_HW_CONTEXT_OPENGLES3        = 4,
   RETRO_HW_CONTEXT_OPENGLES_CORE    = 5,
   RETRO_HW_CONTEXT_VULKAN           = 6,
   RETRO_HW_CONTEXT_DUMMY            = INT32_MAX
};

typedef void (*retro_proc_address_t)(void);
typedef void (*retro_hw_context_reset_t)(void);
typedef uintptr_t (*retro_hw_get_current_framebuffer_t)(void);
typedef retro_proc_address_t (*retro_hw_get_proc_address_t)(const char *sym);

struct retro_hw_render_callback
{
   enum retro_hw_context_type context_type;
   retro_hw_context_reset_t context_reset;
   retro_hw_get_current_framebuffer_t get_current_framebuffer;
   retro_hw_get_proc_address_t get_proc_address;
   bool depth;
   bool stencil;
   bool bottom_left_origin;
   unsigned version_major;
   unsigned version_minor;
   bool cache_context;
   retro_hw_context_reset_t context_destroy;
   bool debug_context;
};

/* Libretro Callbacks */
typedef bool (*retro_environment_t)(unsigned cmd, void *data);
typedef void (*retro_video_refresh_t)(const void *data, unsigned width, unsigned height, size_t pitch);
typedef void (*retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t (*retro_audio_sample_batch_t)(const int16_t *data, size_t frames);
typedef void (*retro_input_poll_t)(void);
typedef int16_t (*retro_input_state_t)(unsigned port, unsigned device, unsigned index, unsigned id);

/* Core API exports */
typedef void (*retro_init_t)(void);
typedef void (*retro_deinit_t)(void);
typedef unsigned (*retro_api_version_t)(void);
typedef void (*retro_get_system_info_t)(struct retro_system_info *info);
typedef void (*retro_get_system_av_info_t)(struct retro_system_av_info *info);
typedef void (*retro_set_environment_t)(retro_environment_t);
typedef void (*retro_set_video_refresh_t)(retro_video_refresh_t);
typedef void (*retro_set_audio_sample_t)(retro_audio_sample_t);
typedef void (*retro_set_audio_sample_batch_t)(retro_audio_sample_batch_t);
typedef void (*retro_set_input_poll_t)(retro_input_poll_t);
typedef void (*retro_set_input_state_t)(retro_input_state_t);
typedef void (*retro_set_controller_port_device_t)(unsigned port, unsigned device);
typedef void (*retro_reset_t)(void);
typedef void (*retro_run_t)(void);
typedef size_t (*retro_serialize_size_t)(void);
typedef bool (*retro_serialize_t)(void *data, size_t size);
typedef bool (*retro_unserialize_t)(const void *data, size_t size);
typedef void (*retro_cheat_reset_t)(void);
typedef void (*retro_cheat_set_t)(unsigned index, bool enabled, const char *code);
typedef bool (*retro_load_game_t)(const struct retro_game_info *game);
typedef bool (*retro_load_game_special_t)(unsigned game_type, const struct retro_game_info *info, size_t num_info);
typedef void (*retro_unload_game_t)(void);
typedef unsigned (*retro_get_region_t)(void);
typedef void *(*retro_get_memory_data_t)(unsigned id);
typedef size_t (*retro_get_memory_size_t)(unsigned id);

#ifdef __cplusplus
}
#endif

#endif

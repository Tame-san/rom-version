#ifndef AUDIO_ENGINE_H__
#define AUDIO_ENGINE_H__

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <vector>

#ifdef __ANDROID__
#include <aaudio/AAudio.h>
#endif

namespace arcade {

// Stereo 16-bit PCM frame (left and right channels)
struct AudioFrame {
    int16_t left;
    int16_t right;
};

// High-Performance Lock-Free Single-Producer Single-Consumer (SPSC) Audio Ring Buffer
class AudioRingBuffer {
public:
    explicit AudioRingBuffer(size_t capacity_frames = 4096);
    ~AudioRingBuffer() = default;

    // Non-copyable, non-movable
    AudioRingBuffer(const AudioRingBuffer&) = delete;
    AudioRingBuffer& operator=(const AudioRingBuffer&) = delete;

    // Resets read and write pointers
    void clear();

    // Writes stereo frames into ring buffer from core thread (Producer)
    // Automatically caps latency if buffer grows beyond max_target_frames
    size_t write(const int16_t* interleaved_pcm, size_t frames, size_t max_target_frames = 768);

    // Reads stereo frames into destination buffer from audio callback thread (Consumer)
    // Fills underrun frames with silence
    size_t read(int16_t* out_interleaved_pcm, size_t frames);

    // Returns available frames to read
    size_t available_read() const;

    // Returns available space to write
    size_t available_write() const;

    size_t capacity() const { return capacity_; }

private:
    const size_t capacity_;
    const size_t mask_;
    std::vector<AudioFrame> buffer_;

    // Cache-line aligned atomic pointers to prevent false sharing
    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    // Initializes audio stream with requested sample rate (e.g. 44100 or 48000)
    bool start(int sample_rate = 48000, int channels = 2);

    // Pauses and shuts down audio stream
    void stop();

    // Core Libretro callback: pushes audio batch directly into low-latency ring buffer
    size_t push_samples(const int16_t* pcm_data, size_t frames);

    // Dynamic latency stats
    double get_latency_ms() const;
    int get_sample_rate() const { return sample_rate_; }
    bool is_running() const { return running_; }

    // Direct access to internal ring buffer for testing
    AudioRingBuffer& get_ring_buffer() { return ring_buffer_; }

private:
    AudioRingBuffer ring_buffer_;
    int sample_rate_ = 48000;
    int channels_ = 2;
    std::atomic<bool> running_{false};

#ifdef __ANDROID__
    AAudioStream* stream_ = nullptr;
    static aaudio_data_callback_result_t data_callback(
        AAudioStream* stream,
        void* user_data,
        void* audio_data,
        int32_t num_frames
    );
    static void error_callback(
        AAudioStream* stream,
        void* user_data,
        aaudio_result_t error
    );
#endif
};

} // namespace arcade

#endif // AUDIO_ENGINE_H__

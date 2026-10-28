#include "audio_engine.h"
#include <cstring>
#include <algorithm>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "ArcadeAudio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) do { printf("[ArcadeAudio INFO] " __VA_ARGS__); printf("\n"); } while(0)
#define LOGW(...) do { printf("[ArcadeAudio WARN] " __VA_ARGS__); printf("\n"); } while(0)
#define LOGE(...) do { fprintf(stderr, "[ArcadeAudio ERROR] " __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace arcade {

static size_t next_power_of_two(size_t val) {
    size_t p = 1;
    while (p < val) {
        p <<= 1;
    }
    return p;
}

AudioRingBuffer::AudioRingBuffer(size_t capacity_frames)
    : capacity_(next_power_of_two(capacity_frames)),
      mask_(capacity_ - 1),
      buffer_(capacity_) {
    clear();
}

void AudioRingBuffer::clear() {
    write_pos_.store(0, std::memory_order_relaxed);
    read_pos_.store(0, std::memory_order_relaxed);
}

size_t AudioRingBuffer::available_read() const {
    size_t w = write_pos_.load(std::memory_order_acquire);
    size_t r = read_pos_.load(std::memory_order_relaxed);
    return w - r;
}

size_t AudioRingBuffer::available_write() const {
    size_t r = read_pos_.load(std::memory_order_acquire);
    size_t w = write_pos_.load(std::memory_order_relaxed);
    size_t occupied = w - r;
    return (occupied < capacity_) ? (capacity_ - occupied) : 0;
}

size_t AudioRingBuffer::write(const int16_t* interleaved_pcm, size_t frames, size_t max_target_frames) {
    if (!interleaved_pcm || frames == 0) return 0;

    size_t current_read = read_pos_.load(std::memory_order_acquire);
    size_t current_write = write_pos_.load(std::memory_order_relaxed);
    size_t occupied = current_write - current_read;

    // SPSC Invariant (SEC-01): The producer thread MUST NEVER mutate read_pos_!
    // To clamp latency to max_target_frames, drop surplus incoming frames at ingress
    if (max_target_frames > 0 && occupied >= max_target_frames) {
        return 0;
    }

    size_t max_allowed = (max_target_frames > 0 && max_target_frames < capacity_)
                         ? max_target_frames
                         : capacity_;

    size_t free_space = (occupied < max_allowed) ? (max_allowed - occupied) : 0;
    size_t to_write = std::min(frames, free_space);
    if (to_write == 0) return 0;

    for (size_t i = 0; i < to_write; ++i) {
        size_t idx = (current_write + i) & mask_;
        buffer_[idx].left = interleaved_pcm[i * 2 + 0];
        buffer_[idx].right = interleaved_pcm[i * 2 + 1];
    }

    write_pos_.store(current_write + to_write, std::memory_order_release);
    return to_write;
}

size_t AudioRingBuffer::read(int16_t* out_interleaved_pcm, size_t frames) {
    if (!out_interleaved_pcm || frames == 0) return 0;

    size_t current_read = read_pos_.load(std::memory_order_relaxed);
    size_t current_write = write_pos_.load(std::memory_order_acquire);
    size_t available = current_write - current_read;

    size_t to_read = std::min(frames, available);

    for (size_t i = 0; i < to_read; ++i) {
        size_t idx = (current_read + i) & mask_;
        out_interleaved_pcm[i * 2 + 0] = buffer_[idx].left;
        out_interleaved_pcm[i * 2 + 1] = buffer_[idx].right;
    }

    // Zero-fill underrun frames to suppress audio crackle/pops
    for (size_t i = to_read; i < frames; ++i) {
        out_interleaved_pcm[i * 2 + 0] = 0;
        out_interleaved_pcm[i * 2 + 1] = 0;
    }

    read_pos_.store(current_read + to_read, std::memory_order_release);
    return to_read;
}

AudioEngine::AudioEngine()
    : ring_buffer_(4096) {
}

AudioEngine::~AudioEngine() {
    stop();
}

#ifdef __ANDROID__
aaudio_data_callback_result_t AudioEngine::data_callback(
    AAudioStream* stream,
    void* user_data,
    void* audio_data,
    int32_t num_frames
) {
    (void)stream;
    AudioEngine* engine = static_cast<AudioEngine*>(user_data);
    if (!engine || !engine->running_.load(std::memory_order_acquire)) {
        std::memset(audio_data, 0, num_frames * 2 * sizeof(int16_t));
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    engine->ring_buffer_.read(static_cast<int16_t*>(audio_data), static_cast<size_t>(num_frames));
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void AudioEngine::error_callback(
    AAudioStream* stream,
    void* user_data,
    aaudio_result_t error
) {
    (void)stream;
    LOGE("AAudio stream error: %s (%d)", AAudio_convertResultToText(error), error);
    AudioEngine* engine = static_cast<AudioEngine*>(user_data);
    if (engine) {
        engine->running_.store(false, std::memory_order_release);
    }
}
#endif

bool AudioEngine::start(int sample_rate, int channels) {
    if (running_.load(std::memory_order_relaxed)) {
        stop();
    }

    sample_rate_ = sample_rate;
    channels_ = channels;
    ring_buffer_.clear();

#ifdef __ANDROID__
    AAudioStreamBuilder* builder = nullptr;
    aaudio_result_t res = AAudio_createStreamBuilder(&builder);
    if (res != AAUDIO_OK) {
        LOGE("Failed to create AAudio stream builder: %d", res);
        return false;
    }

    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, channels_);
    AAudioStreamBuilder_setSampleRate(builder, sample_rate_);
    AAudioStreamBuilder_setDataCallback(builder, data_callback, this);
    AAudioStreamBuilder_setErrorCallback(builder, error_callback, this);

    res = AAudioStreamBuilder_openStream(builder, &stream_);
    bool is_shared = false;
    if (res != AAUDIO_OK) {
        LOGW("Exclusive AAudio mode failed (%d), falling back to SHARED mode", res);
        AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
        res = AAudioStreamBuilder_openStream(builder, &stream_);
        is_shared = true;
    }

    AAudioStreamBuilder_delete(builder);

    if (res != AAUDIO_OK) {
        LOGE("Failed to open AAudio stream: %s (%d)", AAudio_convertResultToText(res), res);
        stream_ = nullptr;
        return false;
    }

    // Set buffer capacity in frames: tight 2x burst for MMAP exclusive mode (<10ms),
    // and deeper 4x burst (or 384 frames) in SHARED mode to prevent audio crackling on older/budget SoCs
    int32_t burst = AAudioStream_getFramesPerBurst(stream_);
    int32_t buffer_frames = is_shared ? std::max(burst * 4, 384) : (burst * 2);
    AAudioStream_setBufferSizeInFrames(stream_, buffer_frames);

    res = AAudioStream_requestStart(stream_);
    if (res != AAUDIO_OK) {
        LOGE("Failed to start AAudio stream: %s (%d)", AAudio_convertResultToText(res), res);
        AAudioStream_close(stream_);
        stream_ = nullptr;
        return false;
    }

    LOGI("AAudio stream started successfully: sample_rate=%d, channels=%d, burst=%d, buffer_size=%d",
         sample_rate_, channels_, burst, AAudioStream_getBufferSizeInFrames(stream_));
#endif

    running_.store(true, std::memory_order_release);
    return true;
}

void AudioEngine::stop() {
    running_.store(false, std::memory_order_release);

#ifdef __ANDROID__
    if (stream_) {
        aaudio_stream_state_t input_state = AAUDIO_STREAM_STATE_STOPPING;
        aaudio_stream_state_t next_state = AAUDIO_STREAM_STATE_STOPPED;
        AAudioStream_requestStop(stream_);
        // Synchronize stop with AAudioStream_waitForStateChange before closing stream
        int64_t timeout_nanos = 100 * 1000 * 1000LL; // 100ms
        AAudioStream_waitForStateChange(stream_, input_state, &next_state, timeout_nanos);
        AAudioStream_close(stream_);
        stream_ = nullptr;
    }
#endif

    ring_buffer_.clear();
}

size_t AudioEngine::push_samples(const int16_t* pcm_data, size_t frames) {
    if (!running_.load(std::memory_order_acquire)) return 0;
    // Target latency threshold: 768 frames is 16ms @ 48kHz, keeping latency under 10-15ms
    return ring_buffer_.write(pcm_data, frames, 768);
}

double AudioEngine::get_latency_ms() const {
    size_t queued_frames = ring_buffer_.available_read();
    if (sample_rate_ <= 0) return 0.0;
    return (static_cast<double>(queued_frames) * 1000.0) / static_cast<double>(sample_rate_);
}

} // namespace arcade

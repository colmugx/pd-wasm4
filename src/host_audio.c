#include "host_audio.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

#include "apu.h"

#ifndef WAMR_PD_AUDIO_CHUNK_FRAMES
#define WAMR_PD_AUDIO_CHUNK_FRAMES 512
#endif

#if WAMR_PD_AUDIO_BACKEND != WAMR_PD_AUDIO_BACKEND_WASM4_COMPAT \
    && WAMR_PD_AUDIO_BACKEND != WAMR_PD_AUDIO_BACKEND_NATIVE
#error "WAMR_PD_AUDIO_BACKEND must be 0 (wasm4_compat) or 1 (native)"
#endif

#if WAMR_PD_AUDIO_BACKEND == WAMR_PD_AUDIO_BACKEND_WASM4_COMPAT

static bool g_paused;

void
host_audio_init(void)
{
    g_paused = false;
    w4_apuInit();
}

void
host_audio_reset(void)
{
    g_paused = false;
    w4_apuInit();
}

void
host_audio_shutdown(void)
{
    g_paused = false;
}

void
host_audio_set_paused(bool paused)
{
    g_paused = paused;
}

void
host_audio_tick(void)
{
    if (!g_paused) {
        w4_apuTick();
    }
}

void
host_audio_tone(int32_t frequency, int32_t duration, int32_t volume, int32_t flags)
{
    if (!g_paused) {
        w4_apuTone(frequency, duration, volume, flags);
    }
}

int
host_audio_render(int16_t *left, int16_t *right, int len, bool output_enabled)
{
    static int16_t interleaved[WAMR_PD_AUDIO_CHUNK_FRAMES * 2];
    int offset = 0;

    if (!left || !right || len <= 0) {
        return 0;
    }

    if (!output_enabled || g_paused) {
        memset(left, 0, sizeof(int16_t) * (size_t)len);
        memset(right, 0, sizeof(int16_t) * (size_t)len);
        return 1;
    }

    while (offset < len) {
        int i;
        int chunk = len - offset;

        if (chunk > WAMR_PD_AUDIO_CHUNK_FRAMES) {
            chunk = WAMR_PD_AUDIO_CHUNK_FRAMES;
        }

        w4_apuWriteSamples(interleaved, (unsigned long)chunk);
        for (i = 0; i < chunk; i++) {
            left[offset + i] = interleaved[i * 2];
            right[offset + i] = interleaved[i * 2 + 1];
        }

        offset += chunk;
    }

    return 1;
}

const char *
host_audio_backend_name(void)
{
    return "wasm4_compat";
}

#else

#define HOST_AUDIO_SAMPLE_RATE 44100
#define HOST_AUDIO_MAX_VOLUME 0x1333
#define HOST_AUDIO_MAX_VOLUME_TRIANGLE 0x2000
#define HOST_AUDIO_TRIANGLE_RELEASE_FRAMES (HOST_AUDIO_SAMPLE_RATE / 1000)
#define HOST_AUDIO_EVENT_QUEUE_CAPACITY 128

typedef struct HostAudioToneEvent {
    int32_t frequency;
    int32_t duration;
    int32_t volume;
    int32_t flags;
} HostAudioToneEvent;

typedef struct HostAudioChannel {
    float freq1;
    float freq2;
    uint32_t start_time;
    uint32_t attack_time;
    uint32_t decay_time;
    uint32_t sustain_time;
    uint32_t release_time;
    uint32_t end_tick;
    int16_t sustain_volume;
    int16_t peak_volume;
    float phase;
    uint8_t pan;
    union {
        struct {
            float duty_cycle;
        } pulse;
        struct {
            uint16_t seed;
            int16_t last_random;
        } noise;
    } tone;
} HostAudioChannel;

typedef struct HostAudioState {
    HostAudioChannel channels[4];
    uint32_t time;
    atomic_uint_fast32_t ticks;
    atomic_uint_fast32_t queue_read_idx;
    atomic_uint_fast32_t queue_write_idx;
    HostAudioToneEvent queue[HOST_AUDIO_EVENT_QUEUE_CAPACITY];
    bool paused;
} HostAudioState;

static HostAudioState g_audio;

static inline int
host_min(int a, int b)
{
    return (a < b) ? a : b;
}

static inline int16_t
clamp_i16(int32_t v)
{
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return (int16_t)v;
}

static float
midi_freq(uint8_t note, uint8_t bend)
{
    return powf(2.0f, ((float)note - 69.0f + (float)bend / 256.0f) / 12.0f)
        * 440.0f;
}

static float
lerp_f(float value1, float value2, float t)
{
    return value1 + t * (value2 - value1);
}

static int
lerp_i(int value1, int value2, float t)
{
    return value1 + (int)((float)(value2 - value1) * t);
}

static float
ramp_f(float value1, float value2, uint32_t time1, uint32_t time2, uint32_t now)
{
    float t;

    if (time2 <= time1 || now >= time2) {
        return value2;
    }

    t = (float)(now - time1) / (float)(time2 - time1);
    return lerp_f(value1, value2, t);
}

static int16_t
ramp_i16(int value1, int value2, uint32_t time1, uint32_t time2, uint32_t now)
{
    float t;

    if (time2 <= time1 || now >= time2) {
        return (int16_t)value2;
    }

    t = (float)(now - time1) / (float)(time2 - time1);
    return (int16_t)lerp_i(value1, value2, t);
}

static float
polyblep(float phase, float phase_inc)
{
    if (phase_inc <= 0.0f) {
        return 1.0f;
    }

    if (phase < phase_inc) {
        float t = phase / phase_inc;
        return t + t - t * t;
    }
    if (phase > 1.0f - phase_inc) {
        float t = (phase - (1.0f - phase_inc)) / phase_inc;
        return 1.0f - (t + t - t * t);
    }
    return 1.0f;
}

static float
channel_frequency(const HostAudioChannel *channel, uint32_t now)
{
    if (channel->freq2 > 0.0f) {
        return ramp_f(channel->freq1, channel->freq2, channel->start_time,
                      channel->release_time, now);
    }
    return channel->freq1;
}

static int16_t
channel_volume(const HostAudioChannel *channel, uint32_t now)
{
    if (now >= channel->sustain_time
        && (channel->release_time - channel->sustain_time)
               > HOST_AUDIO_TRIANGLE_RELEASE_FRAMES) {
        return ramp_i16(channel->sustain_volume, 0, channel->sustain_time,
                        channel->release_time, now);
    }
    if (now >= channel->decay_time) {
        return channel->sustain_volume;
    }
    if (now >= channel->attack_time) {
        return ramp_i16(channel->peak_volume, channel->sustain_volume,
                        channel->attack_time, channel->decay_time, now);
    }
    return ramp_i16(0, channel->peak_volume, channel->start_time,
                    channel->attack_time, now);
}

static void
host_audio_reset_locked(void)
{
    memset(&g_audio, 0, sizeof(g_audio));
    g_audio.channels[3].tone.noise.seed = 0x0001;
}

static void
apply_tone_event(const HostAudioToneEvent *event)
{
    int freq1 = event->frequency & 0xffff;
    int freq2 = (event->frequency >> 16) & 0xffff;

    int sustain = event->duration & 0xff;
    int release = (event->duration >> 8) & 0xff;
    int decay = (event->duration >> 16) & 0xff;
    int attack = (event->duration >> 24) & 0xff;

    int sustain_volume = host_min(event->volume & 0xff, 100);
    int peak_volume = host_min((event->volume >> 8) & 0xff, 100);

    int channel_idx = event->flags & 0x03;
    int mode = (event->flags >> 2) & 0x03;
    int pan = (event->flags >> 4) & 0x03;
    int note_mode = event->flags & 0x40;

    uint32_t ticks = atomic_load_explicit(&g_audio.ticks, memory_order_relaxed);
    HostAudioChannel *channel = &g_audio.channels[channel_idx];
    int16_t max_volume;

    if (g_audio.time > channel->release_time && ticks != channel->end_tick) {
        channel->phase = (channel_idx == 2) ? 0.25f : 0.0f;
    }

    if (note_mode) {
        channel->freq1 = midi_freq((uint8_t)(freq1 & 0xff), (uint8_t)(freq1 >> 8));
        channel->freq2 = (freq2 == 0)
            ? 0.0f
            : midi_freq((uint8_t)(freq2 & 0xff), (uint8_t)(freq2 >> 8));
    }
    else {
        channel->freq1 = (float)freq1;
        channel->freq2 = (float)freq2;
    }

    channel->start_time = g_audio.time;
    channel->attack_time = channel->start_time + (uint32_t)HOST_AUDIO_SAMPLE_RATE
        * (uint32_t)attack / 60u;
    channel->decay_time = channel->attack_time + (uint32_t)HOST_AUDIO_SAMPLE_RATE
        * (uint32_t)decay / 60u;
    channel->sustain_time = channel->decay_time + (uint32_t)HOST_AUDIO_SAMPLE_RATE
        * (uint32_t)sustain / 60u;
    channel->release_time = channel->sustain_time + (uint32_t)HOST_AUDIO_SAMPLE_RATE
        * (uint32_t)release / 60u;
    channel->end_tick = ticks + (uint32_t)attack + (uint32_t)decay
        + (uint32_t)sustain + (uint32_t)release;

    max_volume = (channel_idx == 2) ? HOST_AUDIO_MAX_VOLUME_TRIANGLE
                                    : HOST_AUDIO_MAX_VOLUME;
    channel->sustain_volume = (int16_t)(max_volume * sustain_volume / 100);
    channel->peak_volume = (peak_volume > 0)
        ? (int16_t)(max_volume * peak_volume / 100)
        : max_volume;
    channel->pan = (uint8_t)pan;

    if (channel_idx == 0 || channel_idx == 1) {
        switch (mode) {
            case 0:
                channel->tone.pulse.duty_cycle = 0.125f;
                break;
            case 2:
                channel->tone.pulse.duty_cycle = 0.5f;
                break;
            case 1:
            case 3:
            default:
                channel->tone.pulse.duty_cycle = 0.25f;
                break;
        }
    }
    else if (channel_idx == 2 && release == 0) {
        channel->release_time += HOST_AUDIO_TRIANGLE_RELEASE_FRAMES;
    }
}

static void
drain_tone_events(void)
{
    uint32_t read_idx;
    uint32_t write_idx;

    read_idx = atomic_load_explicit(&g_audio.queue_read_idx, memory_order_relaxed);
    write_idx =
        atomic_load_explicit(&g_audio.queue_write_idx, memory_order_acquire);

    while (read_idx != write_idx) {
        const HostAudioToneEvent *event = &g_audio.queue[read_idx];
        apply_tone_event(event);
        read_idx = (read_idx + 1u) % HOST_AUDIO_EVENT_QUEUE_CAPACITY;
    }

    atomic_store_explicit(&g_audio.queue_read_idx, read_idx, memory_order_release);
}

void
host_audio_init(void)
{
    host_audio_reset_locked();
}

void
host_audio_reset(void)
{
    host_audio_reset_locked();
}

void
host_audio_shutdown(void)
{
    host_audio_reset_locked();
}

void
host_audio_set_paused(bool paused)
{
    g_audio.paused = paused;
}

void
host_audio_tick(void)
{
    if (!g_audio.paused) {
        (void)atomic_fetch_add_explicit(&g_audio.ticks, 1u, memory_order_relaxed);
    }
}

void
host_audio_tone(int32_t frequency, int32_t duration, int32_t volume, int32_t flags)
{
    HostAudioToneEvent event;
    uint32_t write_idx;
    uint32_t read_idx;
    uint32_t next_idx;

    if (g_audio.paused) {
        return;
    }

    event.frequency = frequency;
    event.duration = duration;
    event.volume = volume;
    event.flags = flags;

    write_idx =
        atomic_load_explicit(&g_audio.queue_write_idx, memory_order_relaxed);
    read_idx = atomic_load_explicit(&g_audio.queue_read_idx, memory_order_acquire);
    next_idx = (write_idx + 1u) % HOST_AUDIO_EVENT_QUEUE_CAPACITY;

    if (next_idx == read_idx) {
        read_idx = (read_idx + 1u) % HOST_AUDIO_EVENT_QUEUE_CAPACITY;
        atomic_store_explicit(&g_audio.queue_read_idx, read_idx, memory_order_release);
    }

    g_audio.queue[write_idx] = event;
    atomic_store_explicit(&g_audio.queue_write_idx, next_idx, memory_order_release);
}

int
host_audio_render(int16_t *left, int16_t *right, int len, bool output_enabled)
{
    int i;
    uint32_t ticks;

    if (!left || !right || len <= 0) {
        return 0;
    }

    if (!output_enabled || g_audio.paused) {
        memset(left, 0, sizeof(int16_t) * (size_t)len);
        memset(right, 0, sizeof(int16_t) * (size_t)len);
        return 1;
    }

    drain_tone_events();
    ticks = atomic_load_explicit(&g_audio.ticks, memory_order_relaxed);

    for (i = 0; i < len; i++, g_audio.time++) {
        int32_t mix_left = 0;
        int32_t mix_right = 0;
        int channel_idx;

        for (channel_idx = 0; channel_idx < 4; channel_idx++) {
            HostAudioChannel *channel = &g_audio.channels[channel_idx];

            if (g_audio.time < channel->release_time || ticks == channel->end_tick) {
                float freq = channel_frequency(channel, g_audio.time);
                int16_t volume = channel_volume(channel, g_audio.time);
                int32_t sample = 0;

                if (channel_idx == 3) {
                    channel->phase += freq * freq
                        / (1000000.0f / 44100.0f * HOST_AUDIO_SAMPLE_RATE);
                    while (channel->phase > 0.0f) {
                        channel->phase -= 1.0f;
                        channel->tone.noise.seed ^= channel->tone.noise.seed >> 7;
                        channel->tone.noise.seed ^= channel->tone.noise.seed << 9;
                        channel->tone.noise.seed ^= channel->tone.noise.seed >> 13;
                        channel->tone.noise.last_random =
                            (int16_t)(2 * (channel->tone.noise.seed & 0x1) - 1);
                    }
                    sample = (int32_t)volume * channel->tone.noise.last_random;
                }
                else {
                    float phase_inc = freq / HOST_AUDIO_SAMPLE_RATE;
                    channel->phase += phase_inc;

                    if (channel->phase >= 1.0f) {
                        channel->phase -= 1.0f;
                    }

                    if (channel_idx == 2) {
                        sample = (int32_t)((float)volume
                                           * (2.0f * fabsf(2.0f * channel->phase - 1.0f)
                                              - 1.0f));
                    }
                    else {
                        float duty_phase;
                        float duty_phase_inc;
                        int16_t multiplier;

                        if (channel->phase < channel->tone.pulse.duty_cycle) {
                            duty_phase = channel->phase / channel->tone.pulse.duty_cycle;
                            duty_phase_inc =
                                phase_inc / channel->tone.pulse.duty_cycle;
                            multiplier = volume;
                        }
                        else {
                            duty_phase = (channel->phase - channel->tone.pulse.duty_cycle)
                                / (1.0f - channel->tone.pulse.duty_cycle);
                            duty_phase_inc = phase_inc
                                / (1.0f - channel->tone.pulse.duty_cycle);
                            multiplier = (int16_t)-volume;
                        }
                        sample = (int32_t)((float)multiplier
                                           * polyblep(duty_phase, duty_phase_inc));
                    }
                }

                if (channel->pan != 1) {
                    mix_right += sample;
                }
                if (channel->pan != 2) {
                    mix_left += sample;
                }
            }
        }

        left[i] = clamp_i16(mix_left);
        right[i] = clamp_i16(mix_right);
    }

    return 1;
}

const char *
host_audio_backend_name(void)
{
    return "native";
}

#endif

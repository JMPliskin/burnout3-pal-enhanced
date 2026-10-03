/*
 * The port's sound (--audio): the SPU2's mixed output (spu2.cpp) played through
 * the system's wave device. Windows' waveOut needs nothing beyond the system,
 * like the window; a lower-latency device can replace it behind the same two
 * calls.
 *
 * The SPU2 produces samples as the IOP runs, by the clock, so production
 * matches playback on average. Buffers are queued as they fill; when every
 * buffer is still queued the newest samples are dropped rather than the
 * emulation waiting, and an empty queue simply plays silence until the next.
 *
 * --audio-wav records the same output to a WAV file instead of (or as well as)
 * playing it: how sound is checked without anyone listening.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ps2host/run.h"

#pragma comment(lib, "winmm.lib")

namespace ps2host {

namespace {

constexpr int kBuffers = 8;
constexpr DWORD kFrames = 1024;                 // 21 ms at 48 kHz

struct Device {
    HWAVEOUT out = nullptr;
    WAVEHDR headers[kBuffers] = {};
    std::vector<short> data[kBuffers];
    int filling = 0;                            // the buffer being filled
    DWORD filled = 0;                           // frames in it
    unsigned long long dropped = 0;
    std::FILE* wav = nullptr;
    unsigned long long wav_frames = 0;

    // The WAV header's sizes are known only at the end.
    ~Device() {
        if (!wav) return;
        const unsigned data = unsigned(wav_frames * 4);
        WriteHeader(data);
        std::fclose(wav);
    }
    void WriteHeader(unsigned data) {
        unsigned char h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
                               16, 0, 0, 0, 1, 0, 2, 0, 0x80, 0xBB, 0, 0, 0, 0xEE, 2, 0,
                               4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
        const unsigned riff = 36 + data;
        std::memcpy(h + 4, &riff, 4);
        std::memcpy(h + 40, &data, 4);
        std::fseek(wav, 0, SEEK_SET);
        std::fwrite(h, 1, 44, wav);
        std::fseek(wav, 0, SEEK_END);
    }
};

Device& D() {
    static Device device;
    return device;
}

}  // namespace

bool OpenAudio() {
    Device& d = D();
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 48000 * 4;
    if (waveOutOpen(&d.out, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        d.out = nullptr;
        return false;
    }
    for (int i = 0; i < kBuffers; ++i) {
        d.data[i].assign(kFrames * 2, 0);
        d.headers[i].lpData = reinterpret_cast<LPSTR>(d.data[i].data());
        d.headers[i].dwBufferLength = kFrames * 4;
        d.headers[i].dwFlags = WHDR_DONE;           // free
    }
    return true;
}

bool OpenWavDump(const std::string& path) {
    Device& d = D();
    d.wav = std::fopen(path.c_str(), "wb");
    if (!d.wav) return false;
    d.WriteHeader(0);
    return true;
}

void AudioFrames(const short* frames, unsigned count) {
    Device& d = D();
    if (d.wav) {
        std::fwrite(frames, 4, count, d.wav);
        d.wav_frames += count;
    }
    if (!d.out) return;
    while (count) {
        WAVEHDR& h = d.headers[d.filling];
        if (!(h.dwFlags & WHDR_DONE)) {             // every buffer queued: drop
            d.dropped += count;
            return;
        }
        const DWORD take = std::min<DWORD>(count, kFrames - d.filled);
        std::memcpy(d.data[d.filling].data() + d.filled * 2, frames, take * 4);
        d.filled += take;
        frames += take * 2;
        count -= take;
        if (d.filled == kFrames) {
            if (h.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(d.out, &h, sizeof h);
            h.dwFlags = 0;
            waveOutPrepareHeader(d.out, &h, sizeof h);
            waveOutWrite(d.out, &h, sizeof h);
            d.filling = (d.filling + 1) % kBuffers;
            d.filled = 0;
        }
    }
}

}  // namespace ps2host

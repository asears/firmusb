// soundtracker.cpp - ScreamTracker-style sound generator CLI (demo scene, 90s style)
// Inputs: keyboard (note triggers), Outputs: waveOut audio (sine, saw, cos, tan)
// Usage: soundtracker.exe [--waveform sine|saw|cos|tan|mix] [--bpm N] [--help]
// Future Crew tribute - demo scene 90s dance

#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include <thread>
#include <atomic>
#include <map>
#include <string>

#pragma comment(lib, "winmm.lib")

constexpr int SAMPLE_RATE = 44100;
constexpr int BUFFER_SIZE = 2048;
constexpr int NUM_BUFFERS = 4;
constexpr int NUM_CHANNELS = 1;
constexpr int BITS_PER_SAMPLE = 16;
constexpr double PI = 3.14159265358979323846;

std::atomic<int> current_note(0);
std::atomic<bool> running(true);

// Map keyboard keys to MIDI note numbers (C major scale, QWERTY row)
std::map<int, int> key_to_note = {
    { 'A', 60 }, // C4
    { 'S', 62 }, // D4
    { 'D', 64 }, // E4
    { 'F', 65 }, // F4
    { 'G', 67 }, // G4
    { 'H', 69 }, // A4
    { 'J', 71 }, // B4
    { 'K', 72 }, // C5
};

// Waveform types
enum class Waveform { Sine, Saw, Cos, Tan, Mix };

// Generate sample for a given waveform
short generate_sample(Waveform wf, double phase, double amplitude) {
    double val = 0.0;
    switch (wf) {
        case Waveform::Sine:
            val = sin(phase);
            break;
        case Waveform::Saw:
            val = 2.0 * (phase / (2 * PI) - floor(phase / (2 * PI) + 0.5));
            break;
        case Waveform::Cos:
            val = cos(phase);
            break;
        case Waveform::Tan:
            val = tan(phase) * 0.2; // scale tan to avoid clipping
            break;
        case Waveform::Mix:
            val = 0.4 * sin(phase) + 0.3 * cos(phase) + 0.2 * tan(phase) + 0.1 * (2.0 * (phase / (2 * PI) - floor(phase / (2 * PI) + 0.5)));
            break;
    }
    // Clamp and scale
    if (val > 1.0) val = 1.0;
    if (val < -1.0) val = -1.0;
    return static_cast<short>(val * amplitude * 32767);
}

// MIDI note to frequency
inline double note_to_freq(int note) {
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

// Audio thread: fills waveOut buffers
void audio_thread(Waveform wf) {
    HWAVEOUT hWaveOut;
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = NUM_CHANNELS;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = BITS_PER_SAMPLE;
    wfx.nBlockAlign = NUM_CHANNELS * BITS_PER_SAMPLE / 8;
    wfx.nAvgBytesPerSec = SAMPLE_RATE * wfx.nBlockAlign;
    wfx.cbSize = 0;

    if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        printf("Failed to open waveOut device\n");
        return;
    }

    std::vector<std::vector<short>> buffers(NUM_BUFFERS, std::vector<short>(BUFFER_SIZE));
    std::vector<WAVEHDR> headers(NUM_BUFFERS);
    double amplitude = 0.5;
    double phase = 0.0;
    double phase_inc = 0.0;
    int last_note = 0;

    for (int i = 0; i < NUM_BUFFERS; ++i) {
        headers[i].lpData = (LPSTR)buffers[i].data();
        headers[i].dwBufferLength = BUFFER_SIZE * sizeof(short);
        headers[i].dwFlags = 0;
        headers[i].dwLoops = 0;
        waveOutPrepareHeader(hWaveOut, &headers[i], sizeof(WAVEHDR));
    }

    int buf_idx = 0;
    while (running) {
        int note = current_note.load();
        if (note != last_note) {
            phase = 0.0;
            last_note = note;
            phase_inc = (note > 0) ? (2.0 * PI * note_to_freq(note) / SAMPLE_RATE) : 0.0;
        }
        for (int j = 0; j < BUFFER_SIZE; ++j) {
            if (note > 0)
                buffers[buf_idx][j] = generate_sample(wf, phase, amplitude);
            else
                buffers[buf_idx][j] = 0;
            phase += phase_inc;
            if (phase > 2 * PI) phase -= 2 * PI;
        }
        waveOutWrite(hWaveOut, &headers[buf_idx], sizeof(WAVEHDR));
        buf_idx = (buf_idx + 1) % NUM_BUFFERS;
        Sleep(10);
    }
    waveOutReset(hWaveOut);
    for (int i = 0; i < NUM_BUFFERS; ++i)
        waveOutUnprepareHeader(hWaveOut, &headers[i], sizeof(WAVEHDR));
    waveOutClose(hWaveOut);
}

// Keyboard thread: maps keypresses to notes
void keyboard_thread() {
    printf("\nScreamTracker-style Sound Generator\n");
    printf("Keys: A S D F G H J K (C major scale)\n");
    printf("ESC to quit\n\n");
    while (running) {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            running = false;
            break;
        }
        for (const auto& kv : key_to_note) {
            if (GetAsyncKeyState(kv.first) & 0x8000) {
                current_note = kv.second;
                break;
            }
        }
        // If no key pressed, silence
        bool any_pressed = false;
        for (const auto& kv : key_to_note) {
            if (GetAsyncKeyState(kv.first) & 0x8000) {
                any_pressed = true;
                break;
            }
        }
        if (!any_pressed) current_note = 0;
        Sleep(10);
    }
}

Waveform parse_waveform(const std::string& arg) {
    if (arg == "sine") return Waveform::Sine;
    if (arg == "saw") return Waveform::Saw;
    if (arg == "cos") return Waveform::Cos;
    if (arg == "tan") return Waveform::Tan;
    if (arg == "mix") return Waveform::Mix;
    return Waveform::Sine;
}

int main(int argc, char* argv[]) {
    Waveform wf = Waveform::Mix;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            printf("Usage: soundtracker.exe [--waveform sine|saw|cos|tan|mix]\n");
            return 0;
        }
        if (arg == "--waveform" && i + 1 < argc) {
            wf = parse_waveform(argv[i + 1]);
            ++i;
        }
    }
    std::thread audio(audio_thread, wf);
    std::thread keys(keyboard_thread);
    audio.join();
    keys.join();
    printf("Goodbye!\n");
    return 0;
}

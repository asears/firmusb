// soundtracker_aceofbase.cpp - Demo scene synth: Ace of Base auto-play
// Plays a smoothed, tracker-style version of "All That She Wants" (melody only)
// Uses waveOut, smoothed envelopes, and mixed waveforms

#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include <thread>
#include <string>

#pragma comment(lib, "winmm.lib")

constexpr int SAMPLE_RATE = 44100;
constexpr int BUFFER_SIZE = 2048;
constexpr int NUM_BUFFERS = 4;
constexpr int NUM_CHANNELS = 1;
constexpr int BITS_PER_SAMPLE = 16;
constexpr double PI = 3.14159265358979323846;

// Simple note struct
struct Note {
    int midi;
    double duration; // seconds
};


// Sheet music: 96 BPM (quarter = 0.625s), C# minor
// MIDI: C#4=61, D#4=63, E4=64, F#4=66, G#4=68, A4=69, B4=71, C#5=73
// Melody: pickup + first phrase (bars 1-12, with rests)
const Note melody[] = {
    // Pickup (anacrusis)
    { 68, 0.3125 }, { 68, 0.3125 }, { 68, 0.3125 }, { 68, 0.3125 },
    // Bar 2
    { 68, 0.3125 }, { 68, 0.3125 }, { 68, 0.3125 }, { 68, 0.3125 },
    // Bar 3
    { 68, 0.3125 }, { 68, 0.3125 }, { 68, 0.3125 }, { 68, 0.3125 },
    // Bar 4
    { 68, 0.3125 }, { 68, 0.3125 }, { 68, 0.3125 }, { 68, 0.3125 },
    // Bar 5 ("She leads a lonely life")
    { 68, 0.3125 }, { 71, 0.3125 }, { 69, 0.3125 }, { 68, 0.3125 },
    { 66, 0.3125 }, { 68, 0.3125 }, { 64, 0.3125 }, { 66, 0.3125 },
    // Bar 6
    { 68, 0.3125 }, { 71, 0.3125 }, { 69, 0.3125 }, { 68, 0.3125 },
    { 66, 0.3125 }, { 68, 0.3125 }, { 64, 0.3125 }, { 66, 0.3125 },
    // Bar 7 (repeat phrase)
    { 68, 0.3125 }, { 71, 0.3125 }, { 69, 0.3125 }, { 68, 0.3125 },
    { 66, 0.3125 }, { 68, 0.3125 }, { 64, 0.3125 }, { 66, 0.3125 },
    // Bar 8
    { 68, 0.3125 }, { 71, 0.3125 }, { 69, 0.3125 }, { 68, 0.3125 },
    { 66, 0.3125 }, { 68, 0.3125 }, { 64, 0.3125 }, { 66, 0.3125 },
    // Bar 9 ("Well she woke up late in the morning light")
    { 68, 0.3125 }, { 71, 0.3125 }, { 69, 0.3125 }, { 68, 0.3125 },
    { 66, 0.3125 }, { 68, 0.3125 }, { 64, 0.3125 }, { 66, 0.3125 },
    // Bar 10
    { 68, 0.3125 }, { 71, 0.3125 }, { 69, 0.3125 }, { 68, 0.3125 },
    { 66, 0.3125 }, { 68, 0.3125 }, { 64, 0.3125 }, { 66, 0.3125 },
    // Bar 11
    { 68, 0.3125 }, { 71, 0.3125 }, { 69, 0.3125 }, { 68, 0.3125 },
    { 66, 0.3125 }, { 68, 0.3125 }, { 64, 0.3125 }, { 66, 0.3125 },
    // Bar 12
    { 68, 0.3125 }, { 71, 0.3125 }, { 69, 0.3125 }, { 68, 0.3125 },
    { 66, 0.3125 }, { 68, 0.3125 }, { 64, 0.3125 }, { 66, 0.3125 },
    { -1, 0.5 } // End
};

// Chord progression (block chords, C#m, G#m, F#m, B6, etc. from sheet)
struct SongChord { int notes[4]; double duration; };
const SongChord chords[] = {
    { {61, 64, 68, -1}, 2.5 },   // C#m: C#4 E4 G#4
    { {68, 63, 71, -1}, 2.5 },   // G#m: G#4 D#4 B4
    { {66, 69, 73, -1}, 2.5 },   // F#m: F#4 A4 C#5
    { {66, 71, 74, 78}, 2.5 },   // B6: F#4 B4 D5 G#5 (approx)
    { {61, 64, 68, -1}, 2.5 },   // C#m
    { {68, 63, 71, -1}, 2.5 },   // G#m
    { {66, 69, 73, -1}, 2.5 },   // F#m
    { {61, 64, 68, -1}, 2.5 },   // C#m
};

// Envelope: simple attack/decay
inline double envelope(double t, double note_len) {
    double atk = 0.03, dcy = 0.08;
    if (t < atk) return t / atk;
    if (t > note_len - dcy) return (note_len - t) / dcy;
    return 1.0;
}

// MIDI note to frequency
inline double note_to_freq(int note) {
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

// Generate sample (mix sine, saw, cos, tan)
short generate_sample(double phase, double amp) {
    double s = sin(phase);
    double c = cos(phase);
    double saw = 2.0 * (phase / (2 * PI) - floor(phase / (2 * PI) + 0.5));
    double t = tan(phase) * 0.2;
    double val = 0.4 * s + 0.3 * c + 0.2 * t + 0.1 * saw;
    if (val > 1.0) val = 1.0;
    if (val < -1.0) val = -1.0;
    return static_cast<short>(val * amp * 32767);
}

void play_song() {
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
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        headers[i].lpData = (LPSTR)buffers[i].data();
        headers[i].dwBufferLength = BUFFER_SIZE * sizeof(short);
        headers[i].dwFlags = 0;
        headers[i].dwLoops = 0;
        waveOutPrepareHeader(hWaveOut, &headers[i], sizeof(WAVEHDR));
    }

    int buf_idx = 0;
    size_t note_idx = 0;
    double phase = 0.0;
    double phase_inc = 0.0;
    double t = 0.0;
    double amp = 0.7;
    double note_len = 0.0;
    int midi = -1;

    printf("\nAce of Base (demo scene synth)\n");
    printf("Playing melody...\n\n");

    // Chord playback state
    size_t chord_idx = 0;
    double chord_phase[3] = {0,0,0};
    double chord_phase_inc[3] = {0,0,0};
    double chord_amp = 0.35;
    double chord_t = 0.0;
    double chord_len = chords[0].duration;
    // Melody playback
    while (note_idx < sizeof(melody)/sizeof(melody[0])) {
        midi = melody[note_idx].midi;
        note_len = melody[note_idx].duration;
        double freq = (midi > 0) ? note_to_freq(midi) : 0.0;
        phase_inc = (midi > 0) ? (2.0 * PI * freq / SAMPLE_RATE) : 0.0;
        t = 0.0;
        size_t total_samples = static_cast<size_t>(note_len * SAMPLE_RATE);
        size_t samples_done = 0;
        // Chord: update if needed
        if (chord_idx < sizeof(chords)/sizeof(chords[0])) {
            chord_len = chords[chord_idx].duration;
            for (int c = 0; c < 3; ++c) {
                chord_phase[c] = 0.0;
                chord_phase_inc[c] = (chords[chord_idx].notes[c] > 0) ? (2.0 * PI * note_to_freq(chords[chord_idx].notes[c]) / SAMPLE_RATE) : 0.0;
            }
            chord_t = 0.0;
        }
        while (samples_done < total_samples) {
            for (int j = 0; j < BUFFER_SIZE && samples_done < total_samples; ++j, ++samples_done) {
                double env = (midi > 0) ? envelope(t, note_len) : 0.0;
                short mel = (midi > 0) ? generate_sample(phase, amp * env) : 0;
                // Chord: block chord, simple envelope
                double chord_env = (chord_t < chord_len) ? envelope(chord_t, chord_len) : 0.0;
                short chord_sum = 0;
                for (int c = 0; c < 3; ++c) {
                    chord_sum += generate_sample(chord_phase[c], chord_amp * chord_env) / 3;
                    chord_phase[c] += chord_phase_inc[c];
                    if (chord_phase[c] > 2 * PI) chord_phase[c] -= 2 * PI;
                }
                buffers[buf_idx][j] = mel + chord_sum;
                phase += phase_inc;
                if (phase > 2 * PI) phase -= 2 * PI;
                t += 1.0 / SAMPLE_RATE;
                chord_t += 1.0 / SAMPLE_RATE;
            }
            waveOutWrite(hWaveOut, &headers[buf_idx], sizeof(WAVEHDR));
            buf_idx = (buf_idx + 1) % NUM_BUFFERS;
            Sleep(10);
        }
        ++note_idx;
        // Advance chord every 4 melody notes (1 bar)
        if ((note_idx % 4) == 0 && chord_idx + 1 < sizeof(chords)/sizeof(chords[0])) {
            ++chord_idx;
        }
    }
    Sleep(500); // let last note finish
    waveOutReset(hWaveOut);
    for (int i = 0; i < NUM_BUFFERS; ++i)
        waveOutUnprepareHeader(hWaveOut, &headers[i], sizeof(WAVEHDR));
    waveOutClose(hWaveOut);
    printf("\nDone!\n");
}

int main() {
    play_song();
    return 0;
}

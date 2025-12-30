// mid2csv.cpp - Minimal MIDI to CSV note event converter
#include <fstream>
#include <vector>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <string>

// Helper to read big-endian values
uint32_t read_be(std::ifstream& f, int bytes) {
    uint32_t v = 0;
    for (int i = 0; i < bytes; ++i) v = (v << 8) | f.get();
    return v;
}

// Read variable-length quantity
uint32_t read_vlq(std::ifstream& f) {
    uint32_t v = 0;
    uint8_t b;
    do {
        b = f.get();
        v = (v << 7) | (b & 0x7F);
    } while (b & 0x80);
    return v;
}

struct NoteEvent {
    uint32_t start_tick;
    uint32_t end_tick;
    uint8_t note;
    uint8_t velocity;
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: mid2csv <input.mid> <output.csv>\n";
        return 1;
    }
    std::ifstream f(argv[1], std::ios::binary);
    if (!f) { std::cerr << "Failed to open MIDI file\n"; return 1; }
    std::ofstream out(argv[2]);
    if (!out) { std::cerr << "Failed to open CSV file\n"; return 1; }

    // Header
    char hdr[4]; f.read(hdr, 4);
    if (std::string(hdr, 4) != "MThd") { std::cerr << "Not a MIDI file\n"; return 1; }
    uint32_t hdr_len = read_be(f, 4);
    uint16_t format = read_be(f, 2);
    uint16_t ntrks = read_be(f, 2);
    uint16_t division = read_be(f, 2);

    // Only parse first track for demo
    f.read(hdr, 4);
    if (std::string(hdr, 4) != "MTrk") { std::cerr << "No track found\n"; return 1; }
    uint32_t trk_len = read_be(f, 4);
    uint32_t trk_end = f.tellg();
    trk_end += trk_len;

    std::vector<NoteEvent> notes;
    uint32_t tick = 0;
    uint8_t running_status = 0;
    std::vector<std::pair<uint8_t, uint32_t>> note_on[128]; // note -> list of (velocity, start_tick)

    while (f.tellg() < (std::streampos)trk_end) {
        uint32_t dt = read_vlq(f);
        tick += dt;
        uint8_t b = f.peek();
        if (b < 0x80) b = running_status; else b = f.get();
        running_status = b;

        if ((b & 0xF0) == 0x90) { // Note on
            uint8_t note = f.get();
            uint8_t vel = f.get();
            if (vel > 0)
                note_on[note].emplace_back(vel, tick);
            else if (!note_on[note].empty()) { // Note off (vel=0)
                notes.push_back({note_on[note].back().second, tick, note, note_on[note].back().first});
                note_on[note].pop_back();
            }
        } else if ((b & 0xF0) == 0x80) { // Note off
            uint8_t note = f.get();
            uint8_t vel = f.get();
            if (!note_on[note].empty()) {
                notes.push_back({note_on[note].back().second, tick, note, note_on[note].back().first});
                note_on[note].pop_back();
            }
        } else if (b == 0xFF) { // Meta event
            f.get();
            uint8_t type = f.get();
            uint32_t len = read_vlq(f);
            f.ignore(len);
        } else if ((b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0) {
            f.get(); // program change / channel pressure: 1 data byte
        } else {
            f.get(); f.get(); // skip 2 bytes
        }
    }

    // Write CSV header
    out << "note,start_tick,end_tick,velocity\n";
    for (const auto& n : notes) {
        out << int(n.note) << "," << n.start_tick << "," << n.end_tick << "," << int(n.velocity) << "\n";
    }
    std::cout << "Wrote " << notes.size() << " notes to " << argv[2] << "\n";
    return 0;
}

# monitor_mouse — USB mouse monitor (Windows)

Small Windows CLI tool to monitor USB mice (attach/detach) and optionally capture live mouse events via Raw Input.

Features
- Detect mouse add / remove events (via Raw Input enumeration)
- Capture live mouse events (movement, buttons) using Raw Input WM_INPUT messages (--capture)
- Optional logging to file (--output=path) and JSON-ish output (--json)
- Graceful shutdown on Ctrl+C

Quick start
1. Build (see options below)
2. Run:

  - Monitor only: .\monitor_mouse.exe
  - Monitor + capture: .\monitor_mouse.exe --capture
  - Write logs to file: .\monitor_mouse.exe --output=monitor.log
  - JSON log file: .\monitor_mouse.exe --output=monitor.json --json

Examples (PowerShell)

  # Display and save output at once
  .\monitor_mouse.exe --capture | Tee-Object -FilePath monitor.log

  # Use built-in output file (also prints to console)
  .\monitor_mouse.exe --capture --output=monitor.log

Keyboard monitor

  .\monitor_keyboard.exe
  .\monitor_keyboard.exe --capture
  .\monitor_keyboard.exe --capture --output=keyboard.log

DNS monitor (requires Npcap/WinPcap)

  1) Install Npcap (https://nmap.org/npcap/) and select "WinPcap API-compatible mode" if asked.
  2) List adapters: `.\\monitor_dns.exe`
  3) Capture queries on adapter 0: `.\\monitor_dns.exe --iface=0`
  4) Capture and log: `.\\monitor_dns.exe --iface=0 --output=dns.log`

Notes: The DNS monitor uses libpcap to capture UDP/TCP port 53 and prints query names. Run with sufficient privileges.

Build

Option A — Visual Studio (recommended)

1. Open "Developer PowerShell for VS" (or Developer Command Prompt)
2. From repo root:

   cl /EHsc /std:c++17 src\monitor_mouse.cpp setupapi.lib

Option B — MinGW-w64 (g++)

1. Ensure you have a MinGW-w64 build (with headers for Windows API)
2. From repo root:

   g++ -std=c++17 -O2 src/monitor_mouse.cpp -lsetupapi -o monitor_mouse.exe

Option C — CMake

1. mkdir build && cd build
2. cmake .. -G "MinGW Makefiles"   # or "Visual Studio 17 2022" for MSVC
3. cmake --build . --config Release

Testing

1. Start monitor: .\monitor_mouse.exe
2. Unplug/plug a USB mouse — the console should show [REMOVED] and [ADDED]
3. Run with --capture and move/click the mouse — you should see [INPUT] lines

Notes
- Windows-only: uses Raw Input (GetRawInputDeviceList / RegisterRawInputDevices) and SetupAPI for friendly names
- File logging appends to the output file (so you can keep old logs)
- JSON mode produces simple {"message": "..."} lines (useful for simple parsing)

Files
- src/monitor_mouse.cpp — main source
- build.ps1 — convenience build script
- run.ps1 — example run/test commands
- CMakeLists.txt — optional CMake project

License: MIT (use as you need)

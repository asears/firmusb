<# build.ps1 — Build helper for Windows (MSVC or MinGW) #>
param(
    [string]$Config = "release"
)

Write-Host "Building monitor_mouse..."

if (Get-Command cl -ErrorAction SilentlyContinue) {
    Write-Host "Using MSVC (cl.exe)"
    cl /EHsc /std:c++17 src\monitor_mouse.cpp setupapi.lib user32.lib
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
    Write-Host "Building monitor_usb..."
    cl /EHsc /std:c++17 src\monitor_usb.cpp setupapi.lib user32.lib
    if ($LASTEXITCODE -ne 0) { throw "monitor_usb build failed" }
    Write-Host "Building usb_debug..."
    cl /EHsc /std:c++17 src\usb_debug.cpp setupapi.lib
    if ($LASTEXITCODE -ne 0) { throw "usb_debug build failed" }
    Write-Host "Building soundtracker (waveOut demo scene synth)"
    cl /EHsc /std:c++17 src\soundtracker.cpp winmm.lib user32.lib /Fe:soundtracker.exe
    Write-Host "Building soundtracker_aceofbase (Ace of Base demo scene synth)"
    cl /EHsc /std:c++17 src\soundtracker_aceofbase.cpp winmm.lib /Fe:soundtracker_aceofbase.exe
    if ($LASTEXITCODE -ne 0) { throw "soundtracker_aceofbase build failed" }
    if ($LASTEXITCODE -ne 0) { throw "soundtracker build failed" }
} elseif (Get-Command g++ -ErrorAction SilentlyContinue) {
    Write-Host "Using g++"
    g++ -std=c++17 -O2 src\monitor_mouse.cpp -lsetupapi -o monitor_mouse.exe
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
    Write-Host "Building monitor_usb..."
    g++ -std=c++17 -O2 src\monitor_usb.cpp -lsetupapi -o monitor_usb.exe
    if ($LASTEXITCODE -ne 0) { throw "monitor_usb build failed" }
    Write-Host "Building usb_debug..."
    g++ -std=c++17 -O2 src\usb_debug.cpp -lsetupapi -o usb_debug.exe
    if ($LASTEXITCODE -ne 0) { throw "usb_debug build failed" }
    # Try building DNS monitor with libpcap
    if (Get-Command g++ -ErrorAction SilentlyContinue) {
        Write-Host "Building monitor_dns (needs libpcap/Npcap)"
        g++ -std=c++17 -O2 src\monitor_dns.cpp -lpcap -liphlpapi -lws2_32 -o monitor_dns.exe
        if ($LASTEXITCODE -ne 0) { Write-Host "monitor_dns build failed - ensure Npcap/libpcap is installed" }
        Write-Host "Building soundtracker (waveOut demo scene synth)"
        g++ -std=c++17 -O2 src/soundtracker.cpp -lwinmm -luser32 -o soundtracker.exe
        Write-Host "Building soundtracker_aceofbase (Ace of Base demo scene synth)"
        g++ -std=c++17 -O2 src/soundtracker_aceofbase.cpp -lwinmm -o soundtracker_aceofbase.exe
        if ($LASTEXITCODE -ne 0) { Write-Host "soundtracker_aceofbase build failed" }
        if ($LASTEXITCODE -ne 0) { Write-Host "soundtracker build failed" }
    }
} else {
    Write-Host "No supported compiler found. Install Visual Studio or MinGW-w64."
}

Write-Host "Done. Output: monitor_mouse.exe"

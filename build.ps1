<# build.ps1 — Build helper for Windows (MSVC or MinGW) #>
param(
    [string]$Config = "release"
)

Write-Host "Building monitor_mouse..."

if (Get-Command cl -ErrorAction SilentlyContinue) {
    Write-Host "Using MSVC (cl.exe)"
    cl /EHsc /std:c++17 src\monitor_mouse.cpp setupapi.lib user32.lib
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
} elseif (Get-Command g++ -ErrorAction SilentlyContinue) {
    Write-Host "Using g++"
    g++ -std=c++17 -O2 src\monitor_mouse.cpp -lsetupapi -o monitor_mouse.exe
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
    # Try building DNS monitor with libpcap
    if (Get-Command g++ -ErrorAction SilentlyContinue) {
        Write-Host "Building monitor_dns (needs libpcap/Npcap)"
        g++ -std=c++17 -O2 src\monitor_dns.cpp -lpcap -liphlpapi -lws2_32 -o monitor_dns.exe
        if ($LASTEXITCODE -ne 0) { Write-Host "monitor_dns build failed - ensure Npcap/libpcap is installed" }
    }
} else {
    Write-Host "No supported compiler found. Install Visual Studio or MinGW-w64."
}

Write-Host "Done. Output: monitor_mouse.exe"

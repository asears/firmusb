<# run.ps1 — Example run/test commands #>
Write-Host "Example commands:"
Write-Host "  .\monitor_mouse.exe"
Write-Host "  .\monitor_mouse.exe --capture"
Write-Host "  .\monitor_mouse.exe --capture --output=monitor.log"
Write-Host "  .\monitor_mouse.exe --capture | Tee-Object -FilePath monitor.log"
Write-Host "  .\monitor_mouse.exe --capture --output=monitor.log"

Write-Host "Keyboard examples:"
Write-Host "  .\monitor_keyboard.exe"
Write-Host "  .\monitor_keyboard.exe --capture"
Write-Host "  .\monitor_keyboard.exe --capture --output=keyboard.log"

Write-Host "Run the second or third command in an elevated console if you do not see capture output."

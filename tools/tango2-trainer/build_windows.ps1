$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

Write-Host "Installing/updating build dependencies..."
python -m pip install --upgrade pyinstaller hidapi

Write-Host "Verifying runtime imports..."
python -c "import hid; import tkinter; print('HID + Tkinter OK')"

$IconArgs = @()
if (Test-Path "$ScriptDir\Tango2RacingTrainer.ico") {
    $IconArgs = @("--icon", "$ScriptDir\Tango2RacingTrainer.ico")
}

Write-Host "Building Tango 2 Racing Trainer v1.1..."
python -m PyInstaller `
    --noconfirm `
    --clean `
    --onefile `
    --windowed `
    --hidden-import=hid `
    --version-file "$ScriptDir\version_info.txt" `
    @IconArgs `
    --name "Tango2RacingTrainer" `
    "$ScriptDir\tango_trainer_gui_v1_1.py"

Write-Host ""
Write-Host "Build complete:"
Write-Host "$ScriptDir\dist\Tango2RacingTrainer.exe"

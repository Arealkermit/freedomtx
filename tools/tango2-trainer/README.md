# Tango 2 Racing Trainer

Windows GUI utility for a two-radio Tango 2 trainer setup.

## Runtime behavior

- Student Tango 2 is read as a Windows USB joystick.
- Master Tango 2 communicates over USB HID.
- Student control is gated by the trainer logic in the modified master firmware.
- The student arm switch is treated only as a READY signal and is not forwarded as aircraft arming.

## Build prerequisites

- Windows 10/11
- Python 3.14+ recommended
- `hidapi`
- `PyInstaller`

## Build the standalone EXE

From PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build_windows.ps1
```

Output:

```text
dist\Tango2RacingTrainer.exe
```

The build script adds Windows version metadata and automatically uses
`Tango2RacingTrainer.ico` if that file is present in this directory.

## Build the installer

Install Inno Setup 6, then build `Tango2RacingTrainer.iss`.

From a typical Inno Setup installation:

```powershell
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" ".\Tango2RacingTrainer.iss"
```

Output:

```text
installer\Tango2RacingTrainer_Setup_v1.1.exe
```

## Distribution note

This is an independent utility and should not be represented as an official
Team BlackSheep/TBS product unless explicit authorization is obtained.

## Source

The application currently identifies itself as version 1.1.

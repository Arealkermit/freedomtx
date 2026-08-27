# Tango 2 Racing Trainer

Windows GUI utility for a two-radio Tango 2 trainer setup.

## Download

### [⬇ Download the latest Windows installer](https://github.com/Arealkermit/freedomtx/releases/latest/download/Tango2RacingTrainer_Setup.exe)

Normal users should use the installer above. They do **not** need Python, Git, PyInstaller, or Inno Setup.

## Runtime behavior

- Student Tango 2 is read as a Windows USB joystick.
- Master Tango 2 communicates over USB HID.
- Student control is gated by the trainer logic in the modified master firmware.
- The student arm switch is treated only as a READY signal and is not forwarded as aircraft arming.

## USB modes

Student:

```text
USB Joystick (HID)
```

Master / Instructor:

```text
USB Agent (HID)
```

## Current tested firmware build

```text
tango2-ghst-usbtrainer-override-local
```

See the repository's main README for the Switch E + TBS Agent X Developer Mode firmware installation procedure.

## Build prerequisites

The following are only needed by developers building the application from source:

- Windows 10/11
- Python 3.14+ recommended
- `hidapi`
- `PyInstaller`

## Build the standalone EXE

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\\build_windows.ps1
```

Output:

```text
dist\\Tango2RacingTrainer.exe
```

## Build the installer manually

Install Inno Setup 6, then:

```powershell
& "${env:ProgramFiles(x86)}\\Inno Setup 6\\ISCC.exe" ".\\Tango2RacingTrainer.iss"
```

## Automated releases

Pushing a tag such as:

```powershell
git tag trainer-v1.1
git push origin trainer-v1.1
```

runs `.github/workflows/release-trainer.yml`.

The workflow builds and publishes the stable Windows release asset:

```text
Tango2RacingTrainer_Setup.exe
```

## Distribution note

This is an independent utility and should not be represented as an official Team BlackSheep/TBS product unless explicit authorization is obtained.

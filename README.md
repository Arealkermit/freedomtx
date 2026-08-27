## FreedomTX

FreedomTX is a fork of OpenTX. Support OpenTX through donations!  
[![Donate to OpenTX using Paypal](https://img.shields.io/badge/paypal-donate-yellow.svg)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=DJ9MASSKVW8WN)

Build and setup instructions can be found [here](https://github.com/opentx/opentx/wiki/Compiling-OpenTx)

---

# Tango 2 GHST / USB Racing Trainer

This branch contains the modified Tango 2 firmware used with the Tango 2 USB Racing Trainer setup.

## Current trainer firmware

The current tested build is:

```text
tango2-ghst-usbtrainer-override-local
```

All Tango 2 controllers used with the trainer system should be updated to the same current trainer firmware build. This keeps the radios interchangeable and avoids problems caused by mixing older development firmware with the current trainer application.

Older trainer firmware should be treated as archived/rollback firmware and should not be used for normal trainer operation.

## Installing the trainer firmware with TBS Agent X

Back up any models or radio settings that need to be retained before updating firmware.

### 1. Put the Tango 2 into the firmware update / bootloader connection

1. Power the Tango 2 **off**.
2. Disconnect the USB cable from the Tango 2.
3. Press and hold **Switch E** on the Tango 2.
4. While continuing to hold **E**, plug the USB cable into the computer.
5. Keep holding **E** until TBS Agent X detects the FreedomTX/Tango 2 firmware-update connection.
6. Once the radio is detected, release **E**.

If Agent X does not detect the radio, disconnect USB and repeat the procedure while making sure **E is already held before USB is connected**.

### 2. Enable Developer Mode in TBS Agent X

The trainer firmware is a local/custom FreedomTX build, so it will not appear as a normal public TBS firmware release.

1. Open **TBS Agent X**.
2. Enable **Developer Mode** in Agent X.
3. Open the connected Tango 2 / FreedomTX device.
4. Go to the firmware update section.
5. Locate the local/developer firmware builds.

### 3. Select the correct build

Select:

```text
tango2-ghst-usbtrainer-override-local
```

Do **not** select an older `tango2-ghst`, USB trainer test, override test, or other development build unless intentionally performing a rollback.

### 4. Flash the firmware

1. Start the firmware update for `tango2-ghst-usbtrainer-override-local`.
2. Allow Agent X to complete the erase/write/verification process.
3. **Do not disconnect USB or remove power while the firmware is being written.**
4. Wait until Agent X reports that the update has completed.
5. Disconnect USB.
6. Power-cycle the Tango 2 normally.
7. Confirm that FreedomTX starts normally.

Repeat this process for **every Tango 2 that will be used with the trainer system** so all controllers are on the same current firmware.

## Trainer USB roles

After the firmware is installed, the radios are connected to the trainer computer in different USB modes depending on their role:

**Student Tango 2**

```text
USB Joystick (HID)
```

**Master / Instructor Tango 2**

```text
USB Agent (HID)
```

The Master firmware provides the USB trainer input path, trainer-input failsafe/status reporting, and instructor override behavior used by the Windows trainer utility.

## Firmware version policy

For normal use, there should be only one clearly identified current trainer firmware build:

```text
tango2-ghst-usbtrainer-override-local
```

When a newer tested build replaces it:

- update every trainer-fleet Tango 2 to the new build;
- mark the previous build as archived/unsupported for normal use;
- keep old builds only for rollback or troubleshooting;
- do not leave several unlabeled `firmware.bin` files in the normal installation/download location.

Before removing an old known-good firmware completely, keep at least one archived recovery copy until the replacement has been successfully flashed and tested on the trainer system.

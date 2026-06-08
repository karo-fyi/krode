# krode

Delete onboard recordings from supported **Rode** wireless microphone transmitters on Linux.

Rode only officially supports this through their proprietary **Rode Central** software (Windows/macOS). This tool replicates the delete function via the device's USB HID control interface, which is accessible on Linux.

## Requirements

- `libhidapi-hidraw` (runtime)
- `libhidapi-dev` (build)

```
# Debian/Ubuntu
sudo apt install libhidapi-dev

# Fedora
sudo dnf install hidapi-devel
```

## Build

```
make
```

## Install

Install the Debian package (recommended). Download the latest `.deb` from the [releases page](https://github.com/LinuxRenaissance/krode/releases), then:

```
sudo dpkg -i krode_*.deb
```

Or install the udev rule manually so the tool can run without `sudo`:

```
sudo cp udev/99-rode-interview-pro.rules /usr/lib/udev/rules.d/
sudo udevadm control --reload-rules
```

Then add yourself to the `plugdev` group (log out and back in after):

```
sudo usermod -aG plugdev $USER
```

## Usage

Connect a supported TX unit via USB, then:

```
krode delete
```

This deletes all onboard recordings. The device confirms with a status code of 100 on success.

## Protocol

Supported transmitters present a USB HID control interface:

- **Mass Storage (SCSI)** — read-only; used to copy WAV files to the host
- **HID (vendor-specific)** — used for control commands

Device identifiers:

| Unit | VID | PID |
|------|-----|-----|
| Interview PRO TX (unit 1) | `0x19F7` | `0x0063` |
| Interview PRO TX (unit 2) | `0x19F7` | `0x0068` |
| Wireless PRO TX (both units) | `0x19F7` | `0x0056` |

### Delete command

HID Output Report, 17 bytes, sent via SET_REPORT:

```
01 4A 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

| Byte | Value | Meaning |
|------|-------|---------|
| `[0]` | `0x01` | HID Report ID |
| `[1]` | `0x4A` | Command: Delete |
| `[2]` | `0x01` | Parameter: Delete All |
| `[3..16]` | `0x00` | Padding |

### Device ACK

Response arrives on the HID interrupt IN endpoint, 17 bytes:

```
02 4A 41 64 00 00 00 00 00 00 00 00 00 00 00 00 00
```

| Byte | Value | Meaning |
|------|-------|---------|
| `[0]` | `0x02` | HID Report ID (input) |
| `[1]` | `0x4A` | Echo of command byte |
| `[2]` | `0x41` | ACK (`'A'`); NAK would be `0x4E` (`'N'`) |
| `[3]` | `0x64` | Status: 100 = success |

Reverse-engineered from USB traffic captured with Wireshark + USBPcap on Windows.
Confirmed working on TX unit 1 (PID `0x0063`). PID `0x0068` is untested for delete.

Wireless PRO TX PID `0x0056` is supported by device ID.

## Support

For questions, troubleshooting, or general discussion, please use the
Linux Renaissance forum thread:

https://forum.linuxrenaissance.com/t/how-to-delete-recordings-from-rode-interview-pro-on-linux/42

GitHub issues are reserved for confirmed bugs and feature requests.

## License

BSD-2-Clause. See [LICENSE](LICENSE).

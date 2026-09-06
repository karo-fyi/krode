# krode

Delete onboard recordings from **Rode wireless microphone transmitters** on Linux — Interview PRO and Wireless PRO.

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

Connect the transmitter via USB — a Wireless PRO also works while docked in its
charging case — then:

```
krode list
```

```
Wireless PRO TX             serial 800A92D6  /dev/hidraw12
Wireless PRO TX             serial 800AF63E  /dev/hidraw10
```

```
krode delete            # every connected transmitter
krode delete 800AF63E   # just that one
```

The device reports progress as it erases, and `krode` waits for 100 %.

**Two Wireless PRO transmitters share PID `0x0056`**, so a serial is the only
way to tell them apart. Conveniently the HID serial is also the FAT volume UUID
of that transmitter's storage: mount the volume, look at the filenames, and you
know which microphone a serial belongs to.

Afterwards the transmitter re-enumerates and its storage comes back **without a
filesystem** until the device is unplugged and replugged. Waiting does not help.

This is irreversible and there is no confirmation prompt. Copy your recordings
off first — the storage is read-only at the device level, so this command is
the only way to clear it.

## Protocol

These transmitters present as composite USB devices:

- **Mass Storage (SCSI)** — read-only; used to copy WAV files to the host
- **HID (vendor-specific)** — used for control commands

Device identifiers:

| Unit | VID | PID |
|------|-----|-----|
| Interview PRO TX (unit 1) | `0x19F7` | `0x0063` |
| Interview PRO TX (unit 2) | `0x19F7` | `0x0068` |
| Wireless PRO TX | `0x19F7` | `0x0056` |

Note that the two Interview PRO units have distinct PIDs, while both Wireless
PRO transmitters answer to `0x0056` — which is why devices are enumerated and
opened by path, rather than with `hid_open(vid, pid, NULL)`.

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

Response arrives on endpoint `0x81` (URB_INTERRUPT IN), 17 bytes:

The device does not answer once — it streams progress reports on endpoint
`0x81` (URB_INTERRUPT IN), 17 bytes each:

```
02 4A 41 00     0 %
02 4A 41 05     5 %
02 4A 41 0A    10 %
   ...
02 4A 41 64   100 %
```

| Byte | Value | Meaning |
|------|-------|---------|
| `[0]` | `0x02` | HID Report ID (input) |
| `[1]` | `0x4A` | Echo of command byte |
| `[2]` | `0x41` | ACK (`'A'`); NAK would be `0x4E` (`'N'`) |
| `[3]` | `0x00`..`0x64` | Progress, 0 to 100 in steps of 5 |

Byte `[3]` is a **percentage**, not a status constant: `100` means the erase
finished. Stopping at the first reply reports success before the device is done.
Erasing 6.1 GB took 1.8 s.

Reverse-engineered from USB traffic captured with Wireshark + USBPcap on Windows.
Confirmed working on Interview PRO TX unit 1 (PID `0x0063`) and on both
Wireless PRO transmitters (PID `0x0056`), the latter both connected directly and
docked in the charging case. Interview PRO PID `0x0068` is untested for delete.

## Support

For questions, troubleshooting, or general discussion, please use the
Linux Renaissance forum thread:

https://forum.linuxrenaissance.com/t/how-to-delete-recordings-from-rode-interview-pro-on-linux/42

GitHub issues are reserved for confirmed bugs and feature requests.

## License

BSD-2-Clause. See [LICENSE](LICENSE).

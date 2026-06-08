/*
 * krode - delete onboard recordings from supported Rode transmitters on Linux
 *
 * Usage: krode delete
 *
 * Reverse-engineered from USB HID traffic capture.
 * Delete command: Report ID 0x01, command byte 0x4A, param 0x01
 *
 * Build: gcc -o krode krode.c -lhidapi-hidraw
 * Deps:  libhidapi-dev (Debian/Ubuntu), hidapi-devel (Fedora)
 *
 * License: BSD-2-Clause
 */

#include <stdio.h>
#include <string.h>
#include <hidapi/hidapi.h>

#define RODE_VID        0x19F7

#define ARRAY_SIZE(a)   (sizeof(a) / sizeof((a)[0]))

struct rode_device {
	unsigned short pid;
	const char *name;
};

static const struct rode_device rode_devices[] = {
	{ 0x0063, "Interview PRO TX (unit 1)" },
	{ 0x0068, "Interview PRO TX (unit 2)" },

	// Note: Unlike Interview PRO TX units, Wireless PRO TX units, as-tested, advertise
	//       the same USB PID. Matching on hidraw paths is required when both Wireless
	//       PRO TX units are plugged in.
	{ 0x0056, "Wireless PRO TX" },
};

#define REPORT_SIZE     17
#define CMD_DELETE      0x4A
#define DELETE_ALL      0x01

static int delete_recordings(hid_device *dev, const struct rode_device *device)
{
	unsigned char buf[REPORT_SIZE];
	int ret;

	memset(buf, 0, sizeof(buf));
	buf[0] = 0x01;		/* Report ID */
	buf[1] = CMD_DELETE;	/* 0x4A - delete command */
	buf[2] = DELETE_ALL;	/* 0x01 - delete all */

	printf("Sending delete command to %s (PID 0x%04X)...\n", device->name, device->pid);

	ret = hid_write(dev, buf, sizeof(buf));
	if (ret < 0) {
		fprintf(stderr, "hid_write failed: %ls\n", hid_error(dev));
		return -1;
	}

	/* Read response - device ACKs with 0x4A 0x41 0x64 */
	memset(buf, 0, sizeof(buf));
	ret = hid_read_timeout(dev, buf, sizeof(buf), 3000);
	if (ret < 0) {
		fprintf(stderr, "hid_read failed: %ls\n", hid_error(dev));
		return -1;
	}
	if (ret == 0) {
		fprintf(stderr, "Timeout waiting for device response.\n");
		return -1;
	}

	if (buf[1] == CMD_DELETE && buf[2] == 0x41) {
		printf("Done. Recordings deleted (status: %d).\n", buf[3]);
		return 0;
	}

	/* Print raw response for debugging */
	fprintf(stderr, "Unexpected response: ");

	for (int i = 0; i < ret; i++) {
		fprintf(stderr, "%02x ", buf[i]);
	}

	fprintf(stderr, "\n");

	return -1;
}

static int run_on_device(const struct rode_device *device)
{
	struct hid_device_info *devs;
	struct hid_device_info *cur;
	hid_device *dev;
	int found = 0;
	int failed = 0;

	devs = hid_enumerate(RODE_VID, device->pid);
	if (!devs)
		return -1;	/* not found, not an error if other PID works */

	// Enumerate all found devices
	for (cur = devs; cur; cur = cur->next) {
		if (!cur->path)
			continue;

		found++;

		printf("Opening %s (PID 0x%04X, path %s)...\n", device->name, device->pid, cur->path);

		dev = hid_open_path(cur->path);
		if (!dev) {
			fprintf(stderr, "hid_open_path failed for %s\n", cur->path);
			failed++;
			continue;
		}

		if (delete_recordings(dev, device) != 0)
			failed++;

		hid_close(dev);
	}

	hid_free_enumeration(devs);

	if (found == 0)
		return -1;

	return failed == 0 ? 0 : -2;
}

int main(int argc, char *argv[])
{
	int found = 0;
	int failed = 0;

	if (argc < 2 || strcmp(argv[1], "delete") != 0) {
		fprintf(stderr, "Usage: %s delete\n", argv[0]);
		fprintf(stderr, "  delete  - delete all onboard recordings\n");
		return 1;
	}

	if (hid_init() != 0) {
		fprintf(stderr, "hid_init failed\n");
		return 1;
	}

	for (size_t i = 0; i < ARRAY_SIZE(rode_devices); i++) {
		int ret = run_on_device(&rode_devices[i]);

		if (ret == -1)
			continue;

		found++;
		if (ret == 0)
			continue;
		failed++;
	}

	if (found == 0) {
		fprintf(stderr,
		    "No supported Rode transmitter found. Check USB connection.\n");
		fprintf(stderr,
		    "You may need to run as root or add a udev rule.\n");
		hid_exit();
		return 1;
	}

	hid_exit();
	return failed == 0 ? 0 : 1;
}

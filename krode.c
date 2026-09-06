/*
 * krode - delete onboard recordings from Rode transmitters on Linux
 *
 * Usage: krode list
 *        krode delete [serial]
 *
 * Reverse-engineered from USB HID traffic capture.
 * Delete command: Report ID 0x01, command byte 0x4A, param 0x01
 * The device answers 0x02 0x4A 0x41 <percent>, counting up to 100.
 *
 * Build: gcc -o krode krode.c -lhidapi-hidraw
 * Deps:  libhidapi-dev (Debian/Ubuntu), hidapi-devel (Fedora)
 *
 * License: BSD-2-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <wchar.h>
#include <hidapi/hidapi.h>

#define RODE_VID	0x19F7

#define REPORT_SIZE	17
#define CMD_DELETE	0x4A
#define DELETE_ALL	0x01
#define ACK_OK		0x41
#define DONE_PERCENT	100

/* Reply timeout, and how long we let the device keep reporting progress. */
#define REPLY_MS	3000
#define PROGRESS_MS	120000

struct model {
	unsigned short pid;
	const char *name;
};

/*
 * Interview PRO gives its two transmitters distinct PIDs, so VID/PID alone
 * identifies them. Wireless PRO does not: both transmitters answer to 0x0056,
 * which is why devices are enumerated and opened by path rather than with
 * hid_open(vid, pid, NULL) - that would always return the same one.
 */
static const struct model models[] = {
	{ 0x0063, "Interview PRO TX (unit 1)" },
	{ 0x0068, "Interview PRO TX (unit 2)" },
	{ 0x0056, "Wireless PRO TX" },
};

static const char *model_name(unsigned short pid)
{
	size_t i;

	for (i = 0; i < sizeof(models) / sizeof(models[0]); i++)
		if (models[i].pid == pid)
			return models[i].name;
	return NULL;
}

static int serial_matches(const wchar_t *serial, const char *wanted)
{
	char buf[64];

	if (!serial)
		return 0;
	if (wcstombs(buf, serial, sizeof(buf)) == (size_t)-1)
		return 0;
	return strcasecmp(buf, wanted) == 0;
}

static void print_serial(const wchar_t *serial)
{
	if (serial && *serial)
		printf("%ls", serial);
	else
		printf("(none)");
}

/*
 * On a Wireless PRO the serial is also the FAT volume UUID of that
 * transmitter's storage, so mounting the volume tells you which physical
 * microphone a serial belongs to.
 */
static int list_devices(void)
{
	struct hid_device_info *devs, *cur;
	int found = 0;

	devs = hid_enumerate(RODE_VID, 0);
	for (cur = devs; cur; cur = cur->next) {
		const char *name = model_name(cur->product_id);

		if (!name)
			continue;
		printf("%-26s  serial ", name);
		print_serial(cur->serial_number);
		printf("  %s\n", cur->path);
		found++;
	}
	hid_free_enumeration(devs);

	if (!found)
		printf("No supported Rode transmitter found.\n");
	return found;
}

static int delete_recordings(hid_device *dev, const char *label)
{
	unsigned char buf[REPORT_SIZE];
	int ret, percent = -1;

	memset(buf, 0, sizeof(buf));
	buf[0] = 0x01;		/* Report ID */
	buf[1] = CMD_DELETE;	/* 0x4A - delete command */
	buf[2] = DELETE_ALL;	/* 0x01 - delete all */

	printf("Deleting recordings on %s...\n", label);

	ret = hid_write(dev, buf, sizeof(buf));
	if (ret < 0) {
		fprintf(stderr, "hid_write failed: %ls\n", hid_error(dev));
		return -1;
	}

	/*
	 * The device does not answer once: it streams progress reports,
	 * 0x02 0x4A 0x41 <percent>, stepping to 100. Stopping at the first
	 * reply reports success before the erase has finished.
	 */
	for (;;) {
		memset(buf, 0, sizeof(buf));
		ret = hid_read_timeout(dev, buf, sizeof(buf),
				       percent < 0 ? REPLY_MS : PROGRESS_MS);
		if (ret < 0) {
			fprintf(stderr, "hid_read failed: %ls\n",
				hid_error(dev));
			return -1;
		}
		if (ret == 0) {
			fprintf(stderr,
				"Timeout waiting for device response.\n");
			return -1;
		}
		if (buf[1] != CMD_DELETE)
			continue;
		if (buf[2] != ACK_OK) {
			fprintf(stderr, "Device refused the command (0x%02x).\n",
				buf[2]);
			return -1;
		}
		if (buf[3] != percent) {
			percent = buf[3];
			/* Overwrite in place on a terminal; stay quiet when
			 * redirected, so a log does not fill with percents. */
			if (isatty(STDOUT_FILENO)) {
				printf("\r  %3d%%", percent);
				fflush(stdout);
			}
		}
		if (percent >= DONE_PERCENT)
			break;
	}

	if (isatty(STDOUT_FILENO))
		printf("\r");
	printf("Done. Recordings deleted.\n");
	printf("The transmitter re-enumerates; unplug and replug it before\n"
	       "expecting its storage volume back.\n");
	return 0;
}

static int delete_matching(const char *serial)
{
	struct hid_device_info *devs, *cur;
	int found = 0;

	devs = hid_enumerate(RODE_VID, 0);
	for (cur = devs; cur; cur = cur->next) {
		const char *name = model_name(cur->product_id);
		char label[128];
		hid_device *dev;

		if (!name)
			continue;
		if (serial && !serial_matches(cur->serial_number, serial))
			continue;

		snprintf(label, sizeof(label), "%s (%ls)", name,
			 cur->serial_number ? cur->serial_number : L"no serial");

		/*
		 * Opened by path, resolved from this same enumeration: hidraw
		 * node numbers are not stable, and they have been observed to
		 * swap between two transmitters across a replug.
		 */
		dev = hid_open_path(cur->path);
		if (!dev) {
			fprintf(stderr, "Cannot open %s: %s\n", label,
				cur->path);
			continue;
		}
		delete_recordings(dev, label);
		hid_close(dev);
		found++;
	}
	hid_free_enumeration(devs);
	return found;
}

static void usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s list\n", argv0);
	fprintf(stderr, "       %s delete [serial]\n", argv0);
	fprintf(stderr, "  list    - show connected transmitters\n");
	fprintf(stderr, "  delete  - delete all onboard recordings;\n");
	fprintf(stderr, "            give a serial to target one transmitter\n");
}

int main(int argc, char *argv[])
{
	int found;

	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	if (hid_init() != 0) {
		fprintf(stderr, "hid_init failed\n");
		return 1;
	}

	if (strcmp(argv[1], "list") == 0) {
		found = list_devices();
		hid_exit();
		return found ? 0 : 1;
	}

	if (strcmp(argv[1], "delete") != 0) {
		usage(argv[0]);
		hid_exit();
		return 1;
	}

	found = delete_matching(argc > 2 ? argv[2] : NULL);

	if (found == 0) {
		if (argc > 2)
			fprintf(stderr,
			    "No transmitter with serial %s. Try: %s list\n",
			    argv[2], argv[0]);
		else
			fprintf(stderr,
			    "No supported Rode transmitter found. "
			    "Check USB connection.\n"
			    "You may need to run as root or add a udev rule.\n");
		hid_exit();
		return 1;
	}

	hid_exit();
	return 0;
}

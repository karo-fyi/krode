/*
 * krode - delete onboard recordings from Rode Interview PRO on Linux
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
#include <unistd.h>
#include <hidapi/hidapi.h>

#define RODE_VID        0x19F7

/* Both TX units seen on Interview PRO */
#define RODE_PID_TX1    0x0063
#define RODE_PID_TX2    0x0068

#define REPORT_SIZE     17
#define CMD_DELETE      0x4A
#define DELETE_ALL      0x01
#define ACK_OK          0x41
#define DONE_PERCENT    100

/* First reply, then however long the device needs to work through storage. */
#define REPLY_MS        3000
#define PROGRESS_MS     120000

static int delete_recordings(hid_device *dev, unsigned short pid)
{
	unsigned char buf[REPORT_SIZE];
	int ret, percent = -1;

	memset(buf, 0, sizeof(buf));
	buf[0] = 0x01;		/* Report ID */
	buf[1] = CMD_DELETE;	/* 0x4A - delete command */
	buf[2] = DELETE_ALL;	/* 0x01 - delete all */

	printf("Sending delete command to PID 0x%04X...\n", pid);

	ret = hid_write(dev, buf, sizeof(buf));
	if (ret < 0) {
		fprintf(stderr, "hid_write failed: %ls\n", hid_error(dev));
		return -1;
	}

	/*
	 * The device does not answer once. It streams progress reports,
	 * 02 4A 41 <percent>, stepping to 100 - byte [3] is a percentage,
	 * not a status constant. Reading a single reply announces success
	 * while the erase is still running.
	 */
	for (;;) {
		memset(buf, 0, sizeof(buf));
		ret = hid_read_timeout(dev, buf, sizeof(buf),
				       percent < 0 ? REPLY_MS : PROGRESS_MS);
		if (ret <= 0) {
			/*
			 * The transmitter drops off the bus as soon as it is
			 * done, so losing it after progress has been reported
			 * is the normal ending, not a failure. Losing it
			 * before any reply is not.
			 */
			if (percent < 0) {
				if (ret < 0)
					fprintf(stderr, "hid_read failed: %ls\n",
						hid_error(dev));
				else
					fprintf(stderr, "Timeout waiting for "
						"device response.\n");
				return -1;
			}
			if (isatty(STDOUT_FILENO))
				printf("\r");
			printf("Device disconnected at %d%% - it re-enumerates "
			       "when the erase finishes.\n", percent);
			return 0;
		}

		if (buf[1] != CMD_DELETE || buf[2] != ACK_OK)
			break;		/* unexpected: dump it below */

		if (buf[3] != percent) {
			percent = buf[3];
			/* Overwrite in place on a terminal, stay quiet when
			 * redirected so logs do not fill with percents. */
			if (isatty(STDOUT_FILENO)) {
				printf("\r  %3d%%", percent);
				fflush(stdout);
			}
		}

		if (percent >= DONE_PERCENT) {
			if (isatty(STDOUT_FILENO))
				printf("\r");
			printf("Done. Recordings deleted.\n");
			return 0;
		}
	}

	/* Print raw response for debugging */
	printf("Response: ");
	for (int i = 0; i < ret; i++)
		printf("%02x ", buf[i]);
	printf("\n");

	return 0;
}

static int run_on_pid(unsigned short pid)
{
	hid_device *dev;

	dev = hid_open(RODE_VID, pid, NULL);
	if (!dev)
		return -1;	/* not found, not an error if other PID works */

	delete_recordings(dev, pid);
	hid_close(dev);
	return 0;
}

int main(int argc, char *argv[])
{
	int found = 0;

	if (argc < 2 || strcmp(argv[1], "delete") != 0) {
		fprintf(stderr, "Usage: %s delete\n", argv[0]);
		fprintf(stderr, "  delete  - delete all onboard recordings\n");
		return 1;
	}

	if (hid_init() != 0) {
		fprintf(stderr, "hid_init failed\n");
		return 1;
	}

	if (run_on_pid(RODE_PID_TX1) == 0) found++;
	if (run_on_pid(RODE_PID_TX2) == 0) found++;

	if (found == 0) {
		fprintf(stderr,
		    "No Rode Interview PRO found. Check USB connection.\n");
		fprintf(stderr,
		    "You may need to run as root or add a udev rule.\n");
		hid_exit();
		return 1;
	}

	hid_exit();
	return 0;
}

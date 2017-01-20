/* main.c: tu58fs user interface, global resources
 *
 * TU58 emulator for serial port, with file sharing to host
 *
 * Copyright (c) 2017, Joerg Hoppe, j_hoppe@t-online.de, www.retrocmp.com
 * partly code from Donald N North <ak6dn_at_mindspring_dot_com>
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 *  - Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * 12-Jan-2017    JH  V 0.1   Edit start
 *
 */

#define _MAIN_C_

#define VERSION	"v0.1"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdarg.h>
#include <strings.h>
#include <pthread.h>

#include "utils.h"
#include "getopt2.h"
#include "serial.h"
#include "hostdir.h"
#include "image.h"
#include "tu58.h"
#include "tu58drive.h"

#include "xxdp.h"	// for test

#include "main.h"   // own

uint8_t *dbgdata;
uint32_t dbgdatasize;

static char copyright[] = "(C) 2017 Joerg Hoppe <j_hoppe@t-online.de>,\n"
		"(C) 2005-2017 Don North <ak6dn" "@" "mindspring.com>,\n"
		"(C) 1984 Dan Ts'o <Rockefeller University>";

static char version[] = "tu58fs - DEC TU58 tape emulator with file sharing "VERSION "\n"
"(compile "__DATE__ " " __TIME__ ")";

// global options
char opt_port[256] = "1"; // default port number (COM1, /dev/ttyS0)
int opt_speed = 9600; // default line speed
int opt_stopbits = 1; // default stop bits, 1 or 2
int opt_verbose = 0; // set nonzero to output more info
int opt_timing = 0; // set nonzero to add timing delays
int opt_mrspen = 0; // set nonzero to enable MRSP mode
int opt_nosync = 0; // set nonzero to skip sending INIT at restart
int opt_debug = 0; // set nonzero for debug output
int opt_vax = 0; // set to remove delays for aggressive VAX console timeouts
int opt_background = 0; // set to run in background mode (no console I/O except errors)
int opt_synctimeout_sec = 0; // save changed image to disk after so many seconds of write-inactivity
int opt_offlinetimeout_sec = 0; // TU58 waits with "offline" until so many seconds of RS232-inactivity

int arg_menu_linewidth = 80;

// command line args
static getopt_t getopt_parser;
static int image_count;

//
// print an info message and return
//
void info(char *fmt, ...) {
	va_list args;
	if (!opt_background) {
		va_start(args, fmt);
		fprintf(ferr, "info: ");
		vfprintf(ferr, fmt, args);
		fprintf(ferr, "\n");
		va_end(args);
	}
	return;
}

//
// print an error message and return
//
void error(char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	fprintf(ferr, "ERROR: ");
	vfprintf(ferr, fmt, args);
	fprintf(ferr, "\n");
	va_end(args);
	return;
}

//
// print an error message and die
//
void fatal(char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	fprintf(ferr, "FATAL: ");
	vfprintf(ferr, fmt, args);
	fprintf(ferr, "\n");
	va_end(args);
	exit(EXIT_FAILURE);
}

/*
 * help()
 */
static char * examples[] =
		{
				"sudo ./" PROGNAME "-p /dev/ttyS1 -b 38400 -d 0 r 11XXDP.DSK\n", //
				"    Define device #0:\n", //
				"    Access to serial line requires sudo. Image is readonly.\n", //
				"    If it not exist, an error is signaled.\n",
				"\n", //
				PROGNAME " <serial params> -d 0 c 11XXDP.DSK\n", //
				"    Same. If \"11XXDP.DSK\" does not exists, an unformatted 0-filled image file is created.\n", //
				"\n", //
				PROGNAME " <serial params> -rt11 -sd 1 w tinyrt11\n", //
				"    Shared device image for device #1, \"tinyrt11\" is a directory:\n", //
				"    Image is constructed from file content, to a max size of 256kb, else error.\n", //
				"    Can be accessed with \"DD1:\"\n", //
				"\n",
				PROGNAME "<serial params> -size auto -rt11 -d 1 w tinyrt11\n", //
				"    Same, but device image is automatically enlarged to > 256KB, max 32MB.\n", //
				"    A modified DD.SYS driver must be used on the RT11 system,\n", //
				"\n", //
				PROGNAME " <serial params> -size std -xxdp -d 0 r 11XXDP.DSK  -size auto -d 1 w data.dsk -sd 7 w shared.dir\n", //
				"    Define device #0:\n", //
				"    Standard 256kb image, loaded from file \"11XXDP.DSK\", not created.\n", //
				"    Device #1:\n", //
				"    If \"data.dsk\" does not exist, a file is create and formated as empty XXDP\n", //
				"    If \"data.dsk\" exists, it is opened for read/write\n", //
				"    The image #1 may be oversized.\n", //
				"    Device 7:\n", //
				"    also oversized, contains the files in a sub directory \"shared.dir\" on the host.\n", //
				"    Can be accessed with \"DD0:\", \"DD1:\", \"DD7:\"\n",
				"\n", //
				PROGNAME " -xxdp --unpack 11XXDP.DSK 11xxdp.dir\n", //
				"    Extracts all files from an image into a directory.\n", //
				"    If image is bootable, pseudofiles for bootloader and monitor are generated.\n", //
				"    The directory is then bootable too.\n", //
				"\n", //
				PROGNAME " <serial params> -xxdp -sd 0 w 11xxdp.dir\n", //
				"    Define device #0:\n", //
				"    Standard 256kb image, loaded from file \"11xxdp.dir\", not created.\n", //
				"    Dir is bootable, if it contains the pseudo files for monitor and boot block.\n", //
				"    A bootable dir is can be created by unpacking an bootable image file.\n", //
				"\n", //
				NULL };

void help() {
	char **s;
	fprintf(ferr, "%s\n", version);
	fprintf(ferr, "%s\n", copyright);

	fprintf(ferr, "\n");
	fprintf(ferr, "Online at www.retrocmp.com/tools/tu58fs \n");
	fprintf(ferr, "\n");
	fprintf(ferr, "Command line summary:\n\n");
	// getop must be intialized to print the syntax
	getopt_help(&getopt_parser, stdout, arg_menu_linewidth, 10, PROGNAME);

	fprintf(ferr, "\n");
	fprintf(ferr, "Some examples:\n");
	fprintf(ferr, "\n");
	for (s = examples; *s; s++)
		fputs(*s, ferr);

	exit(1);
}

// show error for one option
static void commandline_error() {
	fprintf(ferr, "Error while parsing commandline:\n");
	fprintf(ferr, "  %s\n", getopt_parser.curerrortext);
	exit(1);
}

// parameter wrong for currently parsed option
static void commandline_option_error() {
	fprintf(ferr, "Error while parsing commandline option:\n");
	fprintf(ferr, "  %s\nSyntax:  ", getopt_parser.curerrortext);
	getopt_help_option(&getopt_parser, stdout, 96, 10);
	exit(1);
}

/*
 * read commandline parameters into global "param_" vars
 * result: 0 = OK, 1 = error
 */
static void parse_commandline(int argc, char **argv) {
	char buff[256];
	int res;
	int cur_autosizing = 0;
	dec_filesystem_t cur_filesystem = fsnone;

	// define commandline syntax
	getopt_init(&getopt_parser, /*ignore_case*/1);

//	getopt_def(&getopt_parser, NULL, NULL, "hostname", NULL, NULL, "Connect to the Blinkenlight API server on <hostname>\n"
//		"<hostname> may be numerical or ar DNS name",
//		"127.0.0.1", "connect to the server running on the same machine.",
//		"raspberrypi", "connected to a RaspberryPi with default name.");

	// !!!1 Do not define any defaults... else these will be set very time!!!

	// Commandline compatible with Don North tu58em
	getopt_parser.ignore_case = 0; // V != v
	getopt_def(&getopt_parser, "?", "help", NULL, NULL, NULL, "Print help.",
	NULL, NULL, NULL, NULL);
	getopt_def(&getopt_parser, "V", "version", NULL, NULL, NULL, "Output version string.",
	NULL, NULL, NULL, NULL);
	getopt_def(&getopt_parser, "dbg", "debug", NULL, NULL, NULL,
			"Enable debug output to console.",
			NULL, NULL, NULL, NULL);
	getopt_def(&getopt_parser, "v", "verbose", NULL, NULL, NULL,
			"Enable verbose output to terminal.",
			NULL, NULL, NULL, NULL);
	getopt_def(&getopt_parser, "m", "mrsp", NULL, NULL, NULL,
			"Disable sending INIT at initial startup.",
			NULL, NULL, NULL, NULL);
	getopt_def(&getopt_parser, "n", "nosync", NULL, NULL, NULL,
			"enable standard MRSP mode (byte-level handshake).",
			NULL, NULL, NULL, NULL);
	getopt_def(&getopt_parser, "x", "vax", NULL, NULL, NULL,
			"Remove delays for aggressive timeouts of VAX console.",
			NULL, NULL, NULL, NULL);
	getopt_def(&getopt_parser, "bk", "background", NULL, NULL, NULL,
			"Run in background mode, no console I/O except errors.",
			NULL, NULL, NULL, NULL);
	getopt_def(&getopt_parser, "t", "timing", "parameter", NULL, NULL,
			"timing 1: add timing delays to spoof diagnostic into passing.\n"
					"timing 2: add timing delays to mimic a real TU58.",
			NULL, NULL, NULL, NULL);
	getopt_def(&getopt_parser, "b", "baudrate", "baudrate", NULL, "38400",
			"Set serial line speed to 1200..3000000 baud.",
			NULL, NULL, NULL, NULL);
	getopt_def(&getopt_parser, "sb", "stopbits", "count", NULL, "1", "Set 1 or 2 stop bits.",
	NULL, NULL, NULL, NULL);
	getopt_def(&getopt_parser, "p", "port", "device", NULL, "1",
			"Select serial port [1..N] or \"/dev/com\"N",
			NULL, NULL, NULL, NULL);

	getopt_def(&getopt_parser, "xx", "xxdp", NULL, NULL, NULL,
			"Select XXDP file system for following --device or --shareddevice options.\n"
					"New image files are create with an empty XXDP file system.",
			NULL, NULL, NULL, NULL);
	getopt_def(&getopt_parser, "rt", "rt11", NULL, NULL, NULL,
			"Same as --xxdp, but RT11 file system is selected.",
			NULL, NULL, NULL, NULL);

	getopt_def(&getopt_parser, "s", "size", "std_or_auto", NULL, NULL,
			"select standard 256KB image size, or auto sizing to max 32MB.\n"
					"Valid for following --device or --shareddevice options.\n"
					"When autosizing, the image is automatically enlarged on each block request\n"
					"beyond the currentimage end. Patched DD.SYS drivers are needed then for\n"
					"DEC operating systems.", "auto", "image is automatically enlarged.",
			NULL, NULL);

	getopt_def(&getopt_parser, "d", "device", "unit,read_write_create,filename",
	NULL, NULL,
			"Open image file for a TU58 drive\n"
					"<unit>: File is mounted in this drive (0..7).\n"
					"<read_write_create>: \"r\" = device is read-only, \"w\" = writable,\n"
					"\"c\" = writable and file is created if not existant.\n"
					"Also the --xxdp, --rt11 and --size options are evaluated.\n"
					"A missing file is created and initialized with 0s or empty XXDP or RT11 filesystem.",
			"0 r 11XXDP.DSK", "mount image file XXDP.DSK into slot #0.",
			NULL, NULL);
	getopt_def(&getopt_parser, "sd", "shareddevice", "unit,read_write_create,directory",
	NULL, NULL, "same as --device, but image is filled with files from a host directory.\n"
			"-xxdp or -rt11 must be specified, <directory> must exist.",
			"1 w /home/user/tu58/data.dir", "fill image with files in a directory.",
			NULL, NULL);

	getopt_def(&getopt_parser, "st", "synctimeout", "seconds", NULL, "5",
			"An image changed by PDP is written to disk after this idle period.",
			NULL, NULL, NULL, NULL);

	getopt_def(&getopt_parser, "ot", "offlinetimeout", "seconds", NULL, "5",
			"By hitting a number-key 0..7, the device goes offline for user control.\n"
					"PDP traffic shall not be interupted,  so Offline state is entered delayed\n"
					"after RS232 port is silent for this period.",
			NULL, NULL, NULL, NULL);

	getopt_def(&getopt_parser, "up", "unpack", "filename,dirname", NULL, NULL,
			"Read a binary disk/tape image, and extract files into directory\n"
					"A filesystem type must be specified (like -xxdp)",
			NULL, NULL, NULL, NULL);
	getopt_def(&getopt_parser, "pk", "pack", "dirname,filename", NULL, NULL,
			"Read files from a directory and pack into binary disk/tape image\n"
					"A filesystem type must be specified (like -xxdp)",
			NULL, NULL, NULL, NULL);

	// test options
	getopt_def(&getopt_parser, "testfs", "testfs", "filename", NULL, NULL,
			"Read an image, convert it to filesystem and test",
			NULL, NULL, NULL, NULL);

	if (argc < 2)
		help(); // at least 1 required

	image_count = 0;

	res = getopt_first(&getopt_parser, argc, argv);
	while (res > 0) {
		if (getopt_isoption(&getopt_parser, "help")) {
			help();
		} else if (getopt_isoption(&getopt_parser, "version")) {
			info(version);
		} else if (getopt_isoption(&getopt_parser, "debug")) {
			opt_debug = 1;
		} else if (getopt_isoption(&getopt_parser, "verbose")) {
			opt_verbose = 1;
		} else if (getopt_isoption(&getopt_parser, "mrsp")) {
			opt_mrspen = 1;
		} else if (getopt_isoption(&getopt_parser, "nosync")) {
			opt_nosync = 1;
		} else if (getopt_isoption(&getopt_parser, "vax")) {
			opt_vax = 1;
		} else if (getopt_isoption(&getopt_parser, "synctimeout")) {
			if (getopt_arg_i(&getopt_parser, "seconds", &opt_synctimeout_sec) < 0)
				commandline_option_error();
		} else if (getopt_isoption(&getopt_parser, "offlinetimeout")) {
			if (getopt_arg_i(&getopt_parser, "seconds", &opt_offlinetimeout_sec) < 0)
				commandline_option_error();
		} else if (getopt_isoption(&getopt_parser, "background")) {
			opt_background = 1;
		} else if (getopt_isoption(&getopt_parser, "timing")) {
			if (getopt_arg_i(&getopt_parser, "parameter", &opt_timing) < 0)
				commandline_option_error();
			if (opt_timing > 2)
				commandline_option_error();
		} else if (getopt_isoption(&getopt_parser, "baudrate")) {
			if (getopt_arg_i(&getopt_parser, "baudrate", &opt_speed) < 0)
				commandline_option_error();
		} else if (getopt_isoption(&getopt_parser, "stopbits")) {
			if (getopt_arg_i(&getopt_parser, "count", &opt_stopbits) < 0)
				commandline_option_error();
			if (opt_stopbits > 2)
				commandline_option_error();
		} else if (getopt_isoption(&getopt_parser, "port")) {
			if (getopt_arg_s(&getopt_parser, "device", opt_port, sizeof(opt_port)) < 0)
				commandline_option_error();
		} else if (getopt_isoption(&getopt_parser, "xxdp")) {
			cur_filesystem = fsxxdp;
		} else if (getopt_isoption(&getopt_parser, "rt11")) {
			cur_filesystem = fsrt11;
		} else if (getopt_isoption(&getopt_parser, "size")) {
			char buff[256];
			if (getopt_arg_s(&getopt_parser, "std_or_auto", buff, sizeof(buff)) < 0)
				commandline_option_error();
			if (toupper(buff[0]) == 'S')
				cur_autosizing = 0;
			else if (toupper(buff[0]) == 'A')
				cur_autosizing = 1;
			else
				commandline_option_error();

		} else if (getopt_isoption(&getopt_parser, "device")
				|| getopt_isoption(&getopt_parser, "shareddevice")) {
			int shared = getopt_isoption(&getopt_parser, "shareddevice");
			int unit;
			int readonly;
			int allowcreate;
			char pathbuff[4096];
			if (getopt_arg_i(&getopt_parser, "unit", &unit) < 0)
				commandline_option_error();
			if (unit < 0 || unit >= TU58_DEVICECOUNT)
				commandline_option_error();

			if (getopt_arg_s(&getopt_parser, "read_write_create", pathbuff, sizeof(pathbuff))
					< 0)
				commandline_option_error();
			if (toupper(pathbuff[0]) == 'R') {
				readonly = 1;
				allowcreate = 0;
			} else if (toupper(pathbuff[0]) == 'W') {
				readonly = 0;
				allowcreate = 0;
			} else if (toupper(pathbuff[0]) == 'C') {
				readonly = 0;
				allowcreate = 1;
			} else
				commandline_option_error();
			if (shared) {
				if (getopt_arg_s(&getopt_parser, "directory", pathbuff, sizeof(pathbuff)) < 0)
					commandline_option_error();
				if (cur_filesystem == fsnone) {
					strcpy(getopt_parser.curerrortext, "no filesystem specified");
					commandline_option_error();
				}
			} else {
				if (getopt_arg_s(&getopt_parser, "filename", pathbuff, sizeof(pathbuff)) < 0)
					commandline_option_error();
			}

			if (image_open(tu58image_get(unit), shared, readonly, allowcreate, pathbuff,
					devTU58, cur_filesystem, cur_autosizing) < 0)
				commandline_option_error();
			image_info(tu58image_get(unit));
			image_count++;
		} else if (getopt_isoption(&getopt_parser, "unpack")) {
			char filename[4096];
			char dirname[4096];
			image_t img;
			xxdp_filesystem_t *pdp_fs;
			hostdir_t *hostdir;
			if (getopt_arg_s(&getopt_parser, "filename", filename, sizeof(filename)) < 0)
				commandline_option_error();
			if (getopt_arg_s(&getopt_parser, "dirname", dirname, sizeof(dirname)) < 0)
				commandline_option_error();
			if (cur_filesystem == fsnone)
				commandline_option_error();
			// read binary
			image_init(&img);
			// autosizing, so no device info needed
			if (image_open(&img, 0, 0, 0, filename, devTU58, cur_filesystem, 1))
				fatal("image_open failed");

			pdp_fs = xxdp_filesystem_create(devTU58, &img.data, &img.data_size,
					img.changedblocks, 0);
			if (xxdp_filesystem_parse(pdp_fs))
				fatal("xxdp_filesystem_parse failed");
			hostdir = hostdir_create(dirname, pdp_fs);
			if (hostdir_prepare(hostdir, 1, 1, NULL))
				fatal("hostdir_prepare failed");
			hostdir_from_pdp_fs(hostdir);
			xxdp_filesystem_print_dir(pdp_fs, ferr);
			info("Files extracted from \"%s\" and written to \"%s\".", filename, dirname);
			xxdp_filesystem_destroy(pdp_fs);
			hostdir_destroy(hostdir);
			image_close(&img);

		} else if (getopt_isoption(&getopt_parser, "pack")) {
			char filename[4096];
			char dirname[4096];
			xxdp_filesystem_t *pdp_fs;
			hostdir_t *hostdir;
			uint8_t *data = NULL; // buffer
			dbgdata = NULL;
			dbgdatasize = 0;
			unsigned data_size = 0;
			if (getopt_arg_s(&getopt_parser, "filename", filename, sizeof(filename)) < 0)
				commandline_option_error();
			if (getopt_arg_s(&getopt_parser, "dirname", dirname, sizeof(dirname)) < 0)
				commandline_option_error();
			if (cur_filesystem != fsxxdp)
				commandline_option_error();

			pdp_fs = xxdp_filesystem_create(devTU58, &dbgdata, &dbgdatasize, NULL, 1);
			hostdir = hostdir_create(dirname, pdp_fs);
			if (hostdir_prepare(hostdir, 0, 0, NULL)) // check
				fatal("hostdir_prepare failed");
			if (hostdir_to_pdp_fs(hostdir))
				fatal("hostdir_to_xxdp failed");
			if (xxdp_filesystem_render(pdp_fs))
				fatal("xxdp_filesystem_render failed");
			xxdp_filesystem_print_dir(pdp_fs, ferr);
			{
				FILE *f = fopen(filename, "w+");
				if (!f)
					fatal("opening file %s failed", filename);
				fwrite(dbgdata, 1, dbgdatasize, f);
				fclose(f);
			}
			xxdp_filesystem_destroy(pdp_fs);
			hostdir_destroy(hostdir);

		} else if (getopt_isoption(&getopt_parser, "testfs")) {
			char pathbuff[4096];
			if (getopt_arg_s(&getopt_parser, "filename", pathbuff, sizeof(pathbuff)) < 0)
				commandline_option_error();
			image_t *img = tu58image_get(7);
			xxdp_filesystem_t *pdp_fs;
			// use unit 7 to test
			if (getopt_arg_s(&getopt_parser, "filename", pathbuff, sizeof(pathbuff)) < 0)
				commandline_option_error();
			// open fix size, read/write, create not allowed, single file
			//if (image_open(7, 0, 0, 0, buff, fsxxdp, 0) < 0)
			if (image_open(img, 0, 0, 0, pathbuff, devTU58, fsxxdp, 1) < 0)
				commandline_option_error();
			pdp_fs = xxdp_filesystem_create(devRL02, &(img->data), &(img->data_size), NULL,
					cur_autosizing);
			// xxdp_filesystem_create(&fs, devTU58, &(img->data), &(img->size));
			xxdp_filesystem_parse(pdp_fs);

			xxdp_filesystem_print_dir(pdp_fs, ferr);
			{
				FILE *f = fopen("blocks.log", "w+");
				if (!f)
					perror("F?!");
				xxdp_filesystem_print_blocks(pdp_fs, f);
				fclose(f);
			}

//			hostdir_prepare("hostdir.tst", 1);
//			hostdir_from_xxdp("hostdir.tst", &fs);

			// rewrite image from logical filesystem
			xxdp_filesystem_render(pdp_fs);

			// dump as image
			{
				FILE *f = fopen("tst_render.img", "w+");
				if (!f)
					perror("F?!");
				fwrite(img->data, 1, img->data_size, f);
				fclose(f);
			}

			image_close(img);
			xxdp_filesystem_destroy(pdp_fs);
			// re-parse the produced image

			if (image_open(img, 0, 0, 0, "tst_render.img", devTU58, fsxxdp, 1) < 0)
				fatal("Can not open just rendered image");
			// parse 2nd time
			pdp_fs = xxdp_filesystem_create(devRL02, &(img->data), &(img->data_size), NULL,
					cur_autosizing);
			xxdp_filesystem_parse(pdp_fs);
			xxdp_filesystem_print_dir(pdp_fs, ferr);
			{
				FILE *f = fopen("tst_render.log", "w+");
				if (!f)
					perror("F?!");
				xxdp_filesystem_print_blocks(pdp_fs, f);
				fclose(f);
			}

			xxdp_filesystem_destroy(pdp_fs);
			exit(0);
		}

		res = getopt_next(&getopt_parser);
	}
	if (res == GETOPT_STATUS_MINARGCOUNT || res == GETOPT_STATUS_MAXARGCOUNT)
		// known option, but wrong number of arguments
		commandline_option_error();
	else if (res < 0)
		commandline_error();
}

static pthread_t th_run;	// emulator thread id
static pthread_t th_monitor;	// monitor thread id

// user wants to set a device offline for work
static void device_dialog(image_t *img) {
	char *tokens[256];
	char *fpath;
	int ready = 0;
	int n;

	tu58_offline_request = 1;
	info("TU58 goes offline after %d seconds of RS232 inactivity ...", opt_offlinetimeout_sec);
	while (!tu58_offline)
		delay_ms(100);
	info("TU58 now offline: \"all cartridges removed\".");

	while (!ready) {
		fprintf(ferr, "Choices for device %d%s :\n", img->unit,
				img->changed ? " (unsaved changes)" : "");
		fprintf(ferr, "S - save image to disk%s\n", img->changed ? "" : " (though unchanged)");
		if (img->shared)
			fprintf(ferr, "L <file>- reload image from dir \"%s\"\n", img->host_fpath);
		else
			fprintf(ferr, "L - load other image file (now \"%s\")\n", img->host_fpath);
		fprintf(ferr, "C - continue server with all drives online (cartridges inserted).\n");
		fprintf(ferr, ".	");

		do {
			n = inputline(tokens, 256);
			delay_ms(100);
		} while (n == 0);

		switch (toupper(tokens[0][0])) {
		case 'S':
			image_save(img);
			break;
		case 'L':
			if (n != 2) {
				error("Syntax: \"L filename\"");
				break;
			}
			fpath = tokens[1];
			if (img->shared)
				;
			else {
				image_close(img);
				// re-open with all paramas same, only other file
				if (image_open(img, img->shared, img->readonly, 0, fpath, img->dec_device,
						img->dec_filesystem, img->autosizing) < 0)
					error("Opening file \"%s\" failed", fpath);
				else
					info("Opened file \"%s\"", fpath);
			}
			break;
		case 'C':
			ready = 1;
			break;

		}
	}
	// go online
	tu58_offline_request = 0;
}

//
// start tu58 drive emulation
//
void run(void) {
	// a sanity check for blocksize definition
	if (TU58_BLOCKSIZE % TU_DATA_LEN != 0)
		fatal("illegal BLOCKSIZE (%d) / TU_DATA_LEN (%d) ratio", TU58_BLOCKSIZE,
		TU_DATA_LEN);

	// say hello
	info("TU58 emulation start");
	info(
			"0-7 device dialog, R restart, S toggle send init, V toggle verbose, D toggle debug, Q quit");

	// run the emulator
	if (pthread_create(&th_run, NULL, tu58_server, NULL))
		error("unable to create emulation thread");

	// run the monitor
	if (pthread_create(&th_monitor, NULL, tu58_monitor, NULL))
		error("unable to create monitor thread");

	// loop on user input
	for (;;) {
		uint8_t c;

		// get char from stdin (if available)
		if ((c = toupper(conget())) > 0) {
			if (c >= '0' && c <= '7') {
				image_t *img = NULL;
				int unit = c - '0';
				// number of open device?
				if (IMAGE_UNIT_VALID(unit))
					img = tu58image_get(unit);
				if (img && img->open)
					device_dialog(img);
			}
			if (c == 'V') {
				// toggle verbosity
				opt_verbose ^= 1;
				opt_debug = 0;
				info("verbosity set to %s; debug %s", opt_verbose ? "ON" : "OFF",
						opt_debug ? "ON" : "OFF");
			} else if (c == 'D') {
				// toggle debug
				opt_verbose = 1;
				opt_debug ^= 1;
				info("verbosity set to %s; debug %s", opt_verbose ? "ON" : "OFF",
						opt_debug ? "ON" : "OFF");
			} else if (c == 'S') {
				// toggle sending init string
				tu58_doinit = (tu58_doinit + 1) % 2;
				if (opt_debug)
					fprintf(ferr, "\n");
				info("send of <INIT> %sabled", tu58_doinit ? "en" : "dis");
			} else if (c == 'R') {
				// kill and restart the emulator
				if (pthread_cancel(th_run))
					error("unable to cancel emulation thread");
				if (pthread_join(th_run, NULL))
					error("unable to join on emulation thread");
				if (pthread_create(&th_run, NULL, tu58_server, NULL))
					error("unable to restart emulation thread");
			} else if (c == 'Q') {
				// kill the emulator and exit
				if (pthread_cancel(th_monitor))
					error("unable to cancel monitor thread");
				if (pthread_cancel(th_run))
					error("unable to cancel emulation thread");
				break;
			}
		}

		// wait a bit
		delay_ms(25);

	} // for (;;)

	// wait for emulator to finish
	if (pthread_join(th_run, NULL))
		error("unable to join on emulation thread");

	// all done
	info("TU58 emulation end");
	return;
}

int main(int argc, char *argv[]) {
	int i, n;

	ferr = stdout; // ferr in Eclipse console not visible?

	// init file structures
	tu58images_init();

	parse_commandline(argc, argv);
	// returns only if everything is OK
	// Std options already executed

	// some debug info
	if (opt_debug) {
		info(version);
		info(copyright);
	}

	// must have opened at least one unit
	if (image_count == 0) {
		info("No units were specified, emulator not started.");
		exit(0);
	}

	// give some info
	info("serial port %s at %d baud %d stop", opt_port, opt_speed, opt_stopbits);
	if (opt_mrspen)
		info("MRSP mode enabled (NOT fully tested - use with caution)");

	// setup serial and console ports
	devinit(opt_port, opt_speed, opt_stopbits);
	coninit();

	// start thread with tu58 emulator
	run();

	// restore serial and console ports
	conrestore();
	devrestore();

	// write back unsaved files and close
	tu58images_closeall();
}
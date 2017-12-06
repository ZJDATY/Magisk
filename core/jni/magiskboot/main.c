#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "magiskboot.h"
#include "utils.h"
#include "sha1.h"

/********************
  Patch Boot Image
*********************/

static void usage(char *arg0) {
	fprintf(stderr,
		"Usage: %s <action> [args...]\n"
		"\n"
		"Supported actions:\n"
		" --parse <bootimg>\n"
		"  Parse <bootimg> only, do not unpack. Return value: \n"
		"    0:OK   1:error   2:insufficient boot partition size\n"
		"    3:chromeos   4:ELF32   5:ELF64\n"
		"\n"
		" --unpack <bootimg>\n"
		"  Unpack <bootimg> to kernel, ramdisk.cpio, (second), (dtb), (extra) into\n"
		"  the current directory. Return value is the same as parse command\n"
		"\n"
		" --repack <origbootimg> [outbootimg]\n"
		"  Repack kernel, ramdisk.cpio[.ext], second, dtb... from current directory\n"
		"  to [outbootimg], or new-boot.img if not specified.\n"
		"  It will compress ramdisk.cpio with the same method used in <origbootimg>,\n"
		"  or attempt to find ramdisk.cpio.[ext], and repack directly with the\n"
		"  compressed ramdisk file\n"
		"\n"
		" --hexpatch <file> <hexpattern1> <hexpattern2>\n"
		"  Search <hexpattern1> in <file>, and replace with <hexpattern2>\n"
		"\n"
		" --cpio-<cmd> <incpio> [flags...] [args...]\n"
		"  Do cpio related cmds to <incpio> (modifications are done directly)\n"
		"  Supported commands:\n"
		"    -rm [-r] <entry>\n"
		"      Remove entry from <incpio>, flag [-r] to remove recursively\n"
		"    -mkdir <mode> <entry>\n"
		"      Create directory as an <entry>\n"
		"    -ln <target> <entry>\n"
		"      Create symlink <entry> to point to <target>\n"
		"    -mv <from-entry> <to-entry>\n"
		"      Move <from-entry> to <to-entry>\n"
		"    -add <mode> <entry> <infile>\n"
		"      Add <infile> as an <entry>; replaces <entry> if already exists\n"
		"    -extract [<entry> <outfile>]\n"
		"      Extract <entry> to <outfile>, or extract all to current directory\n"
		"    -test\n"
		"      Return value: 0/stock 1/Magisk 2/other (phh, SuperSU, Xposed)\n"
		"    -patch <KEEPVERITY> <KEEPFORCEENCRYPT>\n"
		"      Patch cpio for Magisk. KEEP**** are boolean values\n"
		"    -backup <origcpio> <HIGH_COMP> [SHA1]\n"
		"      Create ramdisk backups into <incpio> from <origcpio>\n"
		"      HIGH_COMP is a boolean value, toggles high compression mode\n"
		"      SHA1 of stock boot image is optional\n"
		"    -restore\n"
		"      Restore ramdisk from ramdisk backup within <incpio>\n"
		"    -stocksha1\n"
		"      Get stock boot SHA1 recorded within <incpio>\n"
		"\n"
		" --dtb-<cmd> <dtb>\n"
		"  Do dtb related cmds to <dtb> (modifications are done directly)\n"
		"  Supported commands:\n"
		"    -dump\n"
		"      Dump all contents from dtb for debugging\n"
		"    -test\n"
		"      Check if fstab has verity/avb flags\n"
		"      Return value: 0/no flags 1/flag exists"
		"    -patch\n"
		"      Search for fstab and remove verity/avb\n"
		"\n"
		" --compress[=method] <infile> [outfile]\n"
		"  Compress <infile> with [method] (default: gzip), optionally to [outfile]\n"
		"  <infile>/[outfile] can be '-' to be STDIN/STDOUT\n"
		"  Supported methods: "
	, arg0);
	for (int i = 0; SUP_LIST[i]; ++i)
		fprintf(stderr, "%s ", SUP_LIST[i]);
	fprintf(stderr,
		"\n\n"
		" --decompress <infile> [outfile]\n"
		"  Detect method and decompress <infile>, optionally to [outfile]\n"
		"  <infile>/[outfile] can be '-' to be STDIN/STDOUT\n"
		"  Supported methods: ");
	for (int i = 0; SUP_LIST[i]; ++i)
		fprintf(stderr, "%s ", SUP_LIST[i]);
	fprintf(stderr,
		"\n\n"
		" --sha1 <file>\n"
		"  Print the SHA1 checksum for <file>\n"
		"\n"
		" --cleanup\n"
		"  Cleanup the current working directory\n"
		"\n");

	exit(1);
}

int main(int argc, char *argv[]) {
	fprintf(stderr, "MagiskBoot v" xstr(MAGISK_VERSION) "(" xstr(MAGISK_VER_CODE) ") (by topjohnwu) - Boot Image Modification Tool\n\n");

	umask(0);
	if (argc > 1 && strcmp(argv[1], "--cleanup") == 0) {
		fprintf(stderr, "Cleaning up...\n\n");
		char name[PATH_MAX];
		unlink(KERNEL_FILE);
		unlink(RAMDISK_FILE);
		unlink(RAMDISK_FILE ".raw");
		unlink(SECOND_FILE);
		unlink(DTB_FILE);
		unlink(EXTRA_FILE);
		for (int i = 0; SUP_EXT_LIST[i]; ++i) {
			sprintf(name, "%s.%s", RAMDISK_FILE, SUP_EXT_LIST[i]);
			unlink(name);
		}
	} else if (argc > 2 && strcmp(argv[1], "--sha1") == 0) {
		char sha1[21], *buf;
		size_t size;
		mmap_ro(argv[2], (void **) &buf, &size);
		SHA1(sha1, buf, size);
		for (int i = 0; i < 20; ++i)
			printf("%02x", sha1[i]);
		printf("\n");
		munmap(buf, size);
	} else if (argc > 2 && strcmp(argv[1], "--parse") == 0) {
		boot_img boot;
		exit(parse_img(argv[2], &boot));
	} else if (argc > 2 && strcmp(argv[1], "--unpack") == 0) {
		unpack(argv[2]);
	} else if (argc > 2 && strcmp(argv[1], "--repack") == 0) {
		repack(argv[2], argc > 3 ? argv[3] : NEW_BOOT);
	} else if (argc > 2 && strcmp(argv[1], "--decompress") == 0) {
		decomp_file(argv[2], argc > 3 ? argv[3] : NULL);
	} else if (argc > 2 && strncmp(argv[1], "--compress", 10) == 0) {
		char *method;
		method = strchr(argv[1], '=');
		if (method == NULL) method = "gzip";
		else method++;
		comp_file(method, argv[2], argc > 3 ? argv[3] : NULL);
	} else if (argc > 4 && strcmp(argv[1], "--hexpatch") == 0) {
		hexpatch(argv[2], argv[3], argv[4]);
	} else if (argc > 2 && strncmp(argv[1], "--cpio", 6) == 0) {
		char *cmd = argv[1] + 6;
		if (*cmd == '\0') usage(argv[0]);
		else ++cmd;
		if (cpio_commands(cmd, argc - 2, argv + 2)) usage(argv[0]);
	} else if (argc > 2 && strncmp(argv[1], "--dtb", 5) == 0) {
		char *cmd = argv[1] + 5;
		if (*cmd == '\0') usage(argv[0]);
		else ++cmd;
		if (dtb_commands(cmd, argc - 2, argv + 2)) usage(argv[0]);
	} else {
		usage(argv[0]);
	}

	return 0;
}

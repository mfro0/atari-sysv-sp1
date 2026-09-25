/*
 * atwstart LO HI SECONDS - set the ATW800/2's display start registers
 * (VTG 12 = low word, 13 = high word) for SECONDS, then back to 0. For
 * measuring what unit the start counts in: nothing else is touched, so
 * the X server's picture simply shows from a different place in video
 * memory, and comes back. The display is switched off around the change
 * and back on with X's control value (0x19, 8 bpp), as a mode set does.
 *
 * Writes the VTG in both layouts' places: in the 2 MB layout the upper
 * one is a mirror of the same registers, in the 4 MB layout the lower one
 * lands in video memory above any 8 bpp frame buffer X uses.
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

static volatile unsigned char *m;
#define R(v, n)	(*(volatile unsigned short *)(m + (v) + 2 * (n)))

static void start(unsigned lo, unsigned hi)
{
	R(0x1FF800UL, 0) = 0;			/* display off, as a mode set does */
	R(0x3FF800UL, 0) = 0;
	R(0x1FF800UL, 12) = lo; R(0x1FF800UL, 13) = hi;
	R(0x3FF800UL, 12) = lo; R(0x3FF800UL, 13) = hi;
	/* back on with the value X runs with, 8 bpp through the LUT */
	R(0x1FF800UL, 0) = 0x19;
	R(0x3FF800UL, 0) = 0x19;
}

int main(int argc, char **argv)
{
	int fd = open("/dev/mem", O_RDWR);

	if (argc != 4) {
		fprintf(stderr, "usage: atwstart LO HI SECONDS\n");
		return 2;
	}
	if (fd < 0) { perror("/dev/mem"); return 1; }
	m = (volatile unsigned char *)mmap(0, 0x400000, PROT_READ | PROT_WRITE,
					   MAP_SHARED, fd, (off_t)0xFEA00000UL);
	if ((long)m == -1) { perror("mmap 4 MB at FEA00000"); return 1; }
	start((unsigned)strtoul(argv[1], 0, 0), (unsigned)strtoul(argv[2], 0, 0));
	sleep((unsigned)atoi(argv[3]));
	start(0, 0);
	return 0;
}

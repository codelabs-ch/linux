#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>

#define SHMSTREAM_MARKER 0x487312b6b79a9b6d

int main(int argc, char *argv[])
{
	const char *filename = argv[1];
	int fd;
	uint64_t *ptr;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s file\n", argv[0]);
		exit(1);
	}

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		perror("open");
		exit(1);
	}

	ptr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	if (*ptr != SHMSTREAM_MARKER) {
		printf("Muen channel marker not found in file '%s'\n", filename);
		exit(1);
	}

	printf("Muen channel marker found in file '%s'\n", filename);
	return 0;
}

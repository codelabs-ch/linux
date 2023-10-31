#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>

#define error(...) do { fprintf(stderr, __VA_ARGS__); abort(); } while(0)
#define assert_int(SHOULD, IS) do { int is = (IS); int should = (SHOULD); if (is != should) error("Assertion failed: INT result %d <> expected %d at %s:%d\n", is, should, __FILE__, __LINE__); } while(0)
#define assert_true(IS) do { int is = (IS); if (! is) error("Assertion failed: boolean FALSE <> expected TRUE at %s:%d\n", __FILE__, __LINE__); } while(0)

enum file_type { READ_ONLY, READ_WRITE };

static inline void get_file_info(const char *filename, size_t *size,
		enum file_type *type)
{
	struct stat result;

	assert_int(0, stat(filename, &result));
	assert_true(S_ISREG(result.st_mode));
	assert_int(0, result.st_uid);
	assert_int(0, result.st_gid);

	*size = result.st_size;

	/* not very portable here */
	switch (result.st_mode & 0777) {
	case 0600:
		*type = READ_WRITE;
		break;
	case 0400:
		*type = READ_ONLY;
		break;
	default:
		error("Invalid mode for file %s encountered: %06o\n", filename,
				result.st_mode & 0777);
	}
}

static void buffer_read(int fd, char *buffer, size_t buffer_size,
		size_t file_size, enum file_type perm)
{
	size_t ret = 0;

	/* first rewind to ensure proper operation */
	assert_int(0, lseek(fd, 0, SEEK_SET));

	while (ret < file_size + 1) {
		ssize_t result = read(fd, buffer, buffer_size);
		if (result < 0) {
			error("Reading failed with error %s\n", strerror(errno));
		}
		if (result == 0)
			break;

		if (perm == READ_ONLY) {
			size_t i;
			for (i = 0; i < result; i++)
				if (buffer[i] != 0)
					error("Read check failed at offset %zu: %02x\n", result + i,
							buffer[i]);
		}
		ret += result;
	}
	if (ret > file_size)
		error("Read beyond the memory region end\n");
	if (ret < file_size)
		error("Premature EOF, only read %zu bytes instead of %zu\n", ret,
				file_size);
}

static void buffer_write(int fd, char *buffer, size_t buffer_size, size_t file_size)
{
	size_t ret = 0;

	long seed = random();

	/* first rewind to ensure proper operation */
	assert_int(0, lseek(fd, 0, SEEK_SET));

	srandom(seed & UINT_MAX);

	/* now start filling the file */
	while (ret < file_size) {
		size_t i;
		ssize_t result;
		size_t length = file_size - ret;
		if (length > buffer_size)
			length = buffer_size;

		for (i = 0; i < length; i++)
			buffer[i] = random() & UCHAR_MAX;
		result = write(fd, buffer, length);
		if (result < 0)
			error("Writing failed with error %s\n", strerror(errno));

		if (result < length)
			error("Short write, wrote only %zd bytes\n", result);
		ret += result;
	}

	/* first rewind to ensure proper operation */
	assert_int(0, lseek(fd, 0, SEEK_SET));

	srandom(seed & UINT_MAX);
	/* now read and compare */
	ret = 0;
	while (ret < file_size) {
		size_t i;
		ssize_t result;
		result = read(fd, buffer, buffer_size);
		if (result == 0)
			break;
		if (result < 0)
			error("Reading failed with error %s\n", strerror(errno));
		for (i = 0; i < result; i++) {
			int ra = random() & UCHAR_MAX;
			if (((unsigned char *)buffer)[i] != ra)
				error("Compare failed at offset %zu\n", ret + i);
		}
		ret += result;
	}
	if (ret < file_size)
		error("Short read in buffer_write, read only %zu instead of %zu\n",
				ret, file_size);
}

static void test_write_eof(int fd, char *buffer, size_t buffer_size, size_t size)
{
	size_t pos;
	size_t success_length;

	if (size > 10) {
		pos = size - 10;
		success_length = 10;
	} else {
		pos = 0;
		success_length = size;
	}

	/* seek to that position */
	assert_int(pos, lseek(fd, pos, SEEK_SET));

	/* write the available data */
	assert_int(success_length, write(fd, buffer, buffer_size));

	/* next write should return error */
	assert_int(-1, write(fd, buffer, buffer_size));
	assert_int(ENOSPC, errno);

	/* a zero write should be okay */
	assert_int(0, write(fd, buffer, 0));
}

void test_ftruncate(int fd, size_t size, enum file_type type)
{
	struct stat stat_result;

	if (type == READ_WRITE) {
		/* expect the truncate to be successful */
		assert_int(0, ftruncate(fd, size));
	} else {
		/* truncate must fail */
		assert_int(-1, ftruncate(fd, size));
	}

	/* but it must not change the actual file size */
	assert_int(0, fstat(fd, &stat_result));
	assert_true(size == stat_result.st_size);
}

void test_truncate(const char *filename, size_t size, enum file_type type)
{
	struct stat stat_result;

	if (type == READ_WRITE) {
		/* expect the truncate to be successful */
		assert_int(0, truncate(filename, size));
	}
	/* truncate on r/o file will succeed for root user, so no
	 * assumption here */

	/* but it must not change the actual file size */
	assert_int(0, stat(filename, &stat_result));
	assert_true(size == stat_result.st_size);
}

static void test_mmap(int fd, int prot, size_t size)
{
	unsigned char *ptr;

	if (prot & PROT_WRITE == 0) {
		/* make sure that mmap fails here */
		assert_true(mmap(NULL, size, prot | PROT_WRITE, MAP_SHARED, fd, 0)
				== MAP_FAILED);
	}

	/* make sure that mapping with larger size fails */
	assert_true(mmap(NULL, size + 1, prot, MAP_SHARED, fd, 0) == (void*)-1);
	assert_true(mmap(NULL, size + 4096, prot, MAP_SHARED, fd, 0) == (void*)-1);

	/* now make a mapping that must succeed */
	ptr = mmap(NULL, size, prot, MAP_SHARED, fd, 0);
	assert_true(ptr != MAP_FAILED);

	if (prot & PROT_WRITE == 0) {
		/* pick a random position and read it, it should always be 0 */
		size_t i;
		for (i = 0; i < size * 5; i++) {
			size_t real_pos = (random() % size);
			unsigned char result = ptr[real_pos];
			if (result != 0)
				error("mmap read test failed at position %zu, got result %02x\n",
						real_pos, result);
		}
	}
	munmap(ptr, size);

	if (prot & PROT_WRITE) {
		unsigned char *reference;
		size_t i;

		ptr = mmap(NULL, size, prot, MAP_SHARED, fd, 0);
		/* fill reference with random data */
		reference = malloc(size);
		assert_true(reference != NULL);

		for (i = 0; i < size; i++) {
			unsigned char ra = (random() & UINT_MAX);
			reference[i] = ra;
		}

		/* use one large write to write the data */
		assert_int(0, lseek(fd, 0, SEEK_SET));
		assert_int(size, write(fd, reference, size));

		/* now check randomly using ptr */
		for (i = 0; i < size * 5; i++) {
			size_t real_pos = (random() % size);
			unsigned char result = ptr[real_pos];
			if (result != reference[real_pos])
				error("mmap read test failed at position %zu, got result %02x,"
						" expected %02x\n", real_pos, result,
						reference[real_pos]);
		}
		free(reference);
		munmap(ptr, size);
	}
}

static void test_file(const char *filename)
{
	size_t size;
	enum file_type perm;
	int prot;
	int mode;
	int fd;
	char buffer[8192];

	fprintf(stderr, "Processing file %s\n", filename);
	get_file_info(filename, &size, &perm);

	if (perm == READ_ONLY) {
		prot = PROT_READ;
		mode = O_RDONLY;
	} else {
		prot = PROT_READ | PROT_WRITE;
		mode = O_RDWR;
	}

	if (perm == READ_ONLY) {
		/* make sure that permission checking works */
		assert_int(-EPERM, open(filename, O_RDWR));
	}

	/* now open the file normally */
	fd = open(filename, mode);
	assert_true(fd >= 0);

	/* now read the whole file, optionally checking its contents and counting bytes */
	/* use different buffer sizes to check for possible corner cases */
	buffer_read(fd, buffer, 1234, size, perm);
	buffer_read(fd, buffer, 4095, size, perm);
	buffer_read(fd, buffer, 4096, size, perm);
	buffer_read(fd, buffer, 4097, size, perm);
	buffer_read(fd, buffer, 8192, size, perm);

	if (perm == READ_WRITE) {
		buffer_write(fd, buffer, 1234, size);
		buffer_write(fd, buffer, 4095, size);
		buffer_write(fd, buffer, 4096, size);
		buffer_write(fd, buffer, 4097, size);
		buffer_write(fd, buffer, 8192, size);
		test_write_eof(fd, buffer, sizeof(buffer), size);
	}
	test_ftruncate(fd, size, perm);
	test_mmap(fd, prot, size);
	close(fd);
	test_truncate(filename, size, perm);
	fprintf(stderr, "TEST of file %s: OK\n", filename);
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s file\n", argv[0]);
		exit(1);
	}

	test_file(argv[1]);
	return 0;
}

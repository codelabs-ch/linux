#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#define DATA_ARRAY_SIZE 2048

/*
 * Example message type
 * Corresponds to Foo.Message_Type record in Muen Example component.
 */
struct message_type {
	uint16_t size;
	char data[DATA_ARRAY_SIZE];
} __attribute__((packed));

/*
 * Open channel with given filename.
 *
 * Sets fd and returns a pointer to the mapped channel, MAP_FAILED on error.
 */
struct message_type * open_channel(const char* filename, bool writable, int *fd)
{
	const int mmap_rights = writable ? PROT_READ | PROT_WRITE : PROT_READ;
	void *channel;

	*fd = open(filename, writable ? O_RDWR : O_RDONLY);
	if (*fd == -1) {
		return MAP_FAILED;
	}

	channel = mmap(NULL, 4096, mmap_rights, MAP_SHARED, *fd, 0);
	if (channel == MAP_FAILED) {
		close(*fd);
	}
	return (struct message_type *)channel;
}

/*
 * Close channel.
 *
 * Unmaps the given channel and closes the associated file-descriptor.
 * Returns zero on Success.
 */
int close_channel(struct message_type * channel, int fd)
{
	int res;
	res = munmap((void *)channel, 4096);
	if (!res)
		return res;

	return close(fd);
}

/*
 * Initialize given message with randomized data.
 */
void init_rand_msg(struct message_type *msg)
{
	const uint16_t msg_size = rand() % DATA_ARRAY_SIZE;

	memset(msg, 0, sizeof(struct message_type));
	for (int i = 0; i <= msg_size; i++)
	{
		msg->data[i] = rand() % 256;
	}

	msg->size = msg_size;
}

/*
 * Trigger event by writing to given file.
 * The file is expected to be a muenevents pseudo-file.
 * Returns zero on success.
 */
int trigger_event(const char* filename)
{
	int fd;
	char buf[1] = { 0 };
	ssize_t count;

	fd = open(filename, O_RDWR);
	if (fd == -1)
		return -1;

	count = write(fd, buf, 1);
	close(fd);

	return count == 1 ? 0 : 1;
}


/*
 * Wait for response by polling on given file-descriptor for the specified
 * timeout in milliseconds.
 *
 * Returns zero on success.
 */
int wait_for_response(const int fd, int timeout)
{
	int ret;
	struct pollfd pfd;

	pfd.fd = fd;
	pfd.events = POLLIN;

	ret = poll(&pfd, (unsigned long)1, timeout);
	if (ret < 0)
		return ret;

	if (pfd.revents & (POLLERR|POLLHUP|POLLNVAL))
		return -1;

	if ((pfd.revents & POLLIN) == POLLIN)
		return 0;

	return -1;
}

int main(int argc, char *argv[])
{
	int retval;
	int req_fd, resp_fd;
	struct message_type *req_channel, *resp_channel;
	struct message_type ref_msg, resp_msg;

	if (argc != 4) {
		fprintf(stderr, "Usage: %s <request> <response> <event>\n", argv[0]);
		fprintf(stderr, "  request : filename of channel for request\n");
		fprintf(stderr, "  response: filename of channel for response\n");
		fprintf(stderr, "  event   : filename of event to signal pending request\n");
		exit(1);
	}

	const char *req_filename = argv[1];
	req_channel = open_channel(req_filename, true, &req_fd);
	if (req_channel == MAP_FAILED) {
		perror("open request channel");
		exit(1);
	}
	fprintf(stderr, "Using '%s' as request channel\n", req_filename);

	const char *resp_filename = argv[2];
	resp_channel = open_channel(resp_filename, false, &resp_fd);
	if (req_channel == MAP_FAILED) {
		close_channel(req_channel, req_fd);
		perror("open response channel");
		exit(1);
	}
	fprintf(stderr, "Using '%s' as response channel\n", resp_filename);

	const char *evt_filename = argv[3];
	fprintf(stderr, "Using '%s' to trigger event\n", evt_filename);

	// Seed random number generator
	srand(time(NULL));
	init_rand_msg(&ref_msg);
	fprintf(stdout, "Sending request with size %u\n", ref_msg.size);

	// 1. Write to the example_request channel
	memcpy(req_channel, &ref_msg, sizeof(struct message_type));

	// 2. Trigger the associated event via /muenevents
	// 3. Wait for a response via poll on associated entry in /muenfs
	if (!trigger_event(evt_filename) && !wait_for_response(resp_fd, 1000)) {
		fprintf(stdout, "Wakeup from poll with pending response\n");

		// 4. Read from example_response and assert that it is equal to the sent
		memcpy(&resp_msg, resp_channel, sizeof(struct message_type));

		const int result = memcmp(&resp_msg, &ref_msg,
				sizeof(struct message_type));
		if (!result) {
			fprintf(stdout, "SUCCESS: Response matches sent request\n");
			retval = 0;
		} else
			fprintf(stderr, "FAILURE: Response does not match sent request\n");
	} else {
		fprintf(stderr, "Error trigger event or polling for response\n");
		retval = 1;
	}

	close_channel(req_channel, req_fd);
	close_channel(resp_channel, resp_fd);
	return retval;
}

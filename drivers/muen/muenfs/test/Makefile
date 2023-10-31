all: muenfs-test muenfs-marker muenfs-example

%: %.o
	$(CC) -o $@ $^

install: muenfs-test muenfs-marker muenfs-example
	install -d $(DESTDIR)/usr/sbin
	install $^ $(DESTDIR)/usr/sbin

clean:
	rm -f *.o muenfs-test muenfs-marker muenfs-example

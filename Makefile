LDFLAGS=$(shell pkg-config fuse3 --libs)
CFLAGS=-Wall -g
CFLAGS+=$(shell pkg-config fuse3 --cflags)
JSONFS_IMAGE=$(shell pwd)/jsonfs_image

all: jsonfs

jsonfs: jsonfs.c
	gcc -Wall jsonfs.c $(CFLAGS) $(LDFLAGS) -o jsonfs

clean:
	rm -f jsonfs

mount: jsonfs
	./jsonfs -s -f -json=$(JSONFS_IMAGE) mnt

debug: jsonfs
	./jsonfs -s -f -d -json=$(JSONFS_IMAGE) mnt

gdb: jsonfs
	gdb jsonfs -tui

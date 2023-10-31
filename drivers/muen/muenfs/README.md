# Muenfs

## Introduction

The *muenfs* Linux kernel module implements a virtual file system that
facilitates user-space access to shared memory channels provided by the [Muen
Separation Kernel][1].

For each detected channel, a file of matching size and permission is shown in
the file system. A user-space application can use *stat(2)* calls to get the
permissions (rw or r/o) of the files and the size of the region. For accessing
file contents *read(2)*, *write(2)*, and *mmap(2)* operations are supported.

## Usage

    $ modprobe muenfs
    $ mkdir -p /muenfs
    $ mount -t muenfs none /muenfs

## Example Code

See the `test/muenfs-test.c` program for example code on how to map a channel
file and access its contents. The `test/muenfs-example.c` program illustrates
how to use *poll(2)* to wait for an incoming event.

## Authentication

The current authentication model is that the files are created with uid and gid
set to 0. Depending on the type of the region the files have permissions 0400
or 0600. No further capability checking is done by this module.

[1]: https://muen.sk

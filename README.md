# charloop

A pair of virtual character devices acting as a loop.

## Author

Christophe BLAESS 2019
https://www.blaess.fr/christophe/

## Description

This very simple module create a pair of virtual character devices
`/dev/charloop0` and `/dev/charloop1`.
What is written into `/dev/charloop0` is readable from
`/dev/charloop1` and vice versa.
The devices accept `select()` system call.

They look quite similar to a fifo (named pipe), but they are seen
as character devices, not as special files and it may be important
in some cases. A second difference: opening a device for writing
does NOT block until a reader opens the other side. There's an internal
buffer which size (default 16 kB) is configurable with the `buffer_size`
module parameter.

For example I use the devices to have a console access on the virtual serial
port of a Qemu session.

I run `quemu-system-arm` with the parameter `-serial /dev/charloop0`
and in another terminal I run `minicom -D /dev/charloop1`.

Another use of this project is during training sessions on kernel
drivers to show the `poll()` system call implementation.

## License

This software is distributed under the terms of the Gnu GPL v.2 license.

## Usage

```
$ ls
charloop.c  Makefile  README.md
$ make
[...]
$ ls
charloop.c  charloop.ko  charloop.mod.c  charloop.mod.o  charloop.o  Makefile  modules.order  Module.symvers  README.md
$ sudo insmod charloop.ko 
$ echo HELLO > /dev/charloop0
$ cat /dev/charloop1
HELLO
^C
$ echo HELLO > /dev/charloop1
$ cat /dev/charloop0
HELLO
^C
$
```

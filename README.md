[![Build Status](https://travis-ci.org/till-varoquaux/mmq.png?branch=master)](https://travis-ci.org/till-varoquaux/mmq)
Overview
========

`MMQ` is a disk backed FIFO queue for linux system. This library was built at OKcupid to handle datasets that were too large to fit in memory and needed to be accessed with low worst case latency (all operations are strictly O(1) or can be started in the background by the operating system).
The container leverages the operating system caching menachism instead of maintaining its own by memory mapping the file. The on disk representation is fully transactional and follows ACID semmantics.

Installing
==========

The project is autotooled. A simple `./autogen.sh && ./configure && make && make install` should do the trick.

Requirement
===========

This library requires linux (although porting it to any other POSIX compliant system should be easy), glibc, a c++11 compatible compiler and standard library. It has been tested on Ubuntu 13.04 and Debian Wheezy with gcc 4.7 and clang 3.4, you should pass the `--std=c++11` flag to the compiler in order to compile those files.

Limitations
===========

The files are not threadsafe. You should never have more than one process/thread writting to the same file at once. The library uses advisory locks to ensure this doesn't happen but they are not properly supported by all filesystems (eg FAT, NFS...).

On file-systems where advisory locks work the process writting to the file cannot sync values while there is another process reading the file. The data is still written to the disk but it can only be commited once the writting process is the only having the file open.

Error management
================

By default the library uses exceptions to signal errors but adding the line `#define MMQ_NOEXCEPT` will disable that. Errors will then be printed to stderr. All library errors are unix errors and aren't recoverable. Getting an error will close the file and leave it in a readable state with all the data written by the last succesful `sync` operation. You can test wether a file is closed by using the `closed` member function.


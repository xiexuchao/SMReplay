# TraceReplayer

## Building
make
sudo ./replay

## Trace File 
time(ms) lba(sectors) size(sectors) type(read:0,write:1)

## Config File
device name
trace file name
log file name

## Log File
time(us) lba(bytes) size(bytes) type(0/1) latency(us)

## Bug Notes
the right way to compile: gcc replay.c -lrt

error:	  aio.cc:(.text+0x156): undefined reference to `aio_read'

solution: add "-lrt" to link command. -lrt is necessary here. 

error:	  ‘O_DIRECT’ undeclared (first use in this function)

solution: add #define _GNU_SOURCE before #include <fcntl.h>

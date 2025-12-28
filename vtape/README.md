# vtape

A utility to write files in SIMH virtual tape format.

### Usage
vtape [_options_] [[-f] _filename_] ...

### Options
-h or -? - display usage message  
-n _recordsize_ - set tape record size (a.k.a. block size) for following records (default: 512)  
-f _filename_ - write _filename_ to standard output in SIMH virtual tape format (the -f may be omitted)  
-m - append a virtual file mark after the next file  
-M - append a virtual file mark before the next file (i.e. immediately)  
-p - pad next file to a multiple of the tape record size (i.e. pad last record)  
-v - display status information  
using - by itself writes standard input to standard output in SIMH virtual tape format (assumed if no files are specified)

### Examples
Create a SIMH virtual Unix v7 distribution tape from the seven files f0 through f6:
> $ vtape -v f0 -M f1 -M f2 -M f3 -M f4 -M -n 10240 f5 -M f6 -M -M >v7tape.img  
> write file f0 (16 512-byte records)  
> write file mark  
> write file f1 (14 512-byte records)  
> write file mark  
> write file f2 (1 512-byte record)  
> write file mark  
> write file f3 (22 512-byte records)  
> write file mark  
> write file f4 (22 512-byte records)  
> write file mark  
> write file f5 (202 10240-byte records)  
> write file mark  
> write file f6 (937 10240-byte records)  
> write file mark  
> write file mark

Create a SIMH virtual Unix v7 addenda tar tape:
> $ gzcat v7addenda.tar.gz | vtape -v -n 10240 -m -m >v7addenda.img  
> write from standard input (71 10240-byte records)  
> write file mark  
> write file mark

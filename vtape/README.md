# vtape / unvtape

Utilities to write files in SIMH virtual tape format or extract files from SIMH virtual tapes.

### vtape Usage
vtape [_options_] [[-f] _filename_] ...

### vtape Options
-h or -? - display usage message  
-n _recordsize_ - set tape record size (a.k.a. block size) for following records (default: 512)  
-f _filename_ - write _filename_ to standard output in SIMH virtual tape format (the -f may be omitted)  
-m - append a virtual file mark after the next file  
-M - append a virtual file mark before the next file (i.e. immediately)  
-t - append a virtual end-of-tape mark at the end of the output
-p - pad next file to a multiple of the tape record size (i.e. pad last record)  
-v - display status information  
using - by itself writes standard input to standard output in SIMH virtual tape format (assumed if no files are specified)  
use -- to disable default writing of standard input

### unvtape Usage
unvtape [_options_] [[-f] _filename_] ...

### unvtape Options
-h or -? - display usage message  
-S - summarize content without extracting  
-s _num_ - skip past _num_ file marks in input (default: 0)  
-n _recordsize_ - set a fixed tape record size (default: variable)  
-f _filename_ - extract a file from virtual tape _filename_ to standard output (the -f may be omitted)  
-p - pad short records in the extracted file  
-v - display status information

### Examples
Create a SIMH virtual Unix v7 distribution tape from the seven files f0 through f6:
> $ vtape -v f0 -M f1 -M f2 -M f3 -M f4 -M -n 10240 f5 -M f6 -M -M -t >v7tape.img  
> write from file f0 (16 512-byte records)  
> write file mark  
> write from file f1 (14 512-byte records)  
> write file mark  
> write from file f2 (1 512-byte record)  
> write file mark  
> write from file f3 (22 512-byte records)  
> write file mark  
> write from file f4 (22 512-byte records)  
> write file mark  
> write from file f5 (202 10240-byte records)  
> write file mark  
> write from file f6 (937 10240-byte records)  
> write file mark  
> write file mark  
> write end-of-tape mark

Create a SIMH virtual Unix v7 addenda tar tape:
> $ gzcat v7addenda.tar.gz | vtape -v -n 10240 -m -m -t >v7addenda.img  
> write from standard input (71 10240-byte records)  
> write file mark  
> write file mark  
> write end-of-tape mark

Append a file mark and a virtual end-of-tape mark to the end of a tape:
> $ vtape -v -- -m -t >>tape.img  
> write file mark  
> write end-of-tape mark

Display the record sizes of all files on a tape:
> $ unvtape -S v7tape.img  
> v7tape.img  
>  (16 512-byte records) (file mark)  
>  (14 512-byte records) (file mark)  
>  (1 512-byte record) (file mark)  
>  (22 512-byte records) (file mark)  
>  (22 512-byte records) (file mark)  
>  (202 10240-byte records) (file mark)  
>  (937 10240-byte records) (file mark)  
>  (file mark)  
>  (tape end mark)

Extract the sixth file from a tape (skip five files):
> $ unvtape -v -s 5 -f v7tape.img >file5  
> extract from file v7tape.img  
>  (16 512-byte records, skipped) (file mark)  
>  (14 512-byte records, skipped) (file mark)  
>  (1 512-byte record, skipped) (file mark)  
>  (22 512-byte records, skipped) (file mark)  
>  (22 512-byte records, skipped) (file mark)  
>  (202 10240-byte records) (file mark)  
> $ cmp f5 file5 && echo same  
> same

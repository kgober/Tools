# dt2

A DECtape II (TU58) tape manipulation program, written in C for Unix-like systems.

### Usage
dt2 [_options_] _command_ [_num_] ...

### Options
-f _device_ - set tty device TU58 is attached to (default: /dev/cua00)  
-s _speed_ - set tty baud rate (default: 38400)  
-m - enable MRSP  
-d - enable debug output to stderr

### Commands
init - initialize TU58 device  
drive|unit _unit_num_ - select current drive number (default: unit 0)  
boot [_unit_num_] - read boot block (default: current drive)  
rewind [_unit_num_] - rewind tape (default: current drive)  
status [_unit_num_] - report status (default: current drive)  
retension [_unit_num_] - retension tape (default: current drive)  
seek _block_num_ - select current block number (default: block 0)  
read [_count_] - read _count_ blocks from current drive to stdout (default: rest of tape)  
readv [_count_] - read _count_ blocks from current drive to stdout with reduced sensitivity (default: rest of tape)  
write [_count_] - write _count_ blocks from stdin to current drive (default: rest of tape)  
writev [_count_] - write and verify _count_ blocks from stdin to current drive (default: rest of tape)  
blocksize {128|512} - select current block size (default: 512) 
blockcount _count_ - set current tape capacity in blocks (default: 262144 divided by current block size)

### Examples
Initialize the TU58 device (attached to /dev/cua01 at 19200 baud), and retension the tape in unit 0:
> $ dt2 -f /dev/cua01 -s 19200 init retension

Dump the tape in unit 1 to a file:
> $ dt2 drive 1 read >_filename_

Write and verify the tape in unit 0 from a file:
> $ dt2 write <_filename_

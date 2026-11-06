@echo off 
set project_name=CH559USB
set xram_size=0x0800
set xram_loc=0x0600
set code_size=0xEFFF
set dfreq_sys=48000000

if not exist "config.h" echo //add your personal defines here > config.h

SDCC\bin\sdcc -c -V -mmcs51 --model-large --xram-size %xram_size% --xram-loc %xram_loc% --code-size %code_size% -I/ -DFREQ_SYS=%dfreq_sys%  main.c
SDCC\bin\sdcc -c -V -mmcs51 --model-large --xram-size %xram_size% --xram-loc %xram_loc% --code-size %code_size% -I/ -DFREQ_SYS=%dfreq_sys%  util.c
SDCC\bin\sdcc -c -V -mmcs51 --model-large --xram-size %xram_size% --xram-loc %xram_loc% --code-size %code_size% -I/ -DFREQ_SYS=%dfreq_sys%  USBHost.c
SDCC\bin\sdcc -c -V -mmcs51 --model-large --xram-size %xram_size% --xram-loc %xram_loc% --code-size %code_size% -I/ -DFREQ_SYS=%dfreq_sys%  uart.c

SDCC\bin\sdcc main.rel util.rel USBHost.rel uart.rel -V -mmcs51 --model-large --xram-size %xram_size% --xram-loc %xram_loc% --code-size %code_size% -I/ -DFREQ_SYS=%dfreq_sys%  -o %project_name%.ihx

SDCC\bin\packihx %project_name%.ihx > %project_name%.hex

SDCC\bin\hex2bin -c %project_name%.hex

del %project_name%.lk
del %project_name%.map
del %project_name%.mem
del %project_name%.ihx

del *.asm
del *.lst
del *.rel
del *.rst
del *.sym


Rem Flashes the bin file to the CH559 (libusb-win32 driver via Zadig, https://zadig.akeo.ie/).
Rem chflasher_fixed.py, not chflasher.exe: the original tool erases only 11 KB and
Rem silently corrupts the tail of any larger image on a re-flash.
python chflasher_fixed.py %project_name%.bin

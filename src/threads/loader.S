#include "threads/loader.h"

#### Kernel loader.

#### This code should be stored in the first sector of a hard disk.
#### When the BIOS runs, it loads this code at physical address
#### 0x7c00-0x7e00 (512 bytes) and jumps to the beginning of it,
#### in real mode.  The loader loads the kernel into memory and jumps
#### to its entry point, which is the start function in start.S.
####
#### The BIOS passes in the drive that the loader was read from as
#### DL, with floppy drives numbered 0x00, 0x01, ... and hard drives
#### numbered 0x80, 0x81, ...  We want to support booting a kernel on
#### a different drive from the loader, so we don't take advantage of
#### this.

# Runs in real mode, which is a 16-bit segment.
	.code16

# Set up segment registers.
# Set stack to grow downward from 60 kB (after boot, the kernel
# continues to use this stack for its initial thread).

	sub %ax, %ax
	mov %ax, %ds
	mov %ax, %ss
	mov $0xf000, %esp

# Configure serial port so we can report progress without connected VGA.
# See [IntrList] for details.
	sub %dx, %dx			# Serial port 0.
	mov $0xe3, %al			# 9600 bps, N-8-1.
					# AH is already 0 (Initialize Port).
	int $0x14			# Destroys AX.

	call puts
	.string "PiLo"

####        ITERATE THROUGH ALL HARD DISKS IN THE SYSTEM, LOOKING FOR A
####        BOOTABLE PINTOS PARTITION TO LOAD AND START.
####
####        Note that there are only ~300 bytes available for the entire
####        Pintos bootloader, as Pintos commands are stored in the
####        region immediately before the partition table in the sector.
####        Sooo... be concise!
####
####        If you want to create helper routines, there is a TODO toward
####        the end of this file that shows where they should go.

    # Iterate through all hard drives and read the MBR to see if it
    # has a Pintos partition. Die if "drive does not exist" error 
    # occurs.
    mov $0x80, %edx
drive_loop:
    mov $0x8000, %bx # Memory segment to save to
    xor %esi, %esi # The LBA to read
    call read_sector

    cmp $0x0, %ah # See if there's a drive error 
    jne error_dr # Got to a point where there's no drive and haven't booted.

    # Check that it's a boot sector
    mov %bx, %es # $0x8000 -> %es
    mov $0x01FE, %bx
    # If it isn't, skip to the next drive
    cmpw $0xAA55, %es:(%bx)
    jne pre_drive_loop

    # Check each partition to be a Pintos drive
    mov $0x01BE, %ebx
part_loop:
    # See if it's a bootable partition, if not, check next partition
    cmpb $0x80, %es:(%bx)
    jne pre_part_loop

    # See if it's a Pintos partition, if not, check next partition
    cmpb $0x20, %es:4(%ebx)
    jne pre_part_loop

    # Otherwise, we found a partition, start loading up to the first 512 KiB
    # into memory location 0x20000.
    # Print out the drive info
    call puts
    .string "\rhd"
    leal -0x1F(%edx), %eax
    call putc
    mov %bl, %al
    shr $4, %al
    add $0x26, %al
    call putc
    mov $0x0D, %al
    call putc

    # Start by saving the start LBA and num sectors of this partition:
    mov %es:8(%ebx), %esi # LBA of starting sector
    mov %es:12(%ebx), %ecx # Number of sectors in the partition
    # Note that %dl is currently the correct drive number

    # Start off the segment and offset locations correctly
    movw $0x2000, %bx # Segment

    # Make sure we don't read more than 1024 sectors (512 KiB)
    cmp $1024, %ecx
    jl kernel_loop
    mov $1024, %ecx

kernel_loop:
    # If number of sectors left divisible by 16, print a dot
    testw $15, %cx
    jnz kernel_loop_3

    mov $0x2E, %al
    call putc

kernel_loop_3:
    pushl %esi
    call read_sector

    cmp $0x0, %ah # See if there's a drive error 
    jne error_rd # Had a read error 
    
    popl %esi

    add $0x20, %bx # Increase segment
    inc %esi # Increase LBA
    dec %ecx # One fewer sector left
    jz continue
    jmp kernel_loop

# Increment the disk counter and jump back to the drive loop
pre_drive_loop:
    inc %dl
    jmp drive_loop

# Increment the partition number and jump back to the partition loop
pre_part_loop:
    addw $0x010, %bx
    # If we're no longer in the partition entry portion, skip to next disk
    cmpw $0x01FE, %bx
    jge pre_drive_loop
    jmp part_loop

error_rd:
    call puts
    .string " e2"
    jmp error
error_dr:
    call puts
    .string " e1"
error:
    int $0x18
continue:
    

#### LOADING CODE ENDS HERE!

#### Transfer control to the kernel that we loaded.  We read the start
#### address out of the ELF header (see [ELF1]) and convert it from a
#### 32-bit linear address into a 16:16 segment:offset address for
#### real mode, then jump to the converted address.  The 80x86 doesn't
#### have an instruction to jump to an absolute segment:offset kept in
#### registers, so in fact we store the address in a temporary memory
#### location, then jump indirectly through that location.  To save 4
#### bytes in the loader, we reuse 4 bytes of the loader's code for
#### this temporary pointer.

	mov $0x2000, %ax
	mov %ax, %es
	mov %es:0x18, %dx
	mov %dx, start
	movw $0x2000, start + 2
	ljmp *start

#### The following label is a placeholder for the preceding code to use
#### to set up for the jump to the Pintos kernel.  Of course, the start
#### label also points to the puts function; when the previous code runs,
#### it will clobber a few of the bytes at the start of the puts function,
#### but hey we're done bootloading, so we don't need it anymore.

start:

#### Print string subroutine.  To save space in the loader, this
#### subroutine takes its null-terminated string argument from the
#### code stream just after the call, and then returns to the byte
#### just after the terminating null.  This subroutine preserves all
#### general-purpose registers.

puts:	xchg %si, %ss:(%esp)
	push %ax
next_char:
	mov %cs:(%si), %al
	inc %si
	test %al, %al
	jz 1f
	call putc
	jmp next_char
1:	pop %ax
	xchg %si, %ss:(%esp)
	ret

#### Character output subroutine.  Prints the character in AL to the
#### VGA display and serial port 0, using BIOS services (see
#### [IntrList]).  Preserves all general-purpose registers.
####
#### If called upon to output a carriage return, this subroutine
#### automatically supplies the following line feed.

putc:	pusha

1:	sub %bh, %bh			# Page 0.
	mov $0x0e, %ah			# Teletype output service.
	int $0x10

	mov $0x01, %ah			# Serial port output service.
	sub %dx, %dx			# Serial port 0.
2:	int $0x14			# Destroys AH.
	test $0x80, %ah			# Output timed out?
	jz 3f
	movw $0x9090, 2b		# Turn "int $0x14" above into NOPs.

3:
	cmp $'\r', %al
	jne popa_ret
	mov $'\n', %al
	jmp 1b

popa_ret:
	popa
	ret				# Error code still in CF


####        YOU CAN PUT HELPER FUNCTIONS HERE IF YOU WISH.

# Expects the LBA to be in %esi, the memory segment in %bx,
# the offset is 0, and the drive number to be in %dl
# This function will clobber %ax and %si
read_sector:
    mov $0x42, %ah
    pushl $0x0 
    pushl %esi # LBA of sector
    pushw %bx # Segment
    pushw $0x0 # Offset
    pushw $0x1 # Number of sectors
    pushw $0x10 # Size of DAP and unused byte
    #mov %ss, %si # Make sure %ds:%si points to the stack(which is at %ss: %sp)
    #mov %si, %ds
    mov %sp, %si
    int $0x13
    add $16, %esp # Pop the DAP
    ret

#### Command-line arguments and their count.
#### This is written by the `pintos' utility and read by the kernel.
#### The loader itself does not do anything with the command line.
	.org LOADER_ARG_CNT - LOADER_BASE
	.fill LOADER_ARG_CNT_LEN, 1, 0

	.org LOADER_ARGS - LOADER_BASE
	.fill LOADER_ARGS_LEN, 1, 0

#### Partition table.
	.org LOADER_PARTS - LOADER_BASE
	.fill LOADER_PARTS_LEN, 1, 0

#### Boot-sector signature for BIOS inspection.
	.org LOADER_SIG - LOADER_BASE
	.word 0xaa55


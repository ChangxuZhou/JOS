OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
    . = 0x80010000;
    .text : {
        *(.text)
    }
    .data : {
        *(.data)
    }
    .bss : {
        *(.bss)
    }

    end = . ;
}

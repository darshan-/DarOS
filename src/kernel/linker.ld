OUTPUT_FORMAT("binary")

SECTIONS
{
        . = 0x7c00;

	.text :
	{
                *(.boot)
                *(.kernel_entry)
                *(.text)
	}

        /*
          I can use GCC's -fno-zero-initialized-in-bss to actually initialize zero-initialized data, but
          I in fact want to make my kernel binary / disk image be exactly what gets loaded at 0x7c00 --
          I don't want to overwrite BIOS areas or anything, so I can just have the assumption that there's
          as much room as I need after the end of the image.  I need the image to match reality.  So put
          .bss in .data section.

          P.S. How was my .data being included?  I didn't put it in .text section, or anwhere else...  Is it
          automatic/

          Hmm, yes, it seems so.  We must discard anything we don't want with /DISCARD/
        */
	.data :
	{
                *(.data)
                *(.bss)
	}

        /DISCARD/ :
        {
                *(.comment)
                *(.note.GNU-stack)
                *(.note.gnu.property) /* This is the one that is otherwise being kept... */
                *(.eh_frame)          /* Wow, this clean up a lot of space, and I'm *pretty* sure I don't need it. */
        }
}

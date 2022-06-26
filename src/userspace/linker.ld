ENTRY(main)

OUTPUT_FORMAT("binary")

SECTIONS
{
        . = 0x7FC0000000;

	.text :
	{
                *(.text)
	}

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

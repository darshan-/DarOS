ENTRY(kernel_entry)

OUTPUT_FORMAT("binary")

SECTIONS
{
	.text :
	{
                *(.kernel_entry)
		*(.text)
	}
}

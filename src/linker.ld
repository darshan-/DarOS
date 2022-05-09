ENTRY(kernel_entry)

OUTPUT_FORMAT("binary")

SECTIONS
{
	.text :
	{
		*(.text)
	}
}

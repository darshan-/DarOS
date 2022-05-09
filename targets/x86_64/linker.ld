ENTRY(kernel_main)

OUTPUT_FORMAT("binary")

SECTIONS
{
	.text :
	{
		*(.text)
	}
}

/* The section loader packs .rodata immediately after .data. Pad .data so
 * pointer tables retain alignment after loading. minicw_elf_inspect.py also
 * checks the remaining packed section boundaries. */
SECTIONS
{
  .data : { *(.data .data.*) . = ALIGN(4); }
}
INSERT AFTER .data.rel.ro;

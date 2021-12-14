OUTPUT_ARCH(arm)
ENTRY(_start)
SECTIONS
{
  /DISCARD/ : {
    *(.ARM.exidx*)
    *(.ARM.extab*)
    *(.data)
  }
  . = 0;
  _text = .;
  .text : {
    _start = .;
    *(.start)
    *(.text)
    *(.text.*)
    *(.fixup)
    *(.gnu.warning)
    *(.glue_7t)
    *(.glue_7)
  }
  .table : ALIGN(4) {
    _table_start = .;
    LONG((2))
    LONG((0x5a534c4b))
    LONG((__piggy_size_addr - _start))
    LONG((_kernel_bss_size))
    LONG(0)
    _table_end = .;
  }
  .rodata : {
    *(.rodata)
    *(.rodata.*)
    *(.data.rel.ro)
  }
  .piggydata : {
    *(.piggydata)
    __piggy_size_addr = . - 4;
  }
  . = ALIGN(4);
  _etext = .;
  .got.plt : { *(.got.plt) }
  _got_start = .;
  .got : { *(.got) }
  _got_end = .;
  .pad : { BYTE(0); . = ALIGN(8); }
  .data : ALIGN(4096) {
    __pecoff_data_start = .;
    *(.data.efistub)
    __pecoff_data_end = .;
    . = ALIGN(512);
  }
  __pecoff_data_rawsize = . - ADDR(.data);
  _edata = .;
  .image_end (NOLOAD) : {
    _edata_real = .;
  }
  _magic_sig = (0x016f2818);
  _magic_start = (_start);
  _magic_end = (_edata);
  _magic_table = (_table_start - _start);
  . = ALIGN(8);
  __bss_start = .;
  .bss : { *(.bss) }
  _end = .;
  . = ALIGN(8);
  .stack : { *(.stack) }
  PROVIDE(__pecoff_data_size = ALIGN(512) - ADDR(.data));
  PROVIDE(__pecoff_end = ALIGN(512));
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
}
ASSERT(_edata_real == _edata, "error: zImage file size is incorrect");

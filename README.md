# MiSTer FPGA cpufreq driver

This is very WIP! It's not fully working yet.

Compile this as a module and the DE10 Nano will be able to scale between 800
MHz and 400 MHz via cpufreq governor.

I have overclocking working in principle, but this driver can't do it yet.

## Helpful resources

- [Cyclone V Hard Processor System Technical Reference Manual](https://www.intel.com/content/dam/www/programmable/us/en/pdfs/literature/hb/cyclone-v/cv_54019.pdf)
- u-boot board code [cyclone5-socdk](https://github.com/altera-opensource/u-boot-socfpga/tree/socfpga_v2021.07/board/altera/cyclone5-socdk/qts) and [de10-nano](https://github.com/altera-opensource/u-boot-socfpga/tree/socfpga_v2021.07/board/terasic/de10-nano/qts)
- [u-boot clock manager code](https://github.com/altera-opensource/u-boot-socfpga/blob/socfpga_v2021.07/arch/arm/mach-socfpga/clock_manager_gen5.c)
- [Linux socfpga Common Clock Framework drivers](https://github.com/altera-opensource/linux-socfpga/tree/socfpga-5.15/drivers/clk/socfpga)

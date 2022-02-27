# MiSTer FPGA cpufreq driver

This is very WIP! It's not fully working yet.

Compile this as a module and the DE10 Nano will be able to scale between 800
MHz and 400 MHz via cpufreq governor.

I have overclocking working in principle, but this driver can't do it yet.

## Notes
1. `CONFIG_CPU_FREQ` needs to be set to `y` on the kernel.
2. Currently this is set up as a loadable kernel module. I'll try to upstream it into the MiSTer kernel when working well.
3. I'll keep any kernel changes in this driver. I don't think I'll update the `socfpga` Common Clock Framework unless I can upstream the changes even further.

## Helpful resources

### Cyclone V docs/code
- [Cyclone V Hard Processor System Technical Reference Manual](https://www.intel.com/content/dam/www/programmable/us/en/pdfs/literature/hb/cyclone-v/cv_54019.pdf) - Everything in the Clock Manager chapter is very helpful.
- u-boot board code [cyclone5-socdk](https://github.com/altera-opensource/u-boot-socfpga/tree/socfpga_v2021.07/board/altera/cyclone5-socdk/qts) and [de10-nano](https://github.com/altera-opensource/u-boot-socfpga/tree/socfpga_v2021.07/board/terasic/de10-nano/qts) - Most of the clock setup is done in u-boot. The socdk uses a higher speed grade so comparing the header files was crucial.
- [u-boot clock manager code](https://github.com/altera-opensource/u-boot-socfpga/blob/socfpga_v2021.07/arch/arm/mach-socfpga/clock_manager_gen5.c)
- [Linux socfpga Common Clock Framework drivers](https://github.com/altera-opensource/linux-socfpga/tree/socfpga-5.15/drivers/clk/socfpga) - Not as helpful compared to u-boot.

### How to build
- [Compiling the Linux kernel for MiSTer](https://github.com/MiSTer-devel/Main_MiSTer/wiki/Compiling-the-Linux-kernel-for-MiSTer)
- [MISTER CUSTOM WIFI DRIVER COMPILATION GUIDE](https://github.com/MiSTer-devel/Main_MiSTer/wiki/MISTER-CUSTOM-WIFI-DRIVER-COMPILATION-GUIDE) - how to build as a Linux kernel module

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/cpufreq.h>

#define DRIVER_AUTHOR "Michael Huang <coolbho3000@gmail.com>"
#define DRIVER_DESCRIPTION "MiSTer FPGA cpufreq driver"
#define DRIVER_VERSION "1.0"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

#define OSC1_HZ 25000000;

#define VCO_NUMER_OFFSET 3
#define VCO_DENOM_OFFSET 16

#define DIVF_MASK 0x0000FFF8
#define DIVF_SHIFT 3
#define DIVQ_MASK 0x003F0000
#define DIVQ_SHIFT 16

// Address offsets
#define MAINPLL_VCO					0x40
#define MAINPLL_MPUCLK				0x48
#define MAINPLL_CFGS2FUSER0CLK		0x5c
#define MAINPLL_EN					0x60
#define ALTR_MPUCLK					0xe0
#define ALTR_MAINCLK				0xe4
#define ALTR_DBGATCLK				0xe8


void __iomem *clk_mgr_base_addr;


struct socfpga_clock_data {
	u32 vco_numer; // Numerator for calculating VCO register
	u32 vco_denom; // Denominator for calculating VCO register
	u32 alteragrp_mpuclk; // Divides the VCO/2 frequency by the value+1
	u32 alteragrp_mainclk; // Divides the VCO frequency by the value+1
	u32 alteragrp_dbgatclk; // Divides the VCO frequency by the value+1
	u32 cfgs2fuser0clk_cnt; // Divides the VCO frequency by the value+1
};

// 1200 MHz overclock
static const struct socfpga_clock_data clock_data_1200000 = {
	.vco_numer = 95, // 25 MHz * (95 + 1) / (0 + 1) = 2400 MHz
	.vco_denom = 0,
	.alteragrp_mpuclk = 1, // 2400 MHz / (1 + 1) = 1200 MHz
	.alteragrp_mainclk = 5, // 2400 MHz / (5 + 1) = 400 MHz
	.alteragrp_dbgatclk = 5, // 2000 MHz / (5 + 1) = 400 MHz
	.cfgs2fuser0clk_cnt = 23, // 2000 MHz / (23 + 1) = 100 MHz
};

// 1000 MHz overclock
static const struct socfpga_clock_data clock_data_1000000 = {
	.vco_numer = 79, // 25 MHz * (79 + 1) / (0 + 1) = 2000 MHz
	.vco_denom = 0,
	.alteragrp_mpuclk = 1, // 2000 MHz / (1 + 1) = 1000 MHz
	.alteragrp_mainclk = 4, // 2000 MHz / (4 + 1) = 400 MHz
	.alteragrp_dbgatclk = 4, // 2000 MHz / (4 + 1) = 400 MHz
	.cfgs2fuser0clk_cnt = 19, // 2000 MHz / (19 + 1) = 100 MHz
};

// 800 MHz. Default for -I7 and -C7 speed grades
static const struct socfpga_clock_data clock_data_800000 = {
	.vco_numer = 63, // 25 MHz * (63 + 1) / (0 + 1) = 1600 MHz
	.vco_denom = 0,
	.alteragrp_mpuclk = 1, // 1600 MHz / (1 + 1) = 800 MHz
	.alteragrp_mainclk = 3, // 1600 MHz / (3 + 1) = 400 MHz
	.alteragrp_dbgatclk = 3, // 1600 MHz / (3 + 1) = 400 MHz
	.cfgs2fuser0clk_cnt = 15, // 1600 MHz / (15 + 1) = 100 MHz
};

// 400 MHz underclock
static const struct socfpga_clock_data clock_data_400000 = {
	.vco_numer = 63, // 25 MHz * (63 + 1) / (0 + 1) = 1600 MHz
	.vco_denom = 0,
	.alteragrp_mpuclk = 3, // 1600 MHz / (3 + 1) = 400 MHz
	.alteragrp_mainclk = 3, // 1600 MHz / (3 + 1) = 400 MHz
	.alteragrp_dbgatclk = 3, // 1600 MHz / (3 + 1) = 400 MHz
	.cfgs2fuser0clk_cnt = 15, // 1600 MHz / (15 + 1) = 100 MHz
};

// 266.66 MHz underclock
static const struct socfpga_clock_data clock_data_266666 = {
	.vco_numer = 63, // 25 MHz * (63 + 1) / (0 + 1) = 1600 MHz
	.vco_denom = 0,
	.alteragrp_mpuclk = 5, // 1600 MHz / (5 + 1) = 266.66 MHz
	.alteragrp_mainclk = 3, // 1600 MHz / (3 + 1) = 400 MHz
	.alteragrp_dbgatclk = 3, // 1600 MHz / (3 + 1) = 400 MHz
	.cfgs2fuser0clk_cnt = 15, // 1600 MHz / (15 + 1) = 100 MHz
};

#define SOCFPGA_CPUFREQ_ROW(freq_khz, f) 		\
	{				\
		.driver_data = (unsigned int) &clock_data_##freq_khz,	\
		.frequency = freq_khz, \
		.flags = f, \
	}


static struct cpufreq_frequency_table freq_table[] = {
	// Mark OC rows as CPUFREQ_BOOST_FREQ to prevent cpufreq from setting them
	// as the policy max
	SOCFPGA_CPUFREQ_ROW(1200000, CPUFREQ_BOOST_FREQ),
	SOCFPGA_CPUFREQ_ROW(1000000, CPUFREQ_BOOST_FREQ),
	SOCFPGA_CPUFREQ_ROW(800000, 0),
	SOCFPGA_CPUFREQ_ROW(400000, 0),
	SOCFPGA_CPUFREQ_ROW(266666, 0),
	{
		.driver_data = 0,
		.frequency	= CPUFREQ_TABLE_END,
	},
};


// Calculate the value of the VCO register from a numerator (divf) and
// denominator (divq)
static unsigned int calculate_vco_reg(u32 numer, u32 denom) {
	return (denom << VCO_DENOM_OFFSET) | (numer << VCO_NUMER_OFFSET);
}

// Calculate the VCO clock in hertz from a numerator (divf) and denominator
// (divq)
static unsigned long long calculate_vco_clock_hz(u32 numer, u32 denom) {
	unsigned long long vco_freq = OSC1_HZ;
	vco_freq *= (numer + 1);
	do_div(vco_freq, (1 + denom));
	return vco_freq;
}

static int socfpga_verify_speed(struct cpufreq_policy_data *policy)
{
	return cpufreq_frequency_table_verify(policy, freq_table);
}

static unsigned int socfpga_get(unsigned int cpu) {
	unsigned long vco_reg, alteragrp_mpuclk_reg, mpuclk_cnt_reg;
	unsigned long long mpuclk_freq;
	u32 numer, denom;

	// Get value of alteragrp_mpuclk
	alteragrp_mpuclk_reg = readl(clk_mgr_base_addr + ALTR_MPUCLK);

	// Get value of mpuclk_cnt
	mpuclk_cnt_reg = readl(clk_mgr_base_addr + MAINPLL_MPUCLK);

	// Get and calculate VCO clock
	vco_reg = readl(clk_mgr_base_addr + MAINPLL_VCO);
	numer = vco_reg >> VCO_NUMER_OFFSET;
	denom = vco_reg >> VCO_DENOM_OFFSET;
	mpuclk_freq = calculate_vco_clock_hz(numer, denom);

	// Divide by value of registers
	do_div(mpuclk_freq, (1 + alteragrp_mpuclk_reg));
	do_div(mpuclk_freq, mpuclk_cnt_reg + 1);

	// Convert to KHz
	do_div(mpuclk_freq, 1000);

	return (unsigned int)mpuclk_freq;
}

static int socfpga_target_index(struct cpufreq_policy *policy,
		       unsigned int index)
{
	unsigned int freq;
	struct socfpga_clock_data * clock_data;
	clock_data = freq_table[index].driver_data;
	printk(KERN_INFO "TARGET FREQ %u\n", freq_table[index].frequency);
	return 0;

}

static int socfpga_cpu_init(struct cpufreq_policy *policy)
{

	policy->cur = socfpga_get(policy->cpu);
	policy->cpuinfo.transition_latency = 300 * 1000;
	policy->cpuinfo.max_freq=1000000;
	policy->cpuinfo.min_freq=266666;
	policy->freq_table = freq_table;
	cpumask_setall(policy->cpus);
	return 0;
}


static int socfpga_cpu_exit(struct cpufreq_policy *policy)
{
	return 0;
}


static struct freq_attr *socfpga_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	&cpufreq_freq_attr_scaling_boost_freqs,
	NULL,
};

static struct cpufreq_driver socfpga_cpufreq_driver = {
	.verify				= socfpga_verify_speed,
	.target_index		= socfpga_target_index,
	.get				= socfpga_get,
	.init				= socfpga_cpu_init,
	.exit				= socfpga_cpu_exit,
	.name				= "socfpga",
	.attr		        = socfpga_cpufreq_attr,
};

static int __init socfpga_cpufreq_init(void)
{
	unsigned long reg;
	struct device_node *clkmgr_np;

	clkmgr_np = of_find_compatible_node(NULL, NULL, "altr,clk-mgr");
	clk_mgr_base_addr = of_iomap(clkmgr_np, 0);
	of_node_put(clkmgr_np);
	reg = readl(clk_mgr_base_addr + 0x40);
	return cpufreq_register_driver(&socfpga_cpufreq_driver);
}

static void __exit socfpga_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&socfpga_cpufreq_driver);
}

module_init(socfpga_cpufreq_init);
module_exit(socfpga_cpufreq_exit);

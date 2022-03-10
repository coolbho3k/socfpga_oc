#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/cpufreq.h>
#include <linux/bitops.h>
#include <linux/wait_bit.h>

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

#define CLKMGR_GEN5_INTER			0x08
#define CLKMGR_GEN5_BYPASS 0x04
#define CLKMGR_BYPASS_MAINPLL				BIT(0)
#define CLKMGR_MAINPLLGRP_EN_DBGATCLK_MASK      BIT(4)

#define CLKMGR_INTER_MAINPLLLOCKED_MASK			BIT(6)
#define CLKMGR_INTER_PERPLLLOCKED_MASK			BIT(7)
#define CLKMGR_INTER_SDRPLLLOCKED_MASK			BIT(8)
#define LOCKED_MASK \
	(CLKMGR_INTER_SDRPLLLOCKED_MASK  | \
	CLKMGR_INTER_PERPLLLOCKED_MASK  | \
	CLKMGR_INTER_MAINPLLLOCKED_MASK)

#define CLKMGR_STAT			0x14
#define CLKMGR_STAT_BUSY				BIT(0)

// Address offsets
#define MAINPLL_VCO					0x40

#define ALTR_MPUCLK  				0xe0
#define ALTR_MAINCLK                0xe4
#define ALTR_DBGATCLK               0xe8
#define MAINPLL_MPUCLK              0x48
#define MAINPLL_CFGS2FUSER0CLK		0x5c
#define MAINPLL_EN					0x60


void __iomem *clk_mgr_base_addr;


struct socfpga_clock_data {
	u32 vco_numer; // Numerator for calculating VCO register
	u32 vco_denom; // Denominator for calculating VCO register
	u32 alteragrp_mpuclk; // Divides the VCO frequency by the value+1
	u32 alteragrp_mainclk; // Divides the VCO frequency by the value+1
	u32 alteragrp_dbgatclk; // Divides the VCO frequency by the value+1
	u32 mainpll_cfgs2fuser0clk; // Divides the VCO frequency by the value+1
};

// // 1200 MHz overclock
// static const struct socfpga_clock_data clock_data_1200000 = {
// 	.vco_numer = 95, // 25 MHz * (95 + 1) / (0 + 1) = 2400 MHz
// 	.vco_denom = 0,
// 	.alteragrp_mpuclk = 1, // 2400 MHz / (1 + 1) = 1200 MHz
// 	.alteragrp_mainclk = 5, // 2400 MHz / (5 + 1) = 400 MHz
// 	.alteragrp_dbgatclk = 5, // 2000 MHz / (5 + 1) = 400 MHz
// 	.mainpll_cfgs2fuser0clk = 23, // 2400 MHz / (23 + 1) = 100 MHz
// };

// 1000 MHz overclock
static const struct socfpga_clock_data clock_data_1000000 = {
	.vco_numer = 79, // 25 MHz * (79 + 1) / (0 + 1) = 2000 MHz
	.vco_denom = 0,
	.alteragrp_mpuclk = 1, // 2000 MHz / (1 + 1) = 1000 MHz
	.alteragrp_mainclk = 4, // 2000 MHz / (4 + 1) = 400 MHz
	.alteragrp_dbgatclk = 4, // 2000 MHz / (4 + 1) = 400 MHz
	.mainpll_cfgs2fuser0clk = 19, // 2000 MHz / (19 + 1) = 100 MHz
};

// 800 MHz. Default for -I7 and -C7 speed grades
static const struct socfpga_clock_data clock_data_800000 = {
	.vco_numer = 63, // 25 MHz * (63 + 1) / (0 + 1) = 1600 MHz
	.vco_denom = 0,
	.alteragrp_mpuclk = 1, // 1600 MHz / (1 + 1) = 800 MHz
	.alteragrp_mainclk = 3, // 1600 MHz / (3 + 1) = 400 MHz
	.alteragrp_dbgatclk = 3, // 1600 MHz / (3 + 1) = 400 MHz
	.mainpll_cfgs2fuser0clk = 15, // 1600 MHz / (15 + 1) = 100 MHz
};

// 400 MHz underclock
static const struct socfpga_clock_data clock_data_400000 = {
	.vco_numer = 63, // 25 MHz * (63 + 1) / (0 + 1) = 1600 MHz
	.vco_denom = 0,
	.alteragrp_mpuclk = 3, // 1600 MHz / (3 + 1) = 400 MHz
	.alteragrp_mainclk = 3, // 1600 MHz / (3 + 1) = 400 MHz
	.alteragrp_dbgatclk = 3, // 1600 MHz / (3 + 1) = 400 MHz
	.mainpll_cfgs2fuser0clk = 15, // 1600 MHz / (15 + 1) = 100 MHz
};

// 266 MHz underclock
static const struct socfpga_clock_data clock_data_266666 = {
	.vco_numer = 63, // 25 MHz * (63 + 1) / (0 + 1) = 1600 MHz
	.vco_denom = 0,
	.alteragrp_mpuclk = 5, // 1600 MHz / (5 + 1) = 266 MHz
	.alteragrp_mainclk = 3, // 1600 MHz / (3 + 1) = 400 MHz
	.alteragrp_dbgatclk = 3, // 1600 MHz / (3 + 1) = 400 MHz
	.mainpll_cfgs2fuser0clk = 15, // 1600 MHz / (15 + 1) = 100 MHz
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
	//SOCFPGA_CPUFREQ_ROW(1200000, CPUFREQ_BOOST_FREQ),
	SOCFPGA_CPUFREQ_ROW(1000000, CPUFREQ_BOOST_FREQ),
	SOCFPGA_CPUFREQ_ROW(800000, 0),
	SOCFPGA_CPUFREQ_ROW(400000, 0),
	SOCFPGA_CPUFREQ_ROW(266666, 0),
	{
		.driver_data = 0,
		.frequency	= CPUFREQ_TABLE_END,
	},
};


// 1.225
// 1.188

// Calculate the value of the VCO register from a numerator (divf) and
// denominator (divq)
static unsigned int calculate_vco_reg(u32 numer, u32 denom) {
	u32 vco_reg;
	vco_reg = readl(clk_mgr_base_addr + MAINPLL_VCO);
	return BIT(1) | ((denom << VCO_DENOM_OFFSET) | (numer << VCO_NUMER_OFFSET));
}

// Calculate the VCO clock in hertz from a numerator (divf) and denominator
// (divq)
static unsigned long long calculate_vco_clock_hz(u32 numer, u32 denom) {
	unsigned long long vco_freq = OSC1_HZ;
	vco_freq *= (numer + 1);
	do_div(vco_freq, (denom + 1));
	return vco_freq;
}

static unsigned long long get_vco_clock_hz(void) {
	unsigned long vco_reg;
	u32 numer, denom;

	vco_reg = readl(clk_mgr_base_addr + MAINPLL_VCO);
	numer = vco_reg >> VCO_NUMER_OFFSET;
	denom = vco_reg >> VCO_DENOM_OFFSET;
	return calculate_vco_clock_hz(numer, denom);
}

void wait_for_lock(u32 mask)
{
	u32 inter_val;
	u32 retry = 0;
	do {
		inter_val = readl(clk_mgr_base_addr +
				  CLKMGR_GEN5_INTER) & mask;
		/* Wait for stable lock */
		if (inter_val == mask)
			retry++;
		else {
			printk(KERN_INFO "socfpga_cpufreq: retry %u\n", retry);
			retry = 0;
		}
		if (retry >= 10)
			break;
	} while (1);
	printk(KERN_INFO "socfpga_cpufreq: waited for lock, retry %u\n", retry);
}

int wait_for_fsm(void)
{
	printk(KERN_INFO "socfpga_cpufreq: bit before %u\n", readl(clk_mgr_base_addr + CLKMGR_STAT));
	wait_on_bit((void *)(clk_mgr_base_addr +
				 CLKMGR_STAT), CLKMGR_STAT_BUSY, TASK_UNINTERRUPTIBLE);
	printk(KERN_INFO "socfpga_cpufreq: bit after %u\n", readl(clk_mgr_base_addr + CLKMGR_STAT));
	return 0;
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
	mpuclk_freq = get_vco_clock_hz();

	// Divide by value of registers
	do_div(mpuclk_freq, (1 + alteragrp_mpuclk_reg));
	do_div(mpuclk_freq, mpuclk_cnt_reg + 1);

	// Convert to KHz
	do_div(mpuclk_freq, 1000);

	return (unsigned int)mpuclk_freq;
}

static inline void set_dividers(struct socfpga_clock_data * clock_data) {
	writel(clock_data->alteragrp_mpuclk, clk_mgr_base_addr + ALTR_MPUCLK);
	writel(clock_data->alteragrp_mainclk, clk_mgr_base_addr + ALTR_MAINCLK);
	writel(clock_data->alteragrp_dbgatclk, clk_mgr_base_addr + ALTR_DBGATCLK);
	writel(clock_data->mainpll_cfgs2fuser0clk, clk_mgr_base_addr + MAINPLL_CFGS2FUSER0CLK);
}

static inline void set_vco_freq(struct socfpga_clock_data * clock_data) {
	// Put main PLL into bypass
	writel(CLKMGR_BYPASS_MAINPLL, clk_mgr_base_addr + CLKMGR_GEN5_BYPASS);
	wait_for_fsm();

	// Set VCO register
	writel(calculate_vco_reg(clock_data->vco_numer, clock_data->vco_denom), clk_mgr_base_addr + MAINPLL_VCO);

	// Wait for PLL lock
	wait_for_lock(LOCKED_MASK);

	// Put main PLL out of bypass
	writel(0, clk_mgr_base_addr + CLKMGR_GEN5_BYPASS);
	wait_for_fsm();
}


static int socfpga_target_index(struct cpufreq_policy *policy,
		       unsigned int index)
{
	struct socfpga_clock_data *clock_data;
	unsigned long long current_vco_clock_hz, target_vco_clock_hz;

	clock_data = (struct socfpga_clock_data *) freq_table[index].driver_data;
	current_vco_clock_hz = get_vco_clock_hz();
	target_vco_clock_hz = calculate_vco_clock_hz(clock_data->vco_numer, clock_data->vco_denom);

	printk(KERN_INFO "socfpga_cpufreq: 63 reg is %x", calculate_vco_reg(63, 0));
	printk(KERN_INFO "socfpga_cpufreq: 79 reg is %x", calculate_vco_reg(79, 0));
	printk(KERN_INFO "socfpga_cpufreq: 95 reg is %x", calculate_vco_reg(95, 0));

	// Get current VCO freq
	// If target VCO freq is equal to current VCO freq, set dividers only
	if (target_vco_clock_hz == current_vco_clock_hz) {
		printk(KERN_INFO "socfpga_cpufreq: target vco equal, set dividers only\n");
		set_dividers(clock_data);
	// If target VCO freq is greater than current VCO freq, set dividers first
	} else if (target_vco_clock_hz > current_vco_clock_hz) {
		printk(KERN_INFO "socfpga_cpufreq: target vco greater, set dividers first\n");
		set_dividers(clock_data);
		set_vco_freq(clock_data);
	// If target VCO freq is less than current VCO freq, set VCO freq first
	} else if (target_vco_clock_hz < current_vco_clock_hz) {
		printk(KERN_INFO "socfpga_cpufreq: target vco lower, set vco first\n");
		set_vco_freq(clock_data);
		set_dividers(clock_data);
	}

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
	clk_mgr_base_addr = NULL;
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
	reg = readl(clk_mgr_base_addr + MAINPLL_VCO);
	return cpufreq_register_driver(&socfpga_cpufreq_driver);
}

static void __exit socfpga_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&socfpga_cpufreq_driver);
}

module_init(socfpga_cpufreq_init);
module_exit(socfpga_cpufreq_exit);

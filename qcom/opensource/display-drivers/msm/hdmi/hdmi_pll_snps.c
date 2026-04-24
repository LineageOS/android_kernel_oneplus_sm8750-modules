// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/unistd.h>
#include <linux/gcd.h>
#include <linux/math.h>

#include "hdmi_pll.h"
#include "hdmi_reg_snps.h"

#define hdmi_read(x, off) \
	readl_relaxed((x)->io_data->io.base + (off))

#define hdmi_write(x, off, value)  \
	writel_relaxed(value, (x)->io_data->io.base + (off))

#define REF_CLK_MHZ	38.4

#define SNPS_MPLL(mul, qut, rem, den, clkd, vcof, pat, rxv, in, prop) \
	.multiplier = (mul), .quotient = (qut), .remainder = (rem), \
	.denominator = (den), .tx_clk_div = (clkd), .vco_freq = (vcof), \
	.clk_pat = (pat), .rxvcosel = (rxv), .cp_int_holder = (in), \
	.cp_prop_holder = (prop)

struct hdmi_pll_snps_mpll {
	u32 vco_freq;
	u32 rxvcosel;
	u32 tx_clk_div;
	u32 cp_int_holder;
	u32 cp_prop_holder;
	u32 quotient;
	u32 remainder;
	u32 multiplier;
	u32 denominator;
	u32 clk_pat;
};

struct hdmi_pll_snps_mpll_wrapper {
	u32 clk;
	struct hdmi_pll_snps_mpll mpll;
};

/* Operation for the register configuration.
 * READ		0,
 * WRITE	1,
 */
enum {
	SET_BIT = 2,
	CLEAR_BIT = 3,
};

static const struct hdmi_pll_snps_mpll_wrapper mpll_list[] = {
	/* 25.2MHz */
	{ 25200, {SNPS_MPLL(0xb0, 0xe525, 0, 1, 5, 2, 0, 3, 0xc, 0x11) }, },
	/* 27MHz */
	{ 27000, {SNPS_MPLL(0xc0, 0x8000, 0, 1, 5, 2, 0, 3, 0xc, 0x11) }, },
	/* 27.027MHz */
	{ 27027, {SNPS_MPLL(0xc0, 0x9ccc, 4, 1, 5, 2, 0, 3, 0xb, 0x12) }, },
	/* 54MHz */
	{ 54000, {SNPS_MPLL(0xc0, 0x8000, 0, 1, 4, 2, 0, 3, 0xb, 0x11) }, },
	/* 59.4MHz */
	{ 59400, {SNPS_MPLL(0xd6, 0xc000, 0, 1, 4, 2, 0, 3, 0xb, 0x12) }, },
	/* 72MHz */
	{ 72000, {SNPS_MPLL(0x76, 0x0000, 0, 1, 3, 3, 0, 4, 0xc, 0x10) }, },
	/* 74.25MHz */
	{ 74250, {SNPS_MPLL(0x7a, 0x5800, 0, 1, 3, 3, 0, 4, 0xb, 0x12) }, },
	/* 82.5Mhz */
	{ 82500, {SNPS_MPLL(0x8a, 0xf000, 0, 1, 3, 3, 0, 4, 0xb, 0x11) }, },
	/* 90MHz */
	{ 90000, {SNPS_MPLL(0x9a, 0xc000, 0, 1, 3, 2, 0, 4, 0xd, 0x11) }, },
	/* 99MHz */
	{ 99000, {SNPS_MPLL(0xae, 0x2000, 0, 1, 3, 2, 0, 4, 0xc, 0x11) }, },
	/* 108MHz */
	{ 108000, {SNPS_MPLL(0xc0, 0x8000, 0, 1, 3, 2, 0, 3, 0xb, 0x11) }, },
	/* 118.8MHz */
	{ 118800, {SNPS_MPLL(0xd6, 0xc000, 0, 1, 3, 2, 0, 3, 0xb, 0x12) }, },
	/* 148.5MHz */
	{ 148500, {SNPS_MPLL(0x7a, 0x5800, 0, 1, 2, 3, 0, 4, 0xc, 0x11) }, },
	/* 165MHz */
	{ 165000, {SNPS_MPLL(0x8a, 0xf000, 0, 1, 2, 3, 0, 4, 0xb, 0x11) }, },
	/* 185.625MHz */
	{ 185625, {SNPS_MPLL(0xa0, 0xae00, 0, 1, 2, 2, 0, 4, 0xc, 0x11) }, },
	/* 198MHz */
	{ 198000, {SNPS_MPLL(0xae, 0x2000, 0, 1, 2, 2, 0, 4, 0xc, 0x11) }, },
	/* 297MHz */
	{ 297000, {SNPS_MPLL(0x7a, 0x5800, 0, 1, 1, 3, 0, 4, 0xc, 0x11) }, },
	/* 371.25MHz */
	{ 371250, {SNPS_MPLL(0xa0, 0xae00, 0, 1, 1, 2, 1, 4, 0xc, 0x11) }, },
	/* 396MHz */
	{ 396000, {SNPS_MPLL(0xae, 0x2000, 0, 1, 1, 2, 1, 4, 0xc, 0x11) }, },
	/* 495MHz */
	{ 495000, {SNPS_MPLL(0xe0, 0xe800, 0, 1, 1, 2, 1, 3, 0xb, 0x12) }, },
	/* 594MHz */
	{ 594000, {SNPS_MPLL(0x7a, 0x5800, 0, 1, 0, 3, 1, 4, 0xc, 0x11) }, },
};

static const struct hdmi_pll_snps_mpll_wrapper mpll_list_dc[] = {
	/* 25.2MHz*/
	{ 25200, {SNPS_MPLL(0x62, 0xa000, 0, 1, 4, 3, 0, 4, 0xd, 0x10) }, },
	/* 27MHz */
	{ 27000, {SNPS_MPLL(0x6c, 0x5000, 0, 1, 4, 3, 0, 4, 0xd, 0x11) }, },
	/* 54MHz */
	{ 54000, {SNPS_MPLL(0x6c, 0x5000, 0, 1, 3, 3, 0, 4, 0xd, 0x11) }, },
	/* 59.4MHz */
	{ 59400, {SNPS_MPLL(0x7a, 0x5800, 0, 1, 3, 3, 0, 4, 0xc, 0x11) }, },
	/* 72MHz */
	{ 72000, {SNPS_MPLL(0x9a, 0xc000, 0, 1, 3, 2, 0, 4, 0xd, 0x11) }, },
	/* 74.25MHz */
	{ 74250, {SNPS_MPLL(0xa0, 0xae00, 0, 1, 3, 2, 0, 4, 0xc, 0x11) }, },
	/* 82.5Mhz */
	{ 82500, {SNPS_MPLL(0xb6, 0x6c00, 0, 1, 3, 2, 0, 3, 0xc, 0x11) }, },
	/* 90MHz */
	{ 90000, {SNPS_MPLL(0xca, 0x3000, 0, 1, 3, 2, 0, 3, 0xb, 0x11) }, },
	/* 99MHz */
	{ 99000, {SNPS_MPLL(0xe0, 0xe800, 0, 1, 3, 2, 0, 3, 0xb, 0x12) }, },
	/* 108MHz */
	{ 108000, {SNPS_MPLL(0x6c, 0x5000, 0, 1, 2, 3, 0, 4, 0xd, 0x11) }, },
	/* 118.8MHz */
	{ 118800, {SNPS_MPLL(0x7a, 0x5800, 0, 1, 2, 3, 0, 4, 0xc, 0x11) }, },
	/* 148.5MHz */
	{ 148500, {SNPS_MPLL(0xa0, 0xae00, 0, 1, 2, 2, 0, 4, 0xc, 0x11) }, },
	/* 165MHz */
	{ 165000, {SNPS_MPLL(0xb6, 0x6c00, 0, 1, 2, 2, 0, 3, 0xc, 0x11) }, },
	/* 185.625MHz */
	{ 185625, {SNPS_MPLL(0xd0, 0xd980, 0, 1, 2, 2, 0, 3, 0xb, 0x11) }, },
	/* 198MHz */
	{ 198000, {SNPS_MPLL(0xe0, 0xe800, 0, 1, 2, 2, 0, 3, 0xb, 0x12) }, },
	/* 297MHz */
	{ 297000, {SNPS_MPLL(0xa0, 0xae00, 0, 1, 1, 2, 1, 4, 0xc, 0x11) }, },
	/* 371.25MHz */
	{ 371250, {SNPS_MPLL(0xd0, 0xd980, 0, 1, 1, 2, 1, 3, 0xb, 0x11) }, },
	/* 396MHz */
	{ 396000, {SNPS_MPLL(0xe0, 0xe800, 0, 1, 1, 2, 1, 3, 0xb, 0x12) }, },
};

static const struct hdmi_pll_snps_mpll *hdmi_pll_snps_mpll_lut_decoder(
		u32 pclk_khz, u32 bpp)
{
	const struct hdmi_pll_snps_mpll *mpll = NULL;
	const struct hdmi_pll_snps_mpll_wrapper *arr = mpll_list;
	int i, arr_size = ARRAY_SIZE(mpll_list);

	if (bpp == 30) {
		arr = mpll_list_dc;
		arr_size = ARRAY_SIZE(mpll_list_dc);
	}

	for (i = 0; i < arr_size; i++) {
		if (pclk_khz == arr[i].clk) {
			mpll = &arr[i].mpll;
			HDMI_DEBUG("pclk: %u entry: %i",
					arr[i].clk, i);
			break;
		}
	}

	return mpll;
}

static inline u32 hdmi_pll_snps_operation(u32 value, u32 val, int op)
{
	switch (op) {
	case SET_BIT:
		return value | val;
	case CLEAR_BIT:
		return value & ~val;
	case WRITE:
	default:
		return val;
	}
}

static inline void HDMI_PLL_CFG_REG(struct hdmi_pll *pll, u32 off, u32 val,
				int op)
{
	u32 reg_val;

	reg_val = hdmi_read(pll, off);
	reg_val = hdmi_pll_snps_operation(reg_val, val, op);
	hdmi_write(pll, off, reg_val);
}

static void hdmi_pll_snps_mpll_config(struct hdmi_pll *pll)
{
	struct hdmi_pll_snps_mpll *mpll = pll->mpll;

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MISC_CONTROL_2,
			mpll->rxvcosel << 4, SET_BIT);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MISC_CONTROL_1, 0x11 << 2, SET_BIT);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MISC_CONTROL_0, 0x5, WRITE);
}

static void hdmi_pll_snps_mpll_mnd_config(struct hdmi_pll *pll)
{
	struct hdmi_pll_snps_mpll *mpll = pll->mpll;
	u32 offset = HDMI_PHY_MPLLB_CONTROL_0 - HDMI_PHY_MPLLA_CONTROL_0;

	if (pll->parser->alternate_pll)
		offset = 0;

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_10 + offset,
			(mpll->cp_int_holder & 0xff), WRITE);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_12 + offset,
			(mpll->cp_prop_holder & 0xff), WRITE);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_0 + offset,
			BIT(0), SET_BIT);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_2 + offset,
			(mpll->multiplier & 0xff), WRITE);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_3 + offset,
			((mpll->multiplier >> 8) & 0xff), WRITE);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_0 + offset,
			BIT(2), SET_BIT);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_6 + offset,
			(mpll->quotient & 0xff), WRITE);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_7 + offset,
			((mpll->quotient >> 8) & 0xff), WRITE);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_8 + offset,
			(mpll->remainder & 0xff), WRITE);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_9 + offset,
			((mpll->remainder >> 8) & 0xff), WRITE);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_4 + offset,
			(mpll->denominator & 0xff), WRITE);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_5 + offset,
			((mpll->denominator >> 8) & 0xff), WRITE);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_1 + offset,
			(mpll->tx_clk_div & 0x7), WRITE);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_14 + offset, 0x82, WRITE);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_0 + offset, BIT(6),
				CLEAR_BIT);

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_MPLLA_CONTROL_0 + offset,
			((mpll->vco_freq & 0x3) << 3), SET_BIT);

	HDMI_DEBUG("[OK]");
}

static void hdmi_pll_snps_clk_pattern_en(struct hdmi_pll *pll, bool enable)
{
	int operation = CLEAR_BIT;

	if (enable)
		operation = WRITE;

	HDMI_PLL_CFG_REG(pll, HDMI_PHY_CLOCK_PATTERN_CONFIG_0, BIT(1),
				operation);
}


static int hdmi_pll_snps_mpll_calculator(struct hdmi_pll *pll,
		u32 clock_khz)
{
	const u32 max_unscrambled_clk = 340e3;
	const s64 bit_rate_multiplier = 1000;
	const s64 bit_rate_divider = 1000;
	const s64 cp_int_param[3] = {4524, -679588, 30860643};
	const s64 cp_prop_param[3] = {8023, -1160252, 51717042};
	const u64 gmult = 1e4;

	s64 expon, base_rate, bit_rate;
	s64 ref_clk, ref_clk_int, ref_clk_div;
	u64 aux0, aux1, aux2, vco_freq;
	u64 utemp, utemp2, multiplier, f_rx_vco;
	s64 stemp, stemp2, stemp3;
	s32 choice1, ref_ana_mpll_div;
	s64 precision3 = 1e3;
	s64 precision6 = 1e6;

	struct hdmi_pll_snps_mpll *mpll = pll->mpll;

	/*
	 * Enable clk scrambling if TMDS CLK > 340MHz.
	 */
	if (clock_khz > max_unscrambled_clk)
		mpll->clk_pat = 1;
	else
		mpll->clk_pat = 0;

	/*
	 * base_rate = clock_khz  * bit_rate_multiplier / 1000
	 * Division by 1000, nullifies the conversion from KHz to Hz.
	 */
	base_rate = clock_khz * gmult;

	/*
	 * bit_rate = clock_khz * bit_rate_multiplier/bit_rate_divider.
	 */
	utemp = DIV_ROUND_UP(bit_rate_multiplier, bit_rate_divider);

	bit_rate = (clock_khz * gmult) *
	DIV_ROUND_UP(bit_rate_multiplier, bit_rate_divider);

	ref_clk = REF_CLK_MHZ * 1e6;

	/*
	 * REF_CLK_MHz: 38.4
	 * min_ref_clk: 50
	 * choice1 = floor(log2(REF_CLK_MHz/min_ref_clk))
	 * choice1 = floor(-0.38)
	 */
	choice1 = -1;
	ref_ana_mpll_div = max_t(s32, choice1, 0);

	/*
	 * ref_clk_div = 2**ref_ana_mpll_div;
	 */
	ref_clk_div = 1UL << ref_ana_mpll_div;
	ref_clk_int = DIV_ROUND_UP(ref_clk, ref_clk_div);

	/*
	 * expon = floor(log2(4.999999e9/base_rate))
	 */
	utemp = 4.999999e9;

	expon = DIV_ROUND_CLOSEST(utemp, base_rate) - 1;

	if (expon > 0)
		expon = ilog2(expon);
	else
		expon = -1;

	if (expon >= 0)
		vco_freq = base_rate * (1UL << expon);
	else
		vco_freq = base_rate >> 1;

	/*
	 * multiplier = 2*(floor(bit_rate) * 2**expon / ref_clk_int) -16)
	 */
	multiplier = DIV_ROUND_UP(vco_freq, ref_clk_int);
	multiplier = 2 * (multiplier - 16 - 1);
	mpll->multiplier = multiplier;

	/*
	 * f_rx_vco = bit_rate * 2**floor(log2(6e9/bit_rate));
	 */
	utemp = 6e9;
	f_rx_vco = DIV_ROUND_UP(utemp, bit_rate);
	f_rx_vco = ilog2(f_rx_vco);
	f_rx_vco = bit_rate * (1UL << f_rx_vco);

	utemp = 4e9;
	if (f_rx_vco < utemp)
		f_rx_vco *= 2;

	utemp = 5e9;
	if (f_rx_vco > utemp)
		mpll->rxvcosel = 4;
	else
		mpll->rxvcosel = 3;


	/*
	 * aux0 = (bit_rate * 2**expon / ref_clk_int - (multiplier/2 + 16);
	 * aux0 = aux0 * 2** 16;
	 */
	if (expon >= 0)
		utemp = bit_rate * (1UL << expon);
	else
		utemp = bit_rate >> 1;

	/*
	 * Need to store the floating-point values as they are
	 * essential for precise computations; thus, increasing
	 * the precision by 10^6.
	 */

	utemp *= precision6;
	utemp = DIV_ROUND_UP(utemp, ref_clk_int);
	aux0 = utemp - (((multiplier >> 1) + 16)) * precision6;
	mpll->quotient = (aux0 * (1UL << 16)) / precision6;

	/*
	 * aux1 = ref_clk_int * bit_rate_divider
	 */
	aux1 = ref_clk_int * bit_rate_divider;

	/*
	 * aux2 = bit_rate * bit_rate_divider * 2**expon;
	 * aux2 = aux2 - ((multiplier/2 + 16) * aux1 * 2**16);
	 * aux2 = aux2 - aux1 *aux0;
	 */

	if (expon >= 0)
		aux2 = bit_rate * (1UL << expon);
	else
		aux2 = bit_rate >> 1;

	aux2 *= bit_rate_divider;
	utemp = ((multiplier >> 1) + 16) * aux1;
	aux2 = ((aux2 - utemp) * (1UL << 16)) - (mpll->quotient * aux1);

	if (aux2 == 0) {
		mpll->remainder = 0;
		mpll->denominator = 1;
	} else {
		mpll->remainder = DIV_ROUND_UP(aux2,
							gcd(aux1, aux2));
		mpll->denominator = DIV_ROUND_UP(aux1,
							gcd(aux1, aux2));
	}

	/*
	 * tx_clk_div = log2(round(vco_freq/base_rate) * 2)
	 */
	utemp = DIV_ROUND_UP(vco_freq, base_rate);
	mpll->tx_clk_div = ilog2(utemp * 2);

	utemp = 3.389e9;
	utemp2 = 5e9;
	if (vco_freq <= utemp)
		mpll->vco_freq = 2;
	else if (vco_freq < utemp2)
		mpll->vco_freq = 3;
	else {
		HDMI_ERR("bit_rate and vco_freq combination not supported: %llu",
					vco_freq);
	return -EINVAL;
	}

	/*
	 * stemp = (ref_clk_mhz/2**ref_ana_mpll_div)
	 */
	stemp = REF_CLK_MHZ * precision3;
	stemp = DIV_ROUND_CLOSEST(stemp, 1UL << ref_ana_mpll_div);

	/*
	 * stemp2 = cp_int_param[0] * stemp**2;
	 */
	stemp2 = stemp * stemp * cp_int_param[0];
	stemp2 = DIV_ROUND_CLOSEST(stemp2, precision6);

	/*
	 * stemp3 = cp_int_param[1] * stemp;
	 */
	stemp3 = stemp * cp_int_param[1];
	stemp3 /= precision3;

	/*
	 * cp_int_holder = floor(cp_int_param[0]*(stemp)**2
	 *                      + cp_int_param[1] * stemp
	 *                      + cp_int_param[2])
	 */
	stemp3 += cp_int_param[2];
	stemp3 += stemp2;

	mpll->cp_int_holder = DIV_ROUND_CLOSEST(stemp3, precision6);

	/*
	 * stemp2 = cp_prop_param[0] * stemp**2;
	 */
	stemp2 = stemp * stemp * cp_prop_param[0];
	stemp2 = DIV_ROUND_CLOSEST(stemp2, precision6);

	/*
	 * stemp3 = cp_prop_param[1] * stemp;
	 */
	stemp3 = (stemp * cp_prop_param[1]) / precision3;

	/*
	 * cp_int_holder = floor(cp_prop_param[0]*(stemp)**2
	 *                      + cp_prop_param[1] * stemp
	 *                      + cp_prop_param[2])
	 */
	stemp3 += stemp2 + cp_prop_param[2];

	mpll->cp_prop_holder = DIV_ROUND_CLOSEST(stemp3, precision6) - 1;

	return 0;
}

static unsigned long hdmi_pll_vco_div_clk_recalc_rate(struct clk_hw *hw,
							unsigned long parent_rate)
{
	struct hdmi_pll *pll;
	struct hdmi_pll_vco_clk *pll_vco;

	if (!hw) {
		HDMI_ERR("invalid input parameters");
		return -EINVAL;
	}

	pll_vco = to_hdmi_vco_hw(hw);
	pll = pll_vco->priv;

	return pll->vco_rate;
}

static long hdmi_pll_vco_div_clk_round(struct clk_hw *hw,
		unsigned long rate, unsigned long *parent_rate)
{
	return hdmi_pll_vco_div_clk_recalc_rate(hw, *parent_rate);
}

/*
 * Adding this redundant function to better understand
 * the pll configuration process, and make it inline/similar
 * to DRM configuration framework.
 */
static inline void hdmi_pll_snps_set_rate(struct hdmi_pll *pll)
{
	hdmi_pll_snps_mpll_mnd_config(pll);

	/*
	 * Set the pixel clock sources so that the
	 * linux clock framework can appropriately
	 * compute the MND values when the pixel
	 * clock is set.
	 */
	clk_set_rate(pll->clk_data->clks[0], pll->vco_rate);
}

/*
 * By default pixel clock would be referred in a LUT,
 * if the required pixel clock entry differs from the
 * LUT entry, m/n/d register configuration parameters
 * will be calculated using the mpll calculator.
 */
static int hdmi_pll_snps_configure(struct hdmi_pll *pll,
		u32 clock_khz, u32 bpp)
{
	int rc = 0;
	const struct hdmi_pll_snps_mpll *lut_dec;
	struct hdmi_pll_snps_mpll *mpll;

	lut_dec = hdmi_pll_snps_mpll_lut_decoder(clock_khz, bpp);

	if (!lut_dec) {
		HDMI_WARN("no valid mpll entry fall back to calculator");

		rc = hdmi_pll_snps_mpll_calculator(pll, clock_khz);
		if (!rc) {
			goto config;
		} else {
			HDMI_ERR("pll calculation failed");
			return rc;
		}
	} else {
		memcpy(pll->mpll, lut_dec, sizeof(*lut_dec));
	}

	mpll = pll->mpll;

	HDMI_DEBUG("TMDS: VCOFREQ|RXVCOSEL|TX_CLK_DIV|INT_HOLDER|PROP_HOLDER");
	HDMI_DEBUG("%u kHz: 0x%x|0x%x|0x%x|0x%x|0x%x",
			clock_khz, mpll->vco_freq, mpll->rxvcosel,
			mpll->tx_clk_div, mpll->cp_int_holder,
			mpll->cp_prop_holder);
	HDMI_DEBUG("quotient: 0x%x|remainder: 0x%x",
			mpll->quotient,
			mpll->remainder);
	HDMI_DEBUG("multiplier: 0x%x|denominator: 0x%x",
			mpll->multiplier,
			mpll->denominator);
	HDMI_DEBUG("clk_pat: 0x%x", mpll->clk_pat);


config:
	hdmi_pll_snps_mpll_config(pll);
	pll->vco_rate = clock_khz * pll->clk_factor;
	hdmi_pll_snps_set_rate(pll);

	return rc;
}

static int hdmi_pll_snps_prepare(struct hdmi_pll *pll)
{
	if (!pll) {
		HDMI_ERR("invalid input");
		return -EINVAL;
	}

	hdmi_pll_snps_clk_pattern_en(pll, true);
	return 0;
}

static int hdmi_pll_snps_unprepare(struct hdmi_pll *pll)
{
	if (!pll) {
		HDMI_ERR("invalid input");
		return -EINVAL;
	}

	hdmi_pll_snps_clk_pattern_en(pll, false);
	pll->vco_rate = 0;

	return 0;
}

static const struct clk_ops hdmi_phy_pll_clk_ops = {
	.recalc_rate    = hdmi_pll_vco_div_clk_recalc_rate,
	.round_rate     = hdmi_pll_vco_div_clk_round,
};

static struct clk_init_data phy_pll_clks[HDMI_PLL_NUM_CLKS] = {
	{
		.name	= "_phy_pll_clk",
		.ops	= &hdmi_phy_pll_clk_ops,
	},
};

static struct hdmi_pll_vco_clk *hdmi_pll_get_clks(struct hdmi_pll *pll)
{
	int i;

	for (i = 0; i < HDMI_PLL_NUM_CLKS; i++) {
		snprintf(pll->pll_clks[i].name, HDMI_PLL_NAME_MAX_SIZE,
				"%s%s", pll->name, phy_pll_clks[i].name);
		pll->pll_clks[i].init_data.name = pll->pll_clks[i].name;
		pll->pll_clks[i].init_data.ops = phy_pll_clks[i].ops;
		pll->pll_clks[i].hw.init = &pll->pll_clks[i].init_data;
	}

	return pll->pll_clks;
}

int hdmi_pll_snps_register(struct hdmi_pll *pll)
{
	int rc = 0;
	struct hdmi_pll_vco_clk *pll_clks;
	struct platform_device *pdev;

	if (!pll) {
		HDMI_ERR("invalid input: %i", -EINVAL);
		return -EINVAL;
	}

	pdev = pll->pdev;

	pll->mpll = kzalloc(sizeof(struct hdmi_pll_snps_mpll), GFP_KERNEL);
	if (!pll->mpll) {
		HDMI_ERR("mpll memory allocation failed");
		return -ENOMEM;
	}

	pll->clk_data = kzalloc(sizeof(*pll->clk_data), GFP_KERNEL);
	if (!pll->clk_data) {
		HDMI_ERR("pll clk data memory allocation failed");
		rc = -ENOMEM;
		goto err_clk_data;
	}

	pll->clk_data->clks = kcalloc(HDMI_PLL_NUM_CLKS,
			sizeof(struct clk *), GFP_KERNEL);
	if (!pll->clk_data->clks) {
		HDMI_ERR("clk memory allocation failed");
		rc = -ENOMEM;
		goto err_clk;
	}

	pll->config	= hdmi_pll_snps_configure,
	pll->prepare	= hdmi_pll_snps_prepare,
	pll->unprepare	= hdmi_pll_snps_unprepare,

	pll->clk_data->clk_num = HDMI_PLL_NUM_CLKS;
	pll_clks = hdmi_pll_get_clks(pll);

	rc = hdmi_pll_clock_register_helper(pll, pll_clks, HDMI_PLL_NUM_CLKS);
	if (rc) {
		HDMI_ERR("Clock registration failed: %i", rc);
		goto clk_reg_fail;
	}

	rc = of_clk_add_provider(pdev->dev.of_node,
			of_clk_src_onecell_get,
			pll->clk_data);
	if (rc) {
		HDMI_ERR("Clock add provider failed rc: %i", rc);
		goto clk_reg_fail;
	}

	HDMI_DEBUG("success");
	return rc;

err_clk:
	kfree(pll->clk_data);
err_clk_data:
	kfree(pll->mpll);
clk_reg_fail:
	hdmi_pll_snps_unregister(pll);
	return rc;
}

void hdmi_pll_snps_unregister(struct hdmi_pll *pll)
{
	if (!pll) {
		HDMI_ERR("invalid input");
		return;
	}

	kfree(pll->mpll);
	kfree(pll->clk_data->clks);
	kfree(pll->clk_data);
	HDMI_DEBUG("complete");
}

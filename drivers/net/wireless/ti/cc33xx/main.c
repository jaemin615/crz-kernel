// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2024 Texas Instruments Incorporated - https://www.ti.com/
 */

#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/irq.h>
#include <linux/pm_wakeirq.h>

#include "../net/mac80211/ieee80211_i.h"

#include "acx.h"
#include "boot.h"
#include "io.h"
#include "tx.h"
#include "ps.h"
#include "init.h"
#include "testmode.h"
#include "scan.h"
#include "event.h"

#define CC33XX_FW_RX_PACKET_RAM (9 * 1024)
static int no_recovery     = -1;

u32 cc33xx_debug_level = DEBUG_NO_DATAPATH;

/* HT cap appropriate for wide channels in 2Ghz */
static struct ieee80211_sta_ht_cap cc33xx_siso40_ht_cap_2ghz = {
	.cap = IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40 |
	       IEEE80211_HT_CAP_SUP_WIDTH_20_40 | IEEE80211_HT_CAP_DSSSCCK40 |
	       IEEE80211_HT_CAP_GRN_FLD,
	.ht_supported = true,
	.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
	.mcs = {
		.rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		.rx_highest = cpu_to_le16(150),
		.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
	},
};

/* HT cap appropriate for wide channels in 5Ghz */
static struct ieee80211_sta_ht_cap cc33xx_siso40_ht_cap_5ghz = {
	.cap = IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40 |
	       IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
	       IEEE80211_HT_CAP_GRN_FLD,
	.ht_supported = true,
	.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
	.mcs = {
		.rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		.rx_highest = cpu_to_le16(150),
		.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
	},
};

/* HT cap appropriate for SISO 20 */
static struct ieee80211_sta_ht_cap cc33xx_siso20_ht_cap = {
	.cap = IEEE80211_HT_CAP_SGI_20 |
	       IEEE80211_HT_CAP_MAX_AMSDU,
	.ht_supported = true,
	.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
	.mcs = {
		.rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
		.rx_highest = cpu_to_le16(72),
		.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
	},
};

#ifdef CONFIG_MAC80211_MESH
static const struct ieee80211_iface_limit cc33xx_iface_limits[] = {
	{
		.max = 2,
		.types = BIT(NL80211_IFTYPE_STATION)
		       | BIT(NL80211_IFTYPE_P2P_CLIENT),
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_AP) | BIT(NL80211_IFTYPE_P2P_GO)
		       | BIT(NL80211_IFTYPE_MESH_POINT)
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_P2P_DEVICE),
	},
};

static inline u16 cc33xx_wiphy_interface_modes(void)
{
	return BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_P2P_GO) |
	       BIT(NL80211_IFTYPE_MESH_POINT) | BIT(NL80211_IFTYPE_AP) |
	       BIT(NL80211_IFTYPE_P2P_CLIENT) | BIT(NL80211_IFTYPE_P2P_DEVICE);
}
#else
static const struct ieee80211_iface_limit cc33xx_iface_limits[] = {
	{
		.max = 2,
		.types = BIT(NL80211_IFTYPE_STATION)
		       | BIT(NL80211_IFTYPE_P2P_CLIENT),
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_AP) | BIT(NL80211_IFTYPE_P2P_GO)
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_P2P_DEVICE),
	},
};

static inline u16 cc33xx_wiphy_interface_modes(void)
{
	return BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_P2P_GO) |
	       BIT(NL80211_IFTYPE_P2P_CLIENT) | BIT(NL80211_IFTYPE_AP) |
	       BIT(NL80211_IFTYPE_P2P_DEVICE);
}
#endif /* CONFIG_MAC80211_MESH */

static const struct ieee80211_iface_combination
cc33xx_iface_combinations[] = {
	{
		.max_interfaces = 3,
		.limits = cc33xx_iface_limits,
		.n_limits = ARRAY_SIZE(cc33xx_iface_limits),
		.num_different_channels = 2,
	}
};

static const u8 cc33xx_rate_to_idx_2ghz[] = {
	CONF_HW_RXTX_RATE_UNSUPPORTED,
	0,              /* RATE_INDEX_1MBPS */
	1,              /* RATE_INDEX_2MBPS */
	2,              /* RATE_INDEX_5_5MBPS */
	3,              /* RATE_INDEX_11MBPS */
	4,              /* RATE_INDEX_6MBPS */
	5,              /* RATE_INDEX_9MBPS */
	6,              /* RATE_INDEX_12MBPS */
	7,              /* RATE_INDEX_18MBPS */
	8,              /* RATE_INDEX_24MBPS */
	9,              /* RATE_INDEX_36MBPS */
	10,            /* RATE_INDEX_48MBPS */
	11,            /* RATE_INDEX_54MBPS */
	0,              /* RATE_INDEX_MCS0 */
	1,              /* RATE_INDEX_MCS1 */
	2,              /* RATE_INDEX_MCS2 */
	3,              /* RATE_INDEX_MCS3 */
	4,              /* RATE_INDEX_MCS4 */
	5,              /* RATE_INDEX_MCS5 */
	6,              /* RATE_INDEX_MCS6 */
	7               /* RATE_INDEX_MCS7 */
};

static const u8 cc33xx_rate_to_idx_5ghz[] = {
	CONF_HW_RXTX_RATE_UNSUPPORTED,
	CONF_HW_RXTX_RATE_UNSUPPORTED,              /* RATE_INDEX_1MBPS */
	CONF_HW_RXTX_RATE_UNSUPPORTED,              /* RATE_INDEX_2MBPS */
	CONF_HW_RXTX_RATE_UNSUPPORTED,              /* RATE_INDEX_5_5MBPS */
	CONF_HW_RXTX_RATE_UNSUPPORTED,              /* RATE_INDEX_11MBPS */
	0,              /* RATE_INDEX_6MBPS */
	1,              /* RATE_INDEX_9MBPS */
	2,              /* RATE_INDEX_12MBPS */
	3,              /* RATE_INDEX_18MBPS */
	4,              /* RATE_INDEX_24MBPS */
	5,              /* RATE_INDEX_36MBPS */
	6,              /* RATE_INDEX_48MBPS */
	7,              /* RATE_INDEX_54MBPS */
	0,              /* RATE_INDEX_MCS0 */
	1,              /* RATE_INDEX_MCS1 */
	2,              /* RATE_INDEX_MCS2 */
	3,              /* RATE_INDEX_MCS3 */
	4,              /* RATE_INDEX_MCS4 */
	5,              /* RATE_INDEX_MCS5 */
	6,              /* RATE_INDEX_MCS6 */
	7               /* RATE_INDEX_MCS7 */
};

static const u8 *cc33xx_band_rate_to_idx[] = {
	[NL80211_BAND_2GHZ] = cc33xx_rate_to_idx_2ghz,
	[NL80211_BAND_5GHZ] = cc33xx_rate_to_idx_5ghz
};

/* can't be const, mac80211 writes to this */
static struct ieee80211_rate cc33xx_rates[] = {
	{ .bitrate = 10,
	  .hw_value = CONF_HW_BIT_RATE_1MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_1MBPS, },
	{ .bitrate = 20,
	  .hw_value = CONF_HW_BIT_RATE_2MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_2MBPS,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55,
	  .hw_value = CONF_HW_BIT_RATE_5_5MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_5_5MBPS,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110,
	  .hw_value = CONF_HW_BIT_RATE_11MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_11MBPS,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60,
	  .hw_value = CONF_HW_BIT_RATE_6MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_6MBPS, },
	{ .bitrate = 90,
	  .hw_value = CONF_HW_BIT_RATE_9MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_9MBPS, },
	{ .bitrate = 120,
	  .hw_value = CONF_HW_BIT_RATE_12MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_12MBPS, },
	{ .bitrate = 180,
	  .hw_value = CONF_HW_BIT_RATE_18MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_18MBPS, },
	{ .bitrate = 240,
	  .hw_value = CONF_HW_BIT_RATE_24MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_24MBPS, },
	{ .bitrate = 360,
	 .hw_value = CONF_HW_BIT_RATE_36MBPS,
	 .hw_value_short = CONF_HW_BIT_RATE_36MBPS, },
	{ .bitrate = 480,
	  .hw_value = CONF_HW_BIT_RATE_48MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_48MBPS, },
	{ .bitrate = 540,
	  .hw_value = CONF_HW_BIT_RATE_54MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_54MBPS, },
};

/* can't be const, mac80211 writes to this */
static struct ieee80211_channel cc33xx_channels[] = {
	{ .hw_value = 1, .center_freq = 2412, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 2, .center_freq = 2417, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 3, .center_freq = 2422, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 4, .center_freq = 2427, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 5, .center_freq = 2432, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 6, .center_freq = 2437, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 7, .center_freq = 2442, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 8, .center_freq = 2447, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 9, .center_freq = 2452, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 10, .center_freq = 2457, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 11, .center_freq = 2462, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 12, .center_freq = 2467, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 13, .center_freq = 2472, .max_power = CC33XX_MAX_TXPWR },
};

static struct ieee80211_sband_iftype_data iftype_data_2ghz[] = {{
	.types_mask = BIT(NL80211_IFTYPE_STATION),
	.he_cap = {
		.has_he = true,
		.he_cap_elem = {
		.mac_cap_info[0] =
			IEEE80211_HE_MAC_CAP0_HTC_HE |
			IEEE80211_HE_MAC_CAP0_TWT_REQ,
		.mac_cap_info[1] =
			IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
			IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
		.mac_cap_info[2] =
			IEEE80211_HE_MAC_CAP2_32BIT_BA_BITMAP |
			IEEE80211_HE_MAC_CAP2_ALL_ACK |
			IEEE80211_HE_MAC_CAP2_TRS |
			IEEE80211_HE_MAC_CAP2_BSR |
			IEEE80211_HE_MAC_CAP2_ACK_EN,
		.mac_cap_info[3] =
			IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
			IEEE80211_HE_MAC_CAP3_RX_CTRL_FRAME_TO_MULTIBSS,
		.mac_cap_info[4] =
			IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU |
			IEEE80211_HE_MAC_CAP4_NDP_FB_REP |
			IEEE80211_HE_MAC_CAP4_MULTI_TID_AGG_TX_QOS_B39,
		.mac_cap_info[5] =
			IEEE80211_HE_MAC_CAP5_HT_VHT_TRIG_FRAME_RX,
		.phy_cap_info[0] = 0,
		.phy_cap_info[1] =
			IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
			IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US,
		.phy_cap_info[2] =
			IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US,
		.phy_cap_info[3] =
			IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_NO_DCM |
			IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1 |
			IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM |
			IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1,
		.phy_cap_info[4] =
			IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |
			IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4,
		.phy_cap_info[5] =
			IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK |
			IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK,
		.phy_cap_info[6] =
			IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU  |
			IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU  |
			IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB  |
			IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB |
			IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB |
			IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE,
		.phy_cap_info[7] =
			IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI,
		.phy_cap_info[8] =
			IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
			IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G |
			IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI,
		.phy_cap_info[9] =
			IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK |
			IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
			IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB |
			IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US,
		},
		/* Set default Tx/Rx HE MCS NSS Support field.
		 * Indicate support for up to 2 spatial streams and all
		 * MCS, without any special cases
		 */
		.he_mcs_nss_supp = {
			.rx_mcs_80 = cpu_to_le16(0xfffc),
			.tx_mcs_80 = cpu_to_le16(0xfffc),
			.rx_mcs_160 = cpu_to_le16(0xffff),
			.tx_mcs_160 = cpu_to_le16(0xffff),
			.rx_mcs_80p80 = cpu_to_le16(0xffff),
			.tx_mcs_80p80 = cpu_to_le16(0xffff),
		},
		/* Set default PPE thresholds, with PPET16 set to 0,
		 * PPET8 set to 7
		 */
		.ppe_thres = {0xff, 0xff, 0xff, 0xff},
	},
}};

/* can't be const, mac80211 writes to this */
static struct ieee80211_supported_band cc33xx_band_2ghz = {
	.channels = cc33xx_channels,
	.n_channels = ARRAY_SIZE(cc33xx_channels),
	.bitrates = cc33xx_rates,
	.n_bitrates = ARRAY_SIZE(cc33xx_rates),
	.iftype_data = iftype_data_2ghz,
	.n_iftype_data = ARRAY_SIZE(iftype_data_2ghz),
};

/* 5 GHz data rates for cc33xx */
static struct ieee80211_rate cc33xx_rates_5ghz[] = {
	{ .bitrate = 60,
	  .hw_value = CONF_HW_BIT_RATE_6MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_6MBPS, },
	{ .bitrate = 90,
	  .hw_value = CONF_HW_BIT_RATE_9MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_9MBPS, },
	{ .bitrate = 120,
	  .hw_value = CONF_HW_BIT_RATE_12MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_12MBPS, },
	{ .bitrate = 180,
	  .hw_value = CONF_HW_BIT_RATE_18MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_18MBPS, },
	{ .bitrate = 240,
	  .hw_value = CONF_HW_BIT_RATE_24MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_24MBPS, },
	{ .bitrate = 360,
	 .hw_value = CONF_HW_BIT_RATE_36MBPS,
	 .hw_value_short = CONF_HW_BIT_RATE_36MBPS, },
	{ .bitrate = 480,
	  .hw_value = CONF_HW_BIT_RATE_48MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_48MBPS, },
	{ .bitrate = 540,
	  .hw_value = CONF_HW_BIT_RATE_54MBPS,
	  .hw_value_short = CONF_HW_BIT_RATE_54MBPS, },
};

/* 5 GHz band channels for cc33xx */
static struct ieee80211_channel cc33xx_channels_5ghz[] = {
	{ .hw_value = 36, .center_freq = 5180, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 40, .center_freq = 5200, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 44, .center_freq = 5220, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 48, .center_freq = 5240, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 52, .center_freq = 5260, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 56, .center_freq = 5280, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 60, .center_freq = 5300, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 64, .center_freq = 5320, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 100, .center_freq = 5500, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 104, .center_freq = 5520, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 108, .center_freq = 5540, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 112, .center_freq = 5560, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 116, .center_freq = 5580, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 120, .center_freq = 5600, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 124, .center_freq = 5620, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 128, .center_freq = 5640, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 132, .center_freq = 5660, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 136, .center_freq = 5680, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 140, .center_freq = 5700, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 149, .center_freq = 5745, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 153, .center_freq = 5765, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 157, .center_freq = 5785, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 161, .center_freq = 5805, .max_power = CC33XX_MAX_TXPWR },
	{ .hw_value = 165, .center_freq = 5825, .max_power = CC33XX_MAX_TXPWR },
};

static struct ieee80211_sband_iftype_data iftype_data_5ghz[] = {{
	.types_mask = BIT(NL80211_IFTYPE_STATION),
	.he_cap = {
		.has_he = true,
		.he_cap_elem = {
		.mac_cap_info[0] =
			IEEE80211_HE_MAC_CAP0_HTC_HE |
			IEEE80211_HE_MAC_CAP0_TWT_REQ,
		.mac_cap_info[1] =
			IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
			IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
		.mac_cap_info[2] =
			IEEE80211_HE_MAC_CAP2_32BIT_BA_BITMAP |
			IEEE80211_HE_MAC_CAP2_ALL_ACK |
			IEEE80211_HE_MAC_CAP2_TRS |
			IEEE80211_HE_MAC_CAP2_BSR |
			IEEE80211_HE_MAC_CAP2_ACK_EN,
		.mac_cap_info[3] =
			IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
			IEEE80211_HE_MAC_CAP3_RX_CTRL_FRAME_TO_MULTIBSS,
		.mac_cap_info[4] =
			IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU |
			IEEE80211_HE_MAC_CAP4_NDP_FB_REP |
			IEEE80211_HE_MAC_CAP4_MULTI_TID_AGG_TX_QOS_B39,
		.mac_cap_info[5] =
			IEEE80211_HE_MAC_CAP5_HT_VHT_TRIG_FRAME_RX,
		.phy_cap_info[0] = 0,
		.phy_cap_info[1] =
			IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
			IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US,
		.phy_cap_info[2] =
			IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US,
		.phy_cap_info[3] =
			IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_NO_DCM |
			IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1 |
			IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM |
			IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1,
		.phy_cap_info[4] =
			IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |
			IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4,
		.phy_cap_info[5] =
			IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK |
			IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK,
		.phy_cap_info[6] =
			IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU  |
			IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU  |
			IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB  |
			IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB |
			IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB |
			IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE,
		.phy_cap_info[7] =
			IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI,
		.phy_cap_info[8] =
			IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
			IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G |
			IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI,
		.phy_cap_info[9] =
			IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK |
			IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
			IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB |
			IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US,
		},
		/* Set default Tx/Rx HE MCS NSS Support field.
		 * Indicate support for up to 2 spatial streams and all
		 * MCS, without any special cases
		 */
		.he_mcs_nss_supp = {
			.rx_mcs_80 = cpu_to_le16(0xfffc),
			.tx_mcs_80 = cpu_to_le16(0xfffc),
			.rx_mcs_160 = cpu_to_le16(0xffff),
			.tx_mcs_160 = cpu_to_le16(0xffff),
			.rx_mcs_80p80 = cpu_to_le16(0xffff),
			.tx_mcs_80p80 = cpu_to_le16(0xffff),
		},
		/* Set default PPE thresholds, with PPET16 set to 0,
		 * PPET8 set to 7
		 */
		.ppe_thres = {0xff, 0xff, 0xff, 0xff},
	},
}};

static struct ieee80211_supported_band cc33xx_band_5ghz = {
	.channels = cc33xx_channels_5ghz,
	.n_channels = ARRAY_SIZE(cc33xx_channels_5ghz),
	.bitrates = cc33xx_rates_5ghz,
	.n_bitrates = ARRAY_SIZE(cc33xx_rates_5ghz),
	.vht_cap = {
		.vht_supported = true,
		.cap = (IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991 | (1 <<
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT)),
		.vht_mcs = {
			.rx_mcs_map = cpu_to_le16(0xfffc),
			.rx_highest = cpu_to_le16(7),
			.tx_mcs_map = cpu_to_le16(0xfffc),
			.tx_highest = cpu_to_le16(7),
		},
	},
	.iftype_data = iftype_data_5ghz,
	.n_iftype_data = ARRAY_SIZE(iftype_data_5ghz),

};

static void __cc33xx_op_remove_interface(struct cc33xx *cc,
					 struct ieee80211_vif *vif,
					 bool reset_tx_queues);
static void cc33xx_turn_off(struct cc33xx *cc);
static void cc33xx_free_ap_keys(struct cc33xx *cc, struct cc33xx_vif *wlvif);
static int process_core_status(struct cc33xx *cc,
			       struct core_status *core_status);
static int cc33xx_setup(struct cc33xx *cc);

static int cc33xx_set_authorized(struct cc33xx *cc, struct cc33xx_vif *wlvif)
{
	int ret;

	if (WARN_ON(wlvif->bss_type != BSS_TYPE_STA_BSS))
		return -EINVAL;

	if (!test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
		return 0;

	if (test_and_set_bit(WLVIF_FLAG_STA_STATE_SENT, &wlvif->flags))
		return 0;

	ret = cc33xx_cmd_set_peer_state(cc, wlvif, wlvif->sta.hlid);
	if (ret < 0)
		return ret;

	cc33xx_info("Association complete.");
	return 0;
}

/* cc->mutex must be taken */
void cc33xx_rearm_tx_watchdog_locked(struct cc33xx *cc)
{
	/* if the watchdog is not armed, don't do anything */
	if (cc->tx_allocated_blocks == 0)
		return;

	cancel_delayed_work(&cc->tx_watchdog_work);
	ieee80211_queue_delayed_work(cc->hw, &cc->tx_watchdog_work,
				     msecs_to_jiffies(cc->conf.host_conf.tx.tx_watchdog_timeout));
}

static void cc33xx_sta_rc_update(struct cc33xx *cc, struct cc33xx_vif *wlvif)
{
	bool wide = wlvif->rc_update_bw >= IEEE80211_STA_RX_BW_40;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 sta_rc_update wide %d", wide);

	/* sanity */
	if (WARN_ON(wlvif->bss_type != BSS_TYPE_STA_BSS))
		return;

	/* ignore the change before association */
	if (!test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
		return;

	/* If we started out as wide, we can change the operation mode. If we
	 * thought this was a 20mhz AP, we have to reconnect
	 */
	if (wlvif->sta.role_chan_type != NL80211_CHAN_HT40MINUS &&
	    wlvif->sta.role_chan_type != NL80211_CHAN_HT40PLUS)
		ieee80211_connection_loss(cc33xx_wlvif_to_vif(wlvif));
}

static void cc33xx_rc_update_work(struct work_struct *work)
{
	struct cc33xx_vif *wlvif = container_of(work, struct cc33xx_vif,
						rc_update_work);
	struct cc33xx *cc = wlvif->cc;
	struct ieee80211_vif *vif = cc33xx_wlvif_to_vif(wlvif);

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	if (!ieee80211_vif_is_mesh(vif))
		cc33xx_sta_rc_update(cc, wlvif);

out:
	mutex_unlock(&cc->mutex);
}

static inline void cc33xx_tx_watchdog_work(struct work_struct *work)
{
	container_of(to_delayed_work(work), struct cc33xx, tx_watchdog_work);
}

static void cc33xx_adjust_conf(struct cc33xx *cc)
{
	if (no_recovery != -1)
		cc->conf.core.no_recovery = (u8)no_recovery;
}

void cc33xx_flush_deferred_work(struct cc33xx *cc)
{
	struct sk_buff *skb;

	/* Pass all received frames to the network stack */
	while ((skb = skb_dequeue(&cc->deferred_rx_queue))) {
		cc33xx_debug(DEBUG_RX, "%s: rx skb 0x%p", __func__, skb);
		ieee80211_rx_ni(cc->hw, skb);
	}

	/* Return sent skbs to the network stack */
	while ((skb = skb_dequeue(&cc->deferred_tx_queue)))
		ieee80211_tx_status_ni(cc->hw, skb);
}

static void cc33xx_netstack_work(struct work_struct *work)
{
	struct cc33xx *cc = container_of(work, struct cc33xx, netstack_work);

	do {
		cc33xx_flush_deferred_work(cc);
	} while (skb_queue_len(&cc->deferred_rx_queue));
}

static int cc33xx_irq_locked(struct cc33xx *cc)
{
	int ret = 0;
	struct core_status *core_status_ptr;
	u8 *rx_buf_ptr;
	u16 rx_buf_len;
	size_t read_data_len;
	const size_t maximum_rx_packet_size = CC33XX_FW_RX_PACKET_RAM;
	size_t rx_byte_count;
	struct NAB_rx_header *NAB_rx_header;

	process_deferred_events(cc);

	claim_core_status_lock(cc);

	rx_byte_count = (le32_to_cpu(cc->core_status->rx_status) & RX_BYTE_COUNT_MASK);
	if (rx_byte_count != 0) {
		const int read_headers_len = sizeof(struct core_status)
			+ sizeof(struct NAB_rx_header);

		/* Read aggressively as more data might be coming in */
		rx_byte_count *= 2;

		read_data_len = rx_byte_count + read_headers_len;

		if (cc->max_transaction_len) { /* Used in SPI interface */
			const int spi_alignment = sizeof(u32) - 1;

			read_data_len = __ALIGN_MASK(read_data_len,
						     spi_alignment);
			read_data_len = min(read_data_len,
					    cc->max_transaction_len);
		} else { /* SDIO */
			const int sdio_alignment = CC33XX_BUS_BLOCK_SIZE - 1;

			read_data_len = __ALIGN_MASK(read_data_len,
						     sdio_alignment);
			read_data_len = min(read_data_len,
					    maximum_rx_packet_size);
		}

		ret = cc33xx_raw_read(cc, NAB_DATA_ADDR, cc->aggr_buf,
				      read_data_len, true);
		if (ret < 0) {
			cc33xx_debug(DEBUG_IRQ,
				     "rx read Error response 0x%x", ret);
			release_core_status_lock(cc);
			return ret;
		}

		core_status_ptr = (struct core_status *)((u8 *)cc->aggr_buf +
				    read_data_len - sizeof(struct core_status));

		memcpy(cc->core_status,
		       core_status_ptr, sizeof(struct core_status));

		process_core_status(cc, cc->core_status);

		release_core_status_lock(cc);

		cc33xx_debug(DEBUG_IRQ, "read rx data 0x%x", ret);
		NAB_rx_header = (struct NAB_rx_header *)cc->aggr_buf;
		rx_buf_len = le16_to_cpu(NAB_rx_header->len) - 8;
		if (rx_buf_len != 0) {
			rx_buf_ptr = (u8 *)cc->aggr_buf + sizeof(struct NAB_rx_header);
			cc33xx_rx(cc, rx_buf_ptr, rx_buf_len);
		} else {
			cc33xx_error("Rx buffer length is 0");
			cc33xx_queue_recovery_work(cc);
		}
	} else {
		cc33xx_debug(DEBUG_IRQ, "IRQ locked work: No rx data, releasing core-status lock");
		release_core_status_lock(cc);
	}

	cc33xx_tx_immediate_complete(cc);

	return ret;
}

static int read_core_status(struct cc33xx *cc, struct core_status *core_status)
{
	cc33xx_debug(DEBUG_CORE_STATUS, "Reading core status");

	return cc33xx_raw_read(cc, NAB_STATUS_ADDR, core_status,
			       sizeof(*core_status), false);
}

#define CTRL_TYPE_BITS 4
static int get_type(struct control_info_descriptor *control_info_descriptor)
{
	u16 type_mask = GENMASK(CTRL_TYPE_BITS - 1, 0);

	return le16_to_cpu(control_info_descriptor->type_and_length) & type_mask;
}

static unsigned int get_length(struct control_info_descriptor *control_info_descriptor)
{
	return le16_to_cpu(control_info_descriptor->type_and_length) >> CTRL_TYPE_BITS;
}

static int parse_control_message(struct cc33xx *cc,
				 const u8 *buffer, size_t buffer_length)
{
	u8 *const end_of_payload = (u8 *const)buffer + buffer_length;
	u8 *const start_of_payload = (u8 *const)buffer;
	struct control_info_descriptor *control_info_descriptor;
	const u8 *event_data, *cmd_result_data;
	unsigned int ctrl_info_type, ctrl_info_length;

	while (buffer < end_of_payload) {
		control_info_descriptor =
				(struct control_info_descriptor *)buffer;

		ctrl_info_type = get_type(control_info_descriptor);
		ctrl_info_length = get_length(control_info_descriptor);

		cc33xx_debug(DEBUG_CMD, "Processing message type %d, len %d",
			     ctrl_info_type, ctrl_info_length);

		switch (ctrl_info_type) {
		case CTRL_MSG_EVENT:
			event_data = buffer + sizeof(*control_info_descriptor);

			deffer_event(cc, event_data, ctrl_info_length);
			break;

		case CTRL_MSG_COMMND_COMPLETE:
			cmd_result_data = buffer;
			cmd_result_data += sizeof(*control_info_descriptor);

			if (ctrl_info_length > sizeof(cc->command_result)) {
				print_hex_dump(KERN_DEBUG, "message dump:",
					       DUMP_PREFIX_OFFSET, 16, 1,
					       cmd_result_data,
					       ctrl_info_length, false);

				WARN(1, "Error device response exceeds result buffer size");

				goto message_parse_error;
			}

			memcpy(cc->command_result,
			       cmd_result_data, ctrl_info_length);

			cc->result_length = ctrl_info_length;

			complete(&cc->command_complete);
			break;

		default:
			print_hex_dump(KERN_DEBUG, "message dump:",
				       DUMP_PREFIX_OFFSET, 16, 1,
				start_of_payload, buffer_length, false);

			WARN(1, "Error processing device message @ offset %zu",
			     (size_t)(buffer - start_of_payload));

			goto message_parse_error;
		}

		buffer += sizeof(*control_info_descriptor);
		buffer += ctrl_info_length;
	}

	return 0;

message_parse_error:
	return -EIO;
}

static int read_control_message(struct cc33xx *cc, u8 *read_buffer,
				size_t buffer_size)
{
	int ret;
	size_t device_message_size;
	struct NAB_header *nab_header;

	ret = cc33xx_raw_read(cc, NAB_CONTROL_ADDR, read_buffer,
			      buffer_size, false);

	if (ret < 0) {
		cc33xx_debug(DEBUG_CMD,
			     "control read Error response 0x%x", ret);
		return ret;
	}

	nab_header = (struct NAB_header *)read_buffer;

	if (le32_to_cpu(nab_header->sync_pattern) != DEVICE_SYNC_PATTERN) {
		cc33xx_error("Wrong device sync pattern: 0x%x",
			     nab_header->sync_pattern);
		return -EIO;
	}

	device_message_size = sizeof(*nab_header) + NAB_EXTRA_BYTES
							+ le16_to_cpu(nab_header->len);

	if (device_message_size > buffer_size) {
		cc33xx_error("Invalid NAB length field: %x", nab_header->len);
		return -EIO;
	}

	return le16_to_cpu(nab_header->len);
}

static int process_event_and_cmd_result(struct cc33xx *cc,
					struct core_status *core_status)
{
	int ret;
	u8 *read_buffer, *message;
	const size_t buffer_size = CC33XX_CMD_MAX_SIZE;
	size_t message_length;
	struct core_status *new_core_status;
	__le32 previous_hint;

	read_buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!read_buffer)
		return -ENOMEM;

	ret = read_control_message(cc, read_buffer, buffer_size);
	if (ret < 0)
		goto out;

	message_length = ret - NAB_EXTRA_BYTES;
	message = read_buffer + sizeof(struct NAB_header) + NAB_EXTRA_BYTES;
	ret = parse_control_message(cc, message, message_length);
	if (ret < 0)
		goto out;

	/* Each read transaction always carries an updated core status */
	previous_hint = core_status->host_interrupt_status;
	new_core_status = (struct core_status *)
		(read_buffer + buffer_size - sizeof(struct core_status));
	memcpy(core_status, new_core_status, sizeof(*core_status));
	/* Host interrupt filed is clear-on-read and we do not want
	 * to overrun previously unhandled bits.
	 */
	core_status->host_interrupt_status |= previous_hint;

out:
	kfree(read_buffer);
	return ret;
}

static int verify_padding(struct core_status *core_status)
{
	unsigned int i;
	const u32 valid_padding = 0x55555555;

	for (i = 0; i < ARRAY_SIZE(core_status->block_pad); i++) {
		if (le32_to_cpu(core_status->block_pad[i]) != valid_padding) {
			cc33xx_error("Error in core status padding:");
			print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 16,
				       1, core_status, sizeof(*core_status),
				       false);
			return -1;
		}
	}

	return 0;
}

static int process_core_status(struct cc33xx *cc,
			       struct core_status *core_status)
{
	bool	core_status_idle;
	u32	shadow_host_interrupt_status;
	int	ret;

	do {
		core_status_idle = true;

		shadow_host_interrupt_status =
			le32_to_cpu(core_status->host_interrupt_status);

		/* Interrupts are aggregated (ORed) in this filed with each
		 * read operation from the device.
		 */
		core_status->host_interrupt_status = 0;

		cc33xx_debug(DEBUG_IRQ,
			     "HINT_STATUS: 0x%x, TSF: 0x%x, rx status: 0x%x",
			     shadow_host_interrupt_status, core_status->tsf,
			     core_status->rx_status);

		if (shadow_host_interrupt_status & HINT_COMMAND_COMPLETE) {
			ret = process_event_and_cmd_result(cc, core_status);
			if (ret < 0) {
				memset(core_status, 0, sizeof(*core_status));
				return ret;
			}
			core_status_idle = false;
		}

		if ((le32_to_cpu(core_status->rx_status) & RX_BYTE_COUNT_MASK) != 0) {
			cc33xx_debug(DEBUG_RX, "Rx data pending, triggering deferred work");
			queue_work(cc->freezable_wq, &cc->irq_deferred_work);
		}

		if (core_status->fw_info.tx_result_queue_index
						!= cc->last_fw_rls_idx){
			cc33xx_debug(DEBUG_TX, "Tx new result, triggering deferred work");
			queue_work(cc->freezable_wq, &cc->irq_deferred_work);
		}

		if (shadow_host_interrupt_status &  HINT_NEW_TX_RESULT) {
			cc33xx_debug(DEBUG_TX, "Tx complete, triggering deferred work");
			queue_work(cc->freezable_wq, &cc->irq_deferred_work);
		}

		if (shadow_host_interrupt_status & BOOT_TIME_INTERRUPTS) {
			cc33xx_handle_boot_irqs(cc,
						shadow_host_interrupt_status);
		}

		if (shadow_host_interrupt_status & HINT_GENERAL_ERROR) {
			cc33xx_error("FW is stuck, triggering recovery");
			cc33xx_queue_recovery_work(cc);
		}
	} while (!core_status_idle);

	return 0;
}

void cc33xx_irq(void *cookie)
{
	struct cc33xx *cc = cookie;
	unsigned long flags;
	int ret;

	claim_core_status_lock(cc);

	if (test_bit(CC33XX_FLAG_SUSPENDED, &cc->flags)) {
		/* don't enqueue a work right now. mark it as pending */
		set_bit(CC33XX_FLAG_PENDING_WORK, &cc->flags);
		spin_lock_irqsave(&cc->cc_lock, flags);
		cc33xx_disable_interrupts_nosync(cc);
		pm_wakeup_hard_event(cc->dev);
		spin_unlock_irqrestore(&cc->cc_lock, flags);
		goto out;
	}

	ret = read_core_status(cc, cc->core_status);
	if (unlikely(ret < 0)) {
		cc33xx_error("IO error during core status read");
		cc33xx_queue_recovery_work(cc);
		goto out;
	}

	ret = verify_padding(cc->core_status);
	if (unlikely(ret < 0)) {
		cc33xx_queue_recovery_work(cc);
		goto out;
	}

	process_core_status(cc, cc->core_status);

out:
	release_core_status_lock(cc);
}

struct vif_counter_data {
	u8 counter;

	struct ieee80211_vif *cur_vif;
	bool cur_vif_running;
};

static void cc33xx_vif_count_iter(void *data, u8 *mac,
				  struct ieee80211_vif *vif)
{
	struct vif_counter_data *counter = data;

	counter->counter++;
	if (counter->cur_vif == vif)
		counter->cur_vif_running = true;
}

/* caller must not hold cc->mutex, as it might deadlock */
static void cc33xx_get_vif_count(struct ieee80211_hw *hw,
				 struct ieee80211_vif *cur_vif,
				 struct vif_counter_data *data)
{
	memset(data, 0, sizeof(*data));
	data->cur_vif = cur_vif;

	ieee80211_iterate_active_interfaces(hw, IEEE80211_IFACE_ITER_RESUME_ALL,
					    cc33xx_vif_count_iter, data);
}

void cc33xx_queue_recovery_work(struct cc33xx *cc)
{
	/* Avoid a recursive recovery */
	if (cc->state == CC33XX_STATE_ON) {
		cc->state = CC33XX_STATE_RESTARTING;
		set_bit(CC33XX_FLAG_RECOVERY_IN_PROGRESS, &cc->flags);
		ieee80211_queue_work(cc->hw, &cc->recovery_work);
	}
}

static void cc33xx_save_freed_pkts(struct cc33xx *cc, struct cc33xx_vif *wlvif,
				   u8 hlid, struct ieee80211_sta *sta)
{
	struct cc33xx_station *wl_sta;
	u32 sqn_recovery_padding = CC33XX_TX_SQN_POST_RECOVERY_PADDING;

	wl_sta = (void *)sta->drv_priv;
	wl_sta->total_freed_pkts = cc->links[hlid].total_freed_pkts;

	/* increment the initial seq number on recovery to account for
	 * transmitted packets that we haven't yet got in the FW status
	 */
	if (wlvif->encryption_type == KEY_GEM)
		sqn_recovery_padding = CC33XX_TX_SQN_POST_RECOVERY_PADDING_GEM;

	if (test_bit(CC33XX_FLAG_RECOVERY_IN_PROGRESS, &cc->flags))
		wl_sta->total_freed_pkts += sqn_recovery_padding;
}

static void cc33xx_save_freed_pkts_addr(struct cc33xx *cc,
					struct cc33xx_vif *wlvif,
					u8 hlid, const u8 *addr)
{
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif = cc33xx_wlvif_to_vif(wlvif);

	if (WARN_ON(hlid == CC33XX_INVALID_LINK_ID ||
		    is_zero_ether_addr(addr)))
		return;

	rcu_read_lock();
	sta = ieee80211_find_sta(vif, addr);

	if (sta)
		cc33xx_save_freed_pkts(cc, wlvif, hlid, sta);

	rcu_read_unlock();
}

static void cc33xx_recovery_work(struct work_struct *work)
{
	struct cc33xx *cc = container_of(work, struct cc33xx, recovery_work);
	struct cc33xx_vif *wlvif;
	struct ieee80211_vif *vif;

	cc33xx_notice("CC33xx driver attempting recovery");

	if (cc->conf.core.no_recovery) {
		cc33xx_info("Recovery disabled by configuration, driver will not restart.");
		return;
	}

	if (test_bit(CC33XX_FLAG_DRIVER_REMOVED, &cc->flags)) {
		cc33xx_info("Driver being removed, recovery disabled");
		return;
	}

	cc->state = CC33XX_STATE_RESTARTING;
	set_bit(CC33XX_FLAG_RECOVERY_IN_PROGRESS, &cc->flags);

	mutex_lock(&cc->mutex);
	while (!list_empty(&cc->wlvif_list)) {
		wlvif = list_first_entry(&cc->wlvif_list,
					 struct cc33xx_vif, list);
		vif = cc33xx_wlvif_to_vif(wlvif);

		if (test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
			ieee80211_connection_loss(vif);

		__cc33xx_op_remove_interface(cc, vif, false);
	}
	mutex_unlock(&cc->mutex);

	cc33xx_turn_off(cc);
	msleep(500);

	mutex_lock(&cc->mutex);
	cc33xx_init_fw(cc);
	mutex_unlock(&cc->mutex);

	ieee80211_restart_hw(cc->hw);

	mutex_lock(&cc->mutex);
	clear_bit(CC33XX_FLAG_RECOVERY_IN_PROGRESS, &cc->flags);
	mutex_unlock(&cc->mutex);
}

static void irq_deferred_work(struct work_struct *work)
{
	int ret;
	unsigned long flags;
	struct cc33xx *cc =
		container_of(work, struct cc33xx, irq_deferred_work);

	mutex_lock(&cc->mutex);

	ret = cc33xx_irq_locked(cc);
	if (ret)
		cc33xx_queue_recovery_work(cc);

	spin_lock_irqsave(&cc->cc_lock, flags);
	/* In case TX was not handled here, queue TX work */
	clear_bit(CC33XX_FLAG_TX_PENDING, &cc->flags);
	if (!test_bit(CC33XX_FLAG_FW_TX_BUSY, &cc->flags) &&
	    cc33xx_tx_total_queue_count(cc) > 0)
		ieee80211_queue_work(cc->hw, &cc->tx_work);
	spin_unlock_irqrestore(&cc->cc_lock, flags);

	mutex_unlock(&cc->mutex);
}

static void irq_wrapper(struct platform_device *pdev)
{
	struct cc33xx *cc = platform_get_drvdata(pdev);

	cc33xx_irq(cc);
}

static int cc33xx_plt_init(struct cc33xx *cc)
{
	/* PLT init: Role enable + Role start + plt Init  */
	int ret = 0;

	/* Role enable */
	u8  returned_role_id = CC33XX_INVALID_ROLE_ID;
	u8 bcast_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	ret = cc33xx_cmd_role_enable(cc, bcast_addr,
				     ROLE_TRANSCEIVER, &returned_role_id);
	if (ret < 0) {
		cc33xx_info("PLT init Role Enable FAILED! , PLT roleID is: %u ",
			    returned_role_id);
		goto out;
	}

	ret = cc33xx_cmd_role_start_transceiver(cc, returned_role_id);
	if (ret < 0) {
		cc33xx_info("PLT init Role Start FAILED! , PLT roleID is: %u ",
			    returned_role_id);
		cc33xx_cmd_role_disable(cc, &returned_role_id);
		goto out;
	}

	cc->plt_role_id = returned_role_id;
	ret = cc33xx_cmd_plt_enable(cc, returned_role_id);

	if (ret >= 0) {
		cc33xx_info("PLT init Role Start succeed!, PLT roleID is: %u ",
			    returned_role_id);
	} else {
		cc33xx_info("PLT init Role Start FAILED! , PLT roleID is: %u ",
			    returned_role_id);
	}

out:
	return ret;
}

int cc33xx_plt_start(struct cc33xx *cc, const enum plt_mode plt_mode)
{
	int ret = 0;

	mutex_lock(&cc->mutex);

	if (plt_mode == PLT_ON && cc->plt_mode == PLT_ON) {
		cc33xx_error("PLT already on");
		ret = 0;
		goto out;
	}

	cc33xx_notice("PLT start");

	if (plt_mode != PLT_CHIP_AWAKE) {
		ret = cc33xx_plt_init(cc);
		if (ret < 0) {
			cc33xx_error("PLT start failed");
			goto out;
		}
	}

	/* Indicate to lower levels that we are now in PLT mode */
	cc->plt = true;
	cc->plt_mode = plt_mode;

out:
	mutex_unlock(&cc->mutex);

	return ret;
}

int cc33xx_plt_stop(struct cc33xx *cc)
{
	int ret = 0;

	cc33xx_notice("PLT stop");

	ret = cc33xx_cmd_role_stop_transceiver(cc);
	if (ret < 0)
		goto out;

	ret = cc33xx_cmd_role_disable(cc, &cc->plt_role_id);
	if (ret < 0)
		goto out;
	else
		cc33xx_cmd_plt_disable(cc);

	cc33xx_flush_deferred_work(cc);

	flush_deferred_event_list(cc);

	mutex_lock(&cc->mutex);
	cc->plt = false;
	cc->plt_mode = PLT_OFF;
	cc->rx_counter = 0;
	mutex_unlock(&cc->mutex);

out:
	return ret;
}

static void cc33xx_op_tx(struct ieee80211_hw *hw,
			 struct ieee80211_tx_control *control,
			 struct sk_buff *skb)
{
	struct cc33xx *cc = hw->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct cc33xx_vif *wlvif = NULL;
	enum cc33xx_queue_stop_reason stop_reason = CC33XX_QUEUE_STOP_REASON_WATERMARK;
	unsigned long flags;
	int q, mapping;
	u8 hlid;

	if (!vif) {
		cc33xx_debug(DEBUG_TX, "DROP skb with no vif");
		ieee80211_free_txskb(hw, skb);
		return;
	}

	wlvif = cc33xx_vif_to_data(vif);
	mapping = skb_get_queue_mapping(skb);
	q = cc33xx_tx_get_queue(mapping);

	hlid = cc33xx_tx_get_hlid(cc, wlvif, skb, control->sta);

	spin_lock_irqsave(&cc->cc_lock, flags);

	/* drop the packet if the link is invalid or the queue is stopped
	 * for any reason but watermark. Watermark is a "soft"-stop so we
	 * allow these packets through.
	 */

	if (hlid == CC33XX_INVALID_LINK_ID ||
	    (!test_bit(hlid, wlvif->links_map)) ||
	    (cc33xx_is_queue_stopped_locked(cc, wlvif, q) &&
	    !cc33xx_is_queue_stopped_by_reason_locked(cc, wlvif, q,
						      stop_reason))) {
		cc33xx_debug(DEBUG_TX, "DROP skb hlid %d q %d ", hlid, q);
		ieee80211_free_txskb(hw, skb);
		goto out;
	}

	cc33xx_debug(DEBUG_TX, "queue skb hlid %d q %d len %d %p",
		     hlid, q, skb->len, skb);
	skb_queue_tail(&cc->links[hlid].tx_queue[q], skb);

	cc->tx_queue_count[q]++;
	wlvif->tx_queue_count[q]++;

	/* The workqueue is slow to process the tx_queue and we need stop
	 * the queue here, otherwise the queue will get too long.
	 */
	if (wlvif->tx_queue_count[q] >= CC33XX_TX_QUEUE_HIGH_WATERMARK &&
	    !cc33xx_is_queue_stopped_by_reason_locked(cc, wlvif, q,
						      stop_reason)) {
		cc33xx_debug(DEBUG_TX, "op_tx: stopping queues for q %d", q);
		cc33xx_stop_queue_locked(cc, wlvif, q, stop_reason);
	}

	/* The chip specific setup must run before the first TX packet -
	 * before that, the tx_work will not be initialized!
	 */
	if (!test_bit(CC33XX_FLAG_FW_TX_BUSY, &cc->flags) &&
	    !test_bit(CC33XX_FLAG_TX_PENDING, &cc->flags)) {
		cc33xx_debug(DEBUG_TX, "Triggering tx thread");
		ieee80211_queue_work(cc->hw, &cc->tx_work);
	} else {
		cc33xx_debug(DEBUG_TX, "Not triggering tx thread cc->flags 0x%lx",
			     cc->flags);
	}

out:
	spin_unlock_irqrestore(&cc->cc_lock, flags);
}

/* The size of the dummy packet should be at least 1400 bytes. However, in
 * order to minimize the number of bus transactions, aligning it to 512 bytes
 * boundaries could be beneficial, performance wise
 */
#define TOTAL_TX_DUMMY_PACKET_SIZE (ALIGN(1400, 512))

static struct sk_buff *cc33xx_alloc_dummy_packet(struct cc33xx *cc)
{
	struct sk_buff *skb;
	struct ieee80211_hdr_3addr *hdr;
	unsigned int dummy_packet_size;

	dummy_packet_size = TOTAL_TX_DUMMY_PACKET_SIZE -
			    sizeof(struct cc33xx_tx_hw_descr) - sizeof(*hdr);

	skb = dev_alloc_skb(TOTAL_TX_DUMMY_PACKET_SIZE);
	if (!skb)
		return NULL;

	skb_reserve(skb, sizeof(struct cc33xx_tx_hw_descr));

	hdr = skb_put_zero(skb, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					 IEEE80211_STYPE_NULLFUNC |
					 IEEE80211_FCTL_TODS);

	skb_put_zero(skb, dummy_packet_size);

	/* Dummy packets require the TID to be management */
	skb->priority = CC33XX_TID_MGMT;

	/* Initialize all fields that might be used */
	skb_set_queue_mapping(skb, 0);
	memset(IEEE80211_SKB_CB(skb), 0, sizeof(struct ieee80211_tx_info));

	return skb;
}

static int cc33xx_validate_wowlan_pattern(struct cfg80211_pkt_pattern *p)
{
	int num_fields = 0, in_field = 0, fields_size = 0;
	int i, pattern_len = 0;

	if (!p->mask) {
		cc33xx_warning("No mask in WoWLAN pattern");
		return -EINVAL;
	}

	/* The pattern is broken up into segments of bytes at different offsets
	 * that need to be checked by the FW filter. Each segment is called
	 * a field in the FW API. We verify that the total number of fields
	 * required for this pattern won't exceed FW limits (8)
	 * as well as the total fields buffer won't exceed the FW limit.
	 * Note that if there's a pattern which crosses Ethernet/IP header
	 * boundary a new field is required.
	 */
	for (i = 0; i < p->pattern_len; i++) {
		if (test_bit(i, (unsigned long *)p->mask)) {
			if (!in_field) {
				in_field = 1;
				pattern_len = 1;
			} else if (i == CC33XX_RX_FILTER_ETH_HEADER_SIZE) {
				num_fields++;
				fields_size += pattern_len +
					RX_FILTER_FIELD_OVERHEAD;
				pattern_len = 1;
			} else {
				pattern_len++;
			}
		} else if (in_field) {
			in_field = 0;
			fields_size += pattern_len + RX_FILTER_FIELD_OVERHEAD;
			num_fields++;
		}
	}

	if (in_field) {
		fields_size += pattern_len + RX_FILTER_FIELD_OVERHEAD;
		num_fields++;
	}

	if (num_fields > CC33XX_RX_FILTER_MAX_FIELDS) {
		cc33xx_warning("RX Filter too complex. Too many segments");
		return -EINVAL;
	}

	if (fields_size > CC33XX_RX_FILTER_MAX_FIELDS_SIZE) {
		cc33xx_warning("RX filter pattern is too big");
		return -E2BIG;
	}

	return 0;
}

static void cc33xx_rx_filter_free(struct cc33xx_rx_filter *filter)
{
	int i;

	if (!filter)
		return;

	for (i = 0; i < filter->num_fields; i++)
		kfree(filter->fields[i].pattern);

	kfree(filter);
}

static int cc33xx_rx_filter_alloc_field(struct cc33xx_rx_filter *filter,
					u16 offset, u8 flags,
					const u8 *pattern, u8 len)
{
	struct cc33xx_rx_filter_field *field;

	if (filter->num_fields == CC33XX_RX_FILTER_MAX_FIELDS) {
		cc33xx_warning("Max fields per RX filter. can't alloc another");
		return -EINVAL;
	}

	field = &filter->fields[filter->num_fields];

	field->pattern = kzalloc(len, GFP_KERNEL);
	if (!field->pattern)
		return -ENOMEM;

	filter->num_fields++;

	field->offset = cpu_to_le16(offset);
	field->flags = flags;
	field->len = len;
	memcpy(field->pattern, pattern, len);

	return 0;
}

/* Allocates an RX filter returned through f
 * which needs to be freed using rx_filter_free()
 */
static int
cc33xx_convert_wowlan_pattern_to_rx_filter(struct cfg80211_pkt_pattern *p,
					   struct cc33xx_rx_filter **f)
{
	int i, j, ret = 0;
	struct cc33xx_rx_filter *filter;
	u16 offset;
	u8 flags, len;

	filter = kzalloc(sizeof(*filter), GFP_KERNEL);
	if (!filter) {
		ret = -ENOMEM;
		goto err;
	}

	i = 0;
	while (i < p->pattern_len) {
		if (!test_bit(i, (unsigned long *)p->mask)) {
			i++;
			continue;
		}

		for (j = i; j < p->pattern_len; j++) {
			if (!test_bit(j, (unsigned long *)p->mask))
				break;

			if (i < CC33XX_RX_FILTER_ETH_HEADER_SIZE &&
			    j >= CC33XX_RX_FILTER_ETH_HEADER_SIZE)
				break;
		}

		if (i < CC33XX_RX_FILTER_ETH_HEADER_SIZE) {
			offset = i;
			flags = CC33XX_RX_FILTER_FLAG_ETHERNET_HEADER;
		} else {
			offset = i - CC33XX_RX_FILTER_ETH_HEADER_SIZE;
			flags = CC33XX_RX_FILTER_FLAG_IP_HEADER;
		}

		len = j - i;

		ret = cc33xx_rx_filter_alloc_field(filter, offset, flags,
						   &p->pattern[i], len);
		if (ret)
			goto err;

		i = j;
	}

	filter->action = FILTER_SIGNAL;

	*f = filter;
	ret = 0;
	goto out;

err:
	cc33xx_rx_filter_free(filter);
	*f = NULL;
out:
	return ret;
}

static int cc33xx_configure_wowlan(struct cc33xx *cc,
				   struct cfg80211_wowlan *wow)
{
	int i, ret;

	if (!wow || (!wow->any && !wow->n_patterns)) {
		if (wow)
			cc33xx_warning("invalid wow configuration - set to pattern trigger without setting pattern");

		ret = cc33xx_acx_default_rx_filter_enable(cc, 0,
							  FILTER_SIGNAL);
		if (ret)
			goto out;

		ret = cc33xx_rx_filter_clear_all(cc);
		if (ret)
			goto out;

		return 0;
	}

	if (wow->any) {
		ret = cc33xx_acx_default_rx_filter_enable(cc, 1,
							  FILTER_SIGNAL);
		if (ret)
			goto out;

		ret = cc33xx_rx_filter_clear_all(cc);
		if (ret)
			goto out;

		return 0;
	}

	if (WARN_ON(wow->n_patterns > CC33XX_MAX_RX_FILTERS))
		return -EINVAL;

	/* Validate all incoming patterns before clearing current FW state */
	for (i = 0; i < wow->n_patterns; i++) {
		ret = cc33xx_validate_wowlan_pattern(&wow->patterns[i]);
		if (ret) {
			cc33xx_warning("Bad wowlan pattern %d", i);
			return ret;
		}
	}

	ret = cc33xx_acx_default_rx_filter_enable(cc, 0, FILTER_SIGNAL);
	if (ret)
		goto out;

	ret = cc33xx_rx_filter_clear_all(cc);
	if (ret)
		goto out;

	/* Translate WoWLAN patterns into filters */
	for (i = 0; i < wow->n_patterns; i++) {
		struct cfg80211_pkt_pattern *p;
		struct cc33xx_rx_filter *filter = NULL;

		p = &wow->patterns[i];

		ret = cc33xx_convert_wowlan_pattern_to_rx_filter(p, &filter);
		if (ret) {
			cc33xx_warning("Failed to create an RX filter from wowlan pattern %d",
				       i);
			goto out;
		}

		ret = cc33xx_rx_filter_enable(cc, i, 1, filter);

		cc33xx_rx_filter_free(filter);
		if (ret)
			goto out;
	}

	ret = cc33xx_acx_default_rx_filter_enable(cc, 1, FILTER_DROP);

out:
	return ret;
}

static int cc33xx_configure_suspend_sta(struct cc33xx *cc,
					struct cc33xx_vif *wlvif,
					struct cfg80211_wowlan *wow)
{
	struct cc33xx_core_conf *core_conf = &cc->conf.core;
	int ret = 0;

	if (!test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
		goto out;

	ret = cc33xx_configure_wowlan(cc, wow);
	if (ret < 0)
		goto out;

	if (core_conf->suspend_wake_up_event == core_conf->wake_up_event &&
	    core_conf->suspend_listen_interval == core_conf->listen_interval)
		goto out;

	ret = cc33xx_acx_wake_up_conditions(cc, wlvif,
					    core_conf->suspend_wake_up_event,
					    core_conf->suspend_listen_interval);

	if (ret < 0)
		cc33xx_error("suspend: set wake up conditions failed: %d", ret);
out:
	return ret;
}

static int cc33xx_configure_suspend_ap(struct cc33xx *cc,
				       struct cc33xx_vif *wlvif,
				       struct cfg80211_wowlan *wow)
{
	int ret = 0;

	if (!test_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags))
		goto out;

	ret = cc33xx_acx_beacon_filter_opt(cc, wlvif, true);
	if (ret < 0)
		goto out;

	ret = cc33xx_configure_wowlan(cc, wow);
	if (ret < 0)
		goto out;

out:
	return ret;
}

static int cc33xx_configure_suspend(struct cc33xx *cc, struct cc33xx_vif *wlvif,
				    struct cfg80211_wowlan *wow)
{
	if (wlvif->bss_type == BSS_TYPE_STA_BSS)
		return cc33xx_configure_suspend_sta(cc, wlvif, wow);

	if (wlvif->bss_type == BSS_TYPE_AP_BSS)
		return cc33xx_configure_suspend_ap(cc, wlvif, wow);

	return 0;
}

static void cc33xx_configure_resume(struct cc33xx *cc, struct cc33xx_vif *wlvif)
{
	int ret = 0;
	bool is_ap = wlvif->bss_type == BSS_TYPE_AP_BSS;
	bool is_sta = wlvif->bss_type == BSS_TYPE_STA_BSS;
	struct cc33xx_core_conf *core_conf = &cc->conf.core;

	if (!is_ap && !is_sta)
		return;

	if ((is_sta && !test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags)) ||
	    (is_ap && !test_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags)))
		return;

	cc33xx_configure_wowlan(cc, NULL);

	if (is_sta) {
		if (core_conf->suspend_wake_up_event == core_conf->wake_up_event &&
		    core_conf->suspend_listen_interval == core_conf->listen_interval)
			return;

		ret = cc33xx_acx_wake_up_conditions(cc, wlvif,
						    core_conf->wake_up_event,
						    core_conf->listen_interval);

		if (ret < 0)
			cc33xx_error("resume: wake up conditions failed: %d",
				     ret);

	} else if (is_ap) {
		ret = cc33xx_acx_beacon_filter_opt(cc, wlvif, false);
	}
}

static int __maybe_unused cc33xx_op_suspend(struct ieee80211_hw *hw,
					    struct cfg80211_wowlan *wow)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif;
	unsigned long flags;
	int ret = 0;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 suspend wow=%d", !!wow);
	WARN_ON(!wow);

	/* we want to perform the recovery before suspending */
	if (test_bit(CC33XX_FLAG_RECOVERY_IN_PROGRESS, &cc->flags)) {
		cc33xx_warning("postponing suspend to perform recovery");
		return -EBUSY;
	}

	cc33xx_tx_flush(cc);

	mutex_lock(&cc->mutex);

	cc->keep_device_power = true;
	cc33xx_for_each_wlvif(cc, wlvif) {
		if (cc33xx_is_p2p_mgmt(wlvif))
			continue;

		ret = cc33xx_configure_suspend(cc, wlvif, wow);
		if (ret < 0) {
			mutex_unlock(&cc->mutex);
			cc33xx_warning("couldn't prepare device to suspend");
			return ret;
		}
	}

	mutex_unlock(&cc->mutex);

	if (ret < 0) {
		cc33xx_warning("couldn't prepare device to suspend");
		return ret;
	}

	/* flush any remaining work */
	cc33xx_debug(DEBUG_MAC80211, "flushing remaining works");

	flush_work(&cc->tx_work);

	/* Cancel the watchdog even if above tx_flush failed. We will detect
	 * it on resume anyway.
	 */
	cancel_delayed_work(&cc->tx_watchdog_work);

	/* set suspended flag to avoid triggering a new threaded_irq
	 * work.
	 */
	spin_lock_irqsave(&cc->cc_lock, flags);
	set_bit(CC33XX_FLAG_SUSPENDED, &cc->flags);
	spin_unlock_irqrestore(&cc->cc_lock, flags);

	return 0;
}

static int __maybe_unused cc33xx_op_resume(struct ieee80211_hw *hw)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif;
	unsigned long flags;
	bool run_irq_work = false, pending_recovery;
	int ret = 0;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 resume wow=%d",
		     cc->keep_device_power);
	WARN_ON(!cc->keep_device_power);

	/* re-enable irq_work enqueuing, and call irq_work directly if
	 * there is a pending work.
	 */
	spin_lock_irqsave(&cc->cc_lock, flags);
	clear_bit(CC33XX_FLAG_SUSPENDED, &cc->flags);
	run_irq_work = test_and_clear_bit(CC33XX_FLAG_PENDING_WORK, &cc->flags);
	spin_unlock_irqrestore(&cc->cc_lock, flags);

	mutex_lock(&cc->mutex);

	/* test the recovery flag before calling any SDIO functions */
	pending_recovery = test_bit(CC33XX_FLAG_RECOVERY_IN_PROGRESS,
				    &cc->flags);

	if (run_irq_work) {
		cc33xx_debug(DEBUG_MAC80211, "Running postponed irq_work directly");

		/* don't talk to the HW if recovery is pending */
		if (!pending_recovery) {
			ret = cc33xx_irq_locked(cc);
			if (ret)
				cc33xx_queue_recovery_work(cc);
		}

		cc33xx_enable_interrupts(cc);
	}

	if (pending_recovery) {
		cc33xx_warning("queuing forgotten recovery on resume");
		ieee80211_queue_work(cc->hw, &cc->recovery_work);
		goto out;
	}

	cc33xx_for_each_wlvif(cc, wlvif) {
		if (cc33xx_is_p2p_mgmt(wlvif))
			continue;

		cc33xx_configure_resume(cc, wlvif);
	}

out:
	cc->keep_device_power = false;

	/* Set a flag to re-init the watchdog on the first Tx after resume.
	 * That way we avoid possible conditions where Tx-complete interrupts
	 * fail to arrive and we perform a spurious recovery.
	 */
	set_bit(CC33XX_FLAG_REINIT_TX_WDOG, &cc->flags);
	mutex_unlock(&cc->mutex);

	return ret;
}

static int cc33xx_op_start(struct ieee80211_hw *hw)
{
	cc33xx_debug(DEBUG_MAC80211, "mac80211 start");

	/* We have to delay the booting of the hardware because
	 * we need to know the local MAC address before downloading and
	 * initializing the firmware. The MAC address cannot be changed
	 * after boot, and without the proper MAC address, the firmware
	 * will not function properly.
	 *
	 * The MAC address is first known when the corresponding interface
	 * is added. That is where we will initialize the hardware.
	 */

	return 0;
}

static void cc33xx_turn_off(struct cc33xx *cc)
{
	int i;

	if (cc->state == CC33XX_STATE_OFF) {
		if (test_and_clear_bit(CC33XX_FLAG_RECOVERY_IN_PROGRESS,
				       &cc->flags))
			cc33xx_enable_interrupts(cc);

		return;
	}

	cc33xx_debug(DEBUG_BOOT, "Turning off");

	mutex_lock(&cc->mutex);

	/* this must be before the cancel_work calls below, so that the work
	 * functions don't perform further work.
	 */
	cc->state = CC33XX_STATE_OFF;

	/* Use the nosync variant to disable interrupts, so the mutex could be
	 * held while doing so without deadlocking.
	 */
	cc33xx_disable_interrupts_nosync(cc);

	mutex_unlock(&cc->mutex);

	if (!test_bit(CC33XX_FLAG_RECOVERY_IN_PROGRESS, &cc->flags))
		cancel_work_sync(&cc->recovery_work);
	cc33xx_flush_deferred_work(cc);
	cancel_delayed_work_sync(&cc->scan_complete_work);
	cancel_work_sync(&cc->netstack_work);
	cancel_work_sync(&cc->tx_work);
	cancel_work_sync(&cc->irq_deferred_work);
	cancel_delayed_work_sync(&cc->tx_watchdog_work);

	/* let's notify MAC80211 about the remaining pending TX frames */
	mutex_lock(&cc->mutex);
	cc33xx_tx_reset(cc);

	cc33xx_power_off(cc);

	cc->band = NL80211_BAND_2GHZ;

	cc->rx_counter = 0;
	cc->power_level = CC33XX_MAX_TXPWR;
	cc->tx_blocks_available = 0;
	cc->tx_allocated_blocks = 0;

	cc->ap_fw_ps_map = 0;
	cc->ap_ps_map = 0;
	cc->sleep_auth = CC33XX_PSM_ILLEGAL;
	memset(cc->roles_map, 0, sizeof(cc->roles_map));
	memset(cc->links_map, 0, sizeof(cc->links_map));
	memset(cc->roc_map, 0, sizeof(cc->roc_map));
	memset(cc->session_ids, 0, sizeof(cc->session_ids));
	memset(cc->rx_filter_enabled, 0, sizeof(cc->rx_filter_enabled));
	cc->active_sta_count = 0;
	cc->active_link_count = 0;
	cc->ble_enable = 0;

	/* The system link is always allocated */
	cc->links[CC33XX_SYSTEM_HLID].allocated_pkts = 0;
	cc->links[CC33XX_SYSTEM_HLID].prev_freed_pkts = 0;
	__set_bit(CC33XX_SYSTEM_HLID, cc->links_map);

	/* this is performed after the cancel_work calls and the associated
	 * mutex_lock, so that cc33xx_op_add_interface does not accidentally
	 * get executed before all these vars have been reset.
	 */
	cc->flags = 0;

	for (i = 0; i < NUM_TX_QUEUES; i++)
		cc->tx_allocated_pkts[i] = 0;

	kfree(cc->target_mem_map);
	cc->target_mem_map = NULL;

	/* FW channels must be re-calibrated after recovery,
	 * save current Reg-Domain channel configuration and clear it.
	 */
	memcpy(cc->reg_ch_conf_pending, cc->reg_ch_conf_last,
	       sizeof(cc->reg_ch_conf_pending));
	memset(cc->reg_ch_conf_last, 0, sizeof(cc->reg_ch_conf_last));

	mutex_unlock(&cc->mutex);
}

static inline void cc33xx_op_stop(struct ieee80211_hw *hw)
{
	cc33xx_debug(DEBUG_MAC80211, "mac80211 stop");
}

static void cc33xx_channel_switch_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct cc33xx *cc;
	struct ieee80211_vif *vif;
	struct cc33xx_vif *wlvif;

	dwork = to_delayed_work(work);
	wlvif = container_of(dwork, struct cc33xx_vif, channel_switch_work);
	cc = wlvif->cc;

	cc33xx_info("channel switch failed (role_id: %d).", wlvif->role_id);

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	/* check the channel switch is still ongoing */
	if (!test_and_clear_bit(WLVIF_FLAG_CS_PROGRESS, &wlvif->flags))
		goto out;

	vif = cc33xx_wlvif_to_vif(wlvif);
	ieee80211_chswitch_done(vif, false);

	cc33xx_cmd_stop_channel_switch(cc, wlvif);

out:
	mutex_unlock(&cc->mutex);
}

static void cc33xx_connection_loss_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct cc33xx *cc;
	struct ieee80211_vif *vif;
	struct cc33xx_vif *wlvif;

	dwork = to_delayed_work(work);
	wlvif = container_of(dwork, struct cc33xx_vif, connection_loss_work);
	cc = wlvif->cc;

	cc33xx_info("Connection loss work (role_id: %d).", wlvif->role_id);

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	/* Call mac80211 connection loss */
	if (!test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
		goto out;

	vif = cc33xx_wlvif_to_vif(wlvif);
	ieee80211_connection_loss(vif);

out:
	mutex_unlock(&cc->mutex);
}

static void cc33xx_pending_auth_complete_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct cc33xx *cc;
	struct cc33xx_vif *wlvif;
	unsigned long time_spare;

	dwork = to_delayed_work(work);
	wlvif = container_of(dwork, struct cc33xx_vif,
			     pending_auth_complete_work);
	cc = wlvif->cc;

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	/* Make sure a second really passed since the last auth reply. Maybe
	 * a second auth reply arrived while we were stuck on the mutex.
	 * Check for a little less than the timeout to protect from scheduler
	 * irregularities.
	 */
	time_spare = msecs_to_jiffies(CC33XX_PEND_AUTH_ROC_TIMEOUT - 50);
	time_spare += jiffies;
	if (!time_after(time_spare, wlvif->pending_auth_reply_time))
		goto out;

	/* cancel the ROC if active */
	cc33xx_debug(DEBUG_CMD,
		     "pending_auth t/o expired - cancel ROC if active");

	cc33xx_update_inconn_sta(cc, wlvif, NULL, false);

out:
	mutex_unlock(&cc->mutex);
}

static void cc33xx_roc_timeout_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct cc33xx *cc;
	struct cc33xx_vif *wlvif;
	unsigned long time_spare;

	dwork = to_delayed_work(work);
	wlvif = container_of(dwork, struct cc33xx_vif, roc_timeout_work);
	cc = wlvif->cc;

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	/* Make sure that requested timeout really passed. Maybe an association
	 * completed and croc arrived while we were stuck on the mutex.
	 * Check for a little less than the timeout to protect from scheduler
	 * irregularities.
	 */
	time_spare = msecs_to_jiffies(CC33xx_PEND_ROC_COMPLETE_TIMEOUT - 50);
	time_spare += jiffies;
	if (!time_after(time_spare, wlvif->pending_auth_reply_time))
		goto out;

	/* cancel the ROC if active */
	cc33xx_debug(DEBUG_CMD, "Waiting for CROC Timeout has expired -> cancel ROC if exist");

	if (test_bit(wlvif->role_id, cc->roc_map))
		cc33xx_croc(cc, wlvif->role_id);

out:
	mutex_unlock(&cc->mutex);
}

static int cc33xx_allocate_rate_policy(struct cc33xx *cc, u8 *idx)
{
	u8 policy = find_first_zero_bit(cc->rate_policies_map,
					CC33XX_MAX_RATE_POLICIES);
	if (policy >= CC33XX_MAX_RATE_POLICIES)
		return -EBUSY;

	__set_bit(policy, cc->rate_policies_map);
	*idx = policy;
	return 0;
}

static void cc33xx_free_rate_policy(struct cc33xx *cc, u8 *idx)
{
	if (WARN_ON(*idx >= CC33XX_MAX_RATE_POLICIES))
		return;

	__clear_bit(*idx, cc->rate_policies_map);
	*idx = CC33XX_MAX_RATE_POLICIES;
}

static u8 cc33xx_get_role_type(struct cc33xx *cc, struct cc33xx_vif *wlvif)
{
	struct ieee80211_vif *vif = cc33xx_wlvif_to_vif(wlvif);

	switch (wlvif->bss_type) {
	case BSS_TYPE_AP_BSS:
		if (wlvif->p2p)
			return CC33XX_ROLE_P2P_GO;
		else if (ieee80211_vif_is_mesh(vif))
			return CC33XX_ROLE_MESH_POINT;
		else
			return CC33XX_ROLE_AP;

	case BSS_TYPE_STA_BSS:
		if (wlvif->p2p)
			return CC33XX_ROLE_P2P_CL;
		else
			return CC33XX_ROLE_STA;

	case BSS_TYPE_IBSS:
		return CC33XX_ROLE_IBSS;

	default:
		cc33xx_error("invalid bss_type: %d", wlvif->bss_type);
	}
	return CC33XX_INVALID_ROLE_TYPE;
}

static int cc33xx_init_vif_data(struct cc33xx *cc, struct ieee80211_vif *vif)
{
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	struct conf_tx_settings *tx_settings = &cc->conf.host_conf.tx;
	int i;

	/* clear everything but the persistent data */
	memset(wlvif, 0, offsetof(struct cc33xx_vif, persistent));

	switch (ieee80211_vif_type_p2p(vif)) {
	case NL80211_IFTYPE_P2P_CLIENT:
		wlvif->p2p = 1;
		fallthrough;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_DEVICE:
		wlvif->bss_type = BSS_TYPE_STA_BSS;
		break;
	case NL80211_IFTYPE_ADHOC:
		wlvif->bss_type = BSS_TYPE_IBSS;
		break;
	case NL80211_IFTYPE_P2P_GO:
		wlvif->p2p = 1;
		fallthrough;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		wlvif->bss_type = BSS_TYPE_AP_BSS;
		break;
	default:
		wlvif->bss_type = MAX_BSS_TYPE;
		return -EOPNOTSUPP;
	}

	wlvif->role_id = CC33XX_INVALID_ROLE_ID;
	wlvif->dev_role_id = CC33XX_INVALID_ROLE_ID;
	wlvif->dev_hlid = CC33XX_INVALID_LINK_ID;

	if (wlvif->bss_type == BSS_TYPE_STA_BSS ||
	    wlvif->bss_type == BSS_TYPE_IBSS) {
		/* init sta/ibss data */
		wlvif->sta.hlid = CC33XX_INVALID_LINK_ID;
		cc33xx_allocate_rate_policy(cc, &wlvif->sta.basic_rate_idx);
		cc33xx_allocate_rate_policy(cc, &wlvif->sta.ap_rate_idx);
		cc33xx_allocate_rate_policy(cc, &wlvif->sta.p2p_rate_idx);
		wlvif->basic_rate_set = CONF_TX_RATE_MASK_BASIC;
		wlvif->basic_rate = CONF_TX_RATE_MASK_BASIC;
		wlvif->rate_set = CONF_TX_RATE_MASK_BASIC;
	} else {
		/* init ap data */
		wlvif->ap.bcast_hlid = CC33XX_INVALID_LINK_ID;
		wlvif->ap.global_hlid = CC33XX_INVALID_LINK_ID;
		cc33xx_allocate_rate_policy(cc, &wlvif->ap.mgmt_rate_idx);
		cc33xx_allocate_rate_policy(cc, &wlvif->ap.bcast_rate_idx);
		for (i = 0; i < CONF_TX_MAX_AC_COUNT; i++)
			cc33xx_allocate_rate_policy(cc,
						    &wlvif->ap.ucast_rate_idx[i]);
		wlvif->basic_rate_set = CONF_TX_ENABLED_RATES;
		/* TODO: check if basic_rate shouldn't be
		 * cc33xx_tx_min_rate_get(cc, wlvif->basic_rate_set);
		 * instead (the same thing for STA above).
		 */
		wlvif->basic_rate = CONF_TX_ENABLED_RATES;
		/* TODO: this seems to be used only for STA, check it */
		wlvif->rate_set = CONF_TX_ENABLED_RATES;
	}

	wlvif->bitrate_masks[NL80211_BAND_2GHZ] = tx_settings->basic_rate;
	wlvif->bitrate_masks[NL80211_BAND_5GHZ] = tx_settings->basic_rate_5;
	wlvif->beacon_int = CC33XX_DEFAULT_BEACON_INT;

	/* mac80211 configures some values globally, while we treat them
	 * per-interface. thus, on init, we have to copy them from cc
	 */
	wlvif->band = cc->band;
	wlvif->power_level = cc->power_level;

	INIT_WORK(&wlvif->rc_update_work, cc33xx_rc_update_work);
	INIT_DELAYED_WORK(&wlvif->channel_switch_work,
			  cc33xx_channel_switch_work);
	INIT_DELAYED_WORK(&wlvif->connection_loss_work,
			  cc33xx_connection_loss_work);
	INIT_DELAYED_WORK(&wlvif->pending_auth_complete_work,
			  cc33xx_pending_auth_complete_work);
	INIT_DELAYED_WORK(&wlvif->roc_timeout_work,
			  cc33xx_roc_timeout_work);
	INIT_LIST_HEAD(&wlvif->list);

	return 0;
}

struct cc33xx_hw_queue_iter_data {
	unsigned long hw_queue_map[BITS_TO_LONGS(CC33XX_NUM_MAC_ADDRESSES)];

	/* current vif */
	struct ieee80211_vif *vif;

	/* is the current vif among those iterated */
	bool cur_running;
};

static void cc33xx_hw_queue_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct cc33xx_hw_queue_iter_data *iter_data = data;

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE ||
	    WARN_ON_ONCE(vif->hw_queue[0] == IEEE80211_INVAL_HW_QUEUE))
		return;

	if (iter_data->cur_running || vif == iter_data->vif) {
		iter_data->cur_running = true;
		return;
	}

	__set_bit(vif->hw_queue[0] / NUM_TX_QUEUES, iter_data->hw_queue_map);
}

static int cc33xx_allocate_hw_queue_base(struct cc33xx *cc,
					 struct cc33xx_vif *wlvif)
{
	struct ieee80211_vif *vif = cc33xx_wlvif_to_vif(wlvif);
	struct cc33xx_hw_queue_iter_data iter_data = {};
	int i, q_base;

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		vif->cab_queue = IEEE80211_INVAL_HW_QUEUE;
		return 0;
	}

	iter_data.vif = vif;

	/* mark all bits taken by active interfaces */
	ieee80211_iterate_active_interfaces_atomic(cc->hw,
						   IEEE80211_IFACE_ITER_RESUME_ALL,
					cc33xx_hw_queue_iter, &iter_data);

	/* the current vif is already running in mac80211 (resume/recovery) */
	if (iter_data.cur_running) {
		wlvif->hw_queue_base = vif->hw_queue[0];
		cc33xx_debug(DEBUG_MAC80211,
			     "using pre-allocated hw queue base %d",
			     wlvif->hw_queue_base);

		/* interface type might have changed type */
		goto adjust_cab_queue;
	}

	q_base = find_first_zero_bit(iter_data.hw_queue_map,
				     CC33XX_NUM_MAC_ADDRESSES);
	if (q_base >= CC33XX_NUM_MAC_ADDRESSES)
		return -EBUSY;

	wlvif->hw_queue_base = q_base * NUM_TX_QUEUES;
	cc33xx_debug(DEBUG_MAC80211, "allocating hw queue base: %d",
		     wlvif->hw_queue_base);

	for (i = 0; i < NUM_TX_QUEUES; i++) {
		cc->queue_stop_reasons[wlvif->hw_queue_base + i] = 0;
		/* register hw queues in mac80211 */
		vif->hw_queue[i] = wlvif->hw_queue_base + i;
	}

adjust_cab_queue:
	/* the last places are reserved for cab queues per interface */
	if (wlvif->bss_type == BSS_TYPE_AP_BSS) {
		vif->cab_queue = NUM_TX_QUEUES * CC33XX_NUM_MAC_ADDRESSES +
					wlvif->hw_queue_base / NUM_TX_QUEUES;
	} else {
		vif->cab_queue = IEEE80211_INVAL_HW_QUEUE;
	}

	return 0;
}

static int cc33xx_op_add_interface(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	struct vif_counter_data vif_count;
	int ret = 0;
	u8 role_type;

	if (cc->plt) {
		cc33xx_error("Adding Interface not allowed while in PLT mode");
		return -EBUSY;
	}

	vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER |
			     IEEE80211_VIF_SUPPORTS_UAPSD |
			     IEEE80211_VIF_SUPPORTS_CQM_RSSI;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 add interface type %d mac %pM",
		     ieee80211_vif_type_p2p(vif), vif->addr);

	cc33xx_get_vif_count(hw, vif, &vif_count);

	mutex_lock(&cc->mutex);

	/* in some very corner case HW recovery scenarios its possible to
	 * get here before __cc33xx_op_remove_interface is complete, so
	 * opt out if that is the case.
	 */
	if (test_bit(CC33XX_FLAG_RECOVERY_IN_PROGRESS, &cc->flags) ||
	    test_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags)) {
		ret = -EBUSY;
		goto out;
	}

	ret = cc33xx_init_vif_data(cc, vif);
	if (ret < 0)
		goto out;

	wlvif->cc = cc;
	role_type = cc33xx_get_role_type(cc, wlvif);
	if (role_type == CC33XX_INVALID_ROLE_TYPE) {
		ret = -EINVAL;
		goto out;
	}

	ret = cc33xx_allocate_hw_queue_base(cc, wlvif);
	if (ret < 0)
		goto out;

	if (!cc33xx_is_p2p_mgmt(wlvif)) {
		ret = cc33xx_cmd_role_enable(cc, vif->addr,
					     role_type, &wlvif->role_id);
		if (ret < 0)
			goto out;

		ret = cc33xx_init_vif_specific(cc, vif);
		if (ret < 0)
			goto out;
	} else {
		ret = cc33xx_cmd_role_enable(cc, vif->addr, CC33XX_ROLE_DEVICE,
					     &wlvif->dev_role_id);
		if (ret < 0)
			goto out;

		/* needed mainly for configuring rate policies */
		ret = cc33xx_acx_config_ps(cc, wlvif);
		if (ret < 0)
			goto out;
	}

	list_add(&wlvif->list, &cc->wlvif_list);
	set_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags);

	if (wlvif->bss_type == BSS_TYPE_AP_BSS)
		cc->ap_count++;
	else
		cc->sta_count++;

out:
	mutex_unlock(&cc->mutex);

	return ret;
}

static void __cc33xx_op_remove_interface(struct cc33xx *cc,
					 struct ieee80211_vif *vif,
					 bool reset_tx_queues)
{
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	int i, ret;
	bool is_ap = (wlvif->bss_type == BSS_TYPE_AP_BSS);

	cc33xx_debug(DEBUG_MAC80211, "mac80211 remove interface %d", vif->type);
	cc33xx_debug(DEBUG_MAC80211, "mac80211 rm: name1=%s, name2=%s, name3=%s",
		     sdata->name, sdata->dev->name, sdata->wdev.netdev->name);

	if (!test_and_clear_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags))
		return;

	/* because of hardware recovery, we may get here twice */
	if (cc->state == CC33XX_STATE_OFF)
		return;

	if (cc->scan.state != CC33XX_SCAN_STATE_IDLE &&
	    cc->scan_wlvif == wlvif) {
		struct cfg80211_scan_info info = {
			.aborted = true,
		};

		/* Rearm the tx watchdog just before idling scan. This
		 * prevents just-finished scans from triggering the watchdog
		 */
		cc33xx_rearm_tx_watchdog_locked(cc);

		cc->scan.state = CC33XX_SCAN_STATE_IDLE;
		memset(cc->scan.scanned_ch, 0, sizeof(cc->scan.scanned_ch));
		cc->scan_wlvif = NULL;
		cc->scan.req = NULL;
		ieee80211_scan_completed(cc->hw, &info);
	}

	if (cc->sched_vif == wlvif)
		cc->sched_vif = NULL;

	if (cc->roc_vif == vif) {
		cc->roc_vif = NULL;
		ieee80211_remain_on_channel_expired(cc->hw);
	}

	if (!test_bit(CC33XX_FLAG_RECOVERY_IN_PROGRESS, &cc->flags)) {
		/* disable active roles */

		if (wlvif->bss_type == BSS_TYPE_STA_BSS ||
		    wlvif->bss_type == BSS_TYPE_IBSS) {
			if (wlvif->dev_hlid != CC33XX_INVALID_LINK_ID)
				cc33xx_stop_dev(cc, wlvif);
		}

		if (!cc33xx_is_p2p_mgmt(wlvif)) {
			ret = cc33xx_cmd_role_disable(cc, &wlvif->role_id);
			if (ret < 0)
				goto deinit;
		} else {
			ret = cc33xx_cmd_role_disable(cc, &wlvif->dev_role_id);
			if (ret < 0)
				goto deinit;
		}
	}
deinit:
	cc33xx_tx_reset_wlvif(cc, wlvif);

	/* clear all hlids (except system_hlid) */
	wlvif->dev_hlid = CC33XX_INVALID_LINK_ID;

	if (wlvif->bss_type == BSS_TYPE_STA_BSS ||
	    wlvif->bss_type == BSS_TYPE_IBSS) {
		wlvif->sta.hlid = CC33XX_INVALID_LINK_ID;
		cc33xx_free_rate_policy(cc, &wlvif->sta.basic_rate_idx);
		cc33xx_free_rate_policy(cc, &wlvif->sta.ap_rate_idx);
		cc33xx_free_rate_policy(cc, &wlvif->sta.p2p_rate_idx);
	} else {
		wlvif->ap.bcast_hlid = CC33XX_INVALID_LINK_ID;
		wlvif->ap.global_hlid = CC33XX_INVALID_LINK_ID;
		cc33xx_free_rate_policy(cc, &wlvif->ap.mgmt_rate_idx);
		cc33xx_free_rate_policy(cc, &wlvif->ap.bcast_rate_idx);
		for (i = 0; i < CONF_TX_MAX_AC_COUNT; i++)
			cc33xx_free_rate_policy(cc,
						&wlvif->ap.ucast_rate_idx[i]);
		cc33xx_free_ap_keys(cc, wlvif);
	}

	dev_kfree_skb(wlvif->probereq);
	wlvif->probereq = NULL;
	if (cc->last_wlvif == wlvif)
		cc->last_wlvif = NULL;
	list_del(&wlvif->list);
	memset(wlvif->ap.sta_hlid_map, 0, sizeof(wlvif->ap.sta_hlid_map));
	wlvif->role_id = CC33XX_INVALID_ROLE_ID;
	wlvif->dev_role_id = CC33XX_INVALID_ROLE_ID;

	if (is_ap)
		cc->ap_count--;
	else
		cc->sta_count--;

	/* Last AP, have more stations. Configure sleep auth according to STA.
	 * Don't do thin on unintended recovery.
	 */
	if (test_bit(CC33XX_FLAG_RECOVERY_IN_PROGRESS, &cc->flags))
		goto unlock;

	/* mask ap events */
	if (cc->ap_count == 0 && is_ap)
		cc->event_mask &= ~cc->ap_event_mask;

	if (cc->ap_count == 0 && is_ap && cc->sta_count) {
		u8 sta_auth = cc->conf.host_conf.conn.sta_sleep_auth;
		/* Configure for power according to debugfs */
		if (sta_auth != CC33XX_PSM_ILLEGAL)
			cc33xx_acx_sleep_auth(cc, sta_auth);
		/* Configure for ELP power saving */
		else
			cc33xx_acx_sleep_auth(cc, CC33XX_PSM_ELP);
	}

unlock:
	mutex_unlock(&cc->mutex);

	cancel_work_sync(&wlvif->rc_update_work);
	cancel_delayed_work_sync(&wlvif->connection_loss_work);
	cancel_delayed_work_sync(&wlvif->channel_switch_work);
	cancel_delayed_work_sync(&wlvif->pending_auth_complete_work);
	cancel_delayed_work_sync(&wlvif->roc_timeout_work);

	mutex_lock(&cc->mutex);
}

static void cc33xx_op_remove_interface(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	struct cc33xx_vif *iter;
	struct vif_counter_data vif_count;

	cc33xx_get_vif_count(hw, vif, &vif_count);
	mutex_lock(&cc->mutex);

	if (cc->state == CC33XX_STATE_OFF ||
	    !test_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags))
		goto out;

	/* cc->vif can be null here if someone shuts down the interface
	 * just when hardware recovery has been started.
	 */
	cc33xx_for_each_wlvif(cc, iter) {
		if (iter != wlvif)
			continue;

		__cc33xx_op_remove_interface(cc, vif, true);
		break;
	}
	WARN_ON(iter != wlvif);

out:
	mutex_unlock(&cc->mutex);
}

static int cc33xx_op_change_interface(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      enum nl80211_iftype new_type, bool p2p)
{
	struct cc33xx *cc = hw->priv;
	int ret;

	set_bit(CC33XX_FLAG_VIF_CHANGE_IN_PROGRESS, &cc->flags);
	cc33xx_op_remove_interface(hw, vif);

	vif->type = new_type;
	vif->p2p = p2p;
	ret = cc33xx_op_add_interface(hw, vif);

	clear_bit(CC33XX_FLAG_VIF_CHANGE_IN_PROGRESS, &cc->flags);
	return ret;
}

static int cc33xx_join(struct cc33xx *cc, struct cc33xx_vif *wlvif)
{
	int ret;
	bool is_ibss = (wlvif->bss_type == BSS_TYPE_IBSS);

	/* One of the side effects of the JOIN command is that is clears
	 * WPA/WPA2 keys from the chipset. Performing a JOIN while associated
	 * to a WPA/WPA2 access point will therefore kill the data-path.
	 * Currently the only valid scenario for JOIN during association
	 * is on roaming, in which case we will also be given new keys.
	 * Keep the below message for now, unless it starts bothering
	 * users who really like to roam a lot :)
	 */
	if (test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
		cc33xx_info("JOIN while associated.");

	/* clear encryption type */
	wlvif->encryption_type = KEY_NONE;

	if (is_ibss) {
		ret = cc33xx_cmd_role_start_ibss(cc, wlvif);
	} else {
		if (cc->quirks & CC33XX_QUIRK_START_STA_FAILS) {
			/* TODO: this is an ugly workaround for wl12xx fw
			 * bug - we are not able to tx/rx after the first
			 * start_sta, so make dummy start+stop calls,
			 * and then call start_sta again.
			 * this should be fixed in the fw.
			 */
			cc33xx_cmd_role_start_sta(cc, wlvif);
			cc33xx_cmd_role_stop_sta(cc, wlvif);
		}

		ret = cc33xx_cmd_role_start_sta(cc, wlvif);
	}

	return ret;
}

static int cc33xx_ssid_set(struct cc33xx_vif *wlvif,
			   struct sk_buff *skb, int offset)
{
	u8 ssid_len;
	const u8 *ptr = cfg80211_find_ie(WLAN_EID_SSID, skb->data + offset,
					 skb->len - offset);

	if (!ptr) {
		cc33xx_error("No SSID in IEs!");
		return -ENOENT;
	}

	ssid_len = ptr[1];
	if (ssid_len > IEEE80211_MAX_SSID_LEN) {
		cc33xx_error("SSID is too long!");
		return -EINVAL;
	}

	wlvif->ssid_len = ssid_len;
	memcpy(wlvif->ssid, ptr + 2, ssid_len);
	return 0;
}

static int cc33xx_set_ssid(struct cc33xx *cc, struct cc33xx_vif *wlvif)
{
	struct ieee80211_vif *vif = cc33xx_wlvif_to_vif(wlvif);
	struct sk_buff *skb;
	int ieoffset;

	/* we currently only support setting the ssid from the ap probe req */
	if (wlvif->bss_type != BSS_TYPE_STA_BSS)
		return -EINVAL;

	skb = ieee80211_ap_probereq_get(cc->hw, vif);
	if (!skb)
		return -EINVAL;

	ieoffset = offsetof(struct ieee80211_mgmt, u.probe_req.variable);
	cc33xx_ssid_set(wlvif, skb, ieoffset);
	dev_kfree_skb(skb);

	return 0;
}

static int cc33xx_set_assoc(struct cc33xx *cc, struct cc33xx_vif *wlvif,
			    struct ieee80211_bss_conf *bss_conf,
			    struct ieee80211_sta *sta,
			    struct ieee80211_vif *vif, u32 sta_rate_set)
{
	int ret;

	wlvif->aid = vif->cfg.aid;
	wlvif->channel_type = cfg80211_get_chandef_type(&bss_conf->chandef);
	wlvif->beacon_int = bss_conf->beacon_int;
	wlvif->wmm_enabled = bss_conf->qos;

	wlvif->nontransmitted = bss_conf->nontransmitted;
	cc33xx_debug(DEBUG_MAC80211, "set_assoc mbssid params: nonTxbssid: %d, idx: %d, max_ind: %d, trans_bssid: %pM, ema_ap: %d",
		     bss_conf->nontransmitted, bss_conf->bssid_index,
		     bss_conf->bssid_indicator, bss_conf->transmitter_bssid,
		     bss_conf->ema_ap);
	wlvif->bssid_index = bss_conf->bssid_index;
	wlvif->bssid_indicator = bss_conf->bssid_indicator;
	memcpy(wlvif->transmitter_bssid, bss_conf->transmitter_bssid, ETH_ALEN);

	set_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags);

	ret = cc33xx_assoc_info_cfg(cc, wlvif, sta, wlvif->aid);
	if (ret < 0)
		return ret;

	if (sta_rate_set) {
		wlvif->rate_set = cc33xx_tx_enabled_rates_get(cc, sta_rate_set,
							      wlvif->band);
	}

	return ret;
}

static int cc33xx_unset_assoc(struct cc33xx *cc, struct cc33xx_vif *wlvif)
{
	int ret;
	bool sta = wlvif->bss_type == BSS_TYPE_STA_BSS;

	/* make sure we are connected (sta) joined */
	if (sta && !test_and_clear_bit(WLVIF_FLAG_STA_ASSOCIATED,
				       &wlvif->flags))
		return false;

	/* make sure we are joined (ibss) */
	if (!sta && test_and_clear_bit(WLVIF_FLAG_IBSS_JOINED, &wlvif->flags))
		return false;

	if (sta) {
		/* use defaults when not associated */
		wlvif->aid = 0;

		/* free probe-request template */
		dev_kfree_skb(wlvif->probereq);
		wlvif->probereq = NULL;

		/* disable beacon filtering */
		ret = cc33xx_acx_beacon_filter_opt(cc, wlvif, false);
		if (ret < 0)
			return ret;
	}

	if (test_and_clear_bit(WLVIF_FLAG_CS_PROGRESS, &wlvif->flags)) {
		struct ieee80211_vif *vif = cc33xx_wlvif_to_vif(wlvif);

		cc33xx_cmd_stop_channel_switch(cc, wlvif);
		ieee80211_chswitch_done(vif, false);
		cancel_delayed_work(&wlvif->channel_switch_work);
	}

	return 0;
}

static void cc33xx_set_band_rate(struct cc33xx *cc, struct cc33xx_vif *wlvif)
{
	wlvif->basic_rate_set = wlvif->bitrate_masks[wlvif->band];
	wlvif->rate_set = wlvif->basic_rate_set;
}

static void cc33xx_sta_handle_idle(struct cc33xx *cc,
				   struct cc33xx_vif *wlvif, bool idle)
{
	bool cur_idle = !test_bit(WLVIF_FLAG_ACTIVE, &wlvif->flags);

	if (idle == cur_idle)
		return;

	if (idle) {
		clear_bit(WLVIF_FLAG_ACTIVE, &wlvif->flags);
	} else {
		/* The current firmware only supports sched_scan in idle */
		if (cc->sched_vif == wlvif)
			cc33xx_scan_sched_scan_stop(cc, wlvif);

		set_bit(WLVIF_FLAG_ACTIVE, &wlvif->flags);
	}
}

static int cc33xx_config_vif(struct cc33xx *cc, struct cc33xx_vif *wlvif,
			     struct ieee80211_conf *conf, u64 changed)
{
	int ret;

	if (cc33xx_is_p2p_mgmt(wlvif))
		return 0;

	if (conf->power_level != wlvif->power_level &&
	    (changed & IEEE80211_CONF_CHANGE_POWER)) {
		ret = cc33xx_acx_tx_power(cc, wlvif, conf->power_level);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int cc33xx_op_config(struct ieee80211_hw *hw, u32 changed)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif;
	struct ieee80211_conf *conf = &hw->conf;
	int ret = 0;

	cc33xx_debug(DEBUG_MAC80211,
		     "mac80211 config psm %s power %d %s changed 0x%x",
		     conf->flags & IEEE80211_CONF_PS ? "on" : "off",
		     conf->power_level,
		     conf->flags & IEEE80211_CONF_IDLE ? "idle" : "in use",
		     changed);

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	/* configure each interface */
	cc33xx_for_each_wlvif(cc, wlvif) {
		ret = cc33xx_config_vif(cc, wlvif, conf, changed);
		if (ret < 0)
			goto out;
	}

out:
	mutex_unlock(&cc->mutex);

	return ret;
}

struct cc33xx_filter_params {
	bool enabled;
	int mc_list_length;
	u8 mc_list[ACX_MC_ADDRESS_GROUP_MAX][ETH_ALEN];
};

static u64 cc33xx_op_prepare_multicast(struct ieee80211_hw *hw,
				       struct netdev_hw_addr_list *mc_list)
{
	struct cc33xx_filter_params *fp;
	struct netdev_hw_addr *ha;

	fp = kzalloc(sizeof(*fp), GFP_ATOMIC);
	if (!fp) {
		cc33xx_error("Out of memory setting filters.");
		return 0;
	}

	/* update multicast filtering parameters */
	fp->mc_list_length = 0;
	if (netdev_hw_addr_list_count(mc_list) > ACX_MC_ADDRESS_GROUP_MAX) {
		fp->enabled = false;
		cc33xx_debug(DEBUG_MAC80211, "mac80211 prepare multicast: too many addresses received, disable multicast filtering");
	} else {
		fp->enabled = true;
		netdev_hw_addr_list_for_each(ha, mc_list) {
			memcpy(fp->mc_list[fp->mc_list_length],
			       ha->addr, ETH_ALEN);
			fp->mc_list_length++;
		}
	}

	return (u64)(unsigned long)fp;
}

#define CC33XX_SUPPORTED_FILTERS (FIF_ALLMULTI)

static void cc33xx_op_configure_filter(struct ieee80211_hw *hw,
				       unsigned int changed,
				       unsigned int *total, u64 multicast)
{
	struct cc33xx_filter_params *fp = (void *)(unsigned long)multicast;
	struct cc33xx *cc = hw->priv;

	cc33xx_debug(DEBUG_MAC80211,
		     "mac80211 configure filter, FIF_ALLMULTI = %d",
		     *total & FIF_ALLMULTI);

	mutex_lock(&cc->mutex);

	*total &= CC33XX_SUPPORTED_FILTERS;

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	if (!fp)
		cc33xx_acx_group_address_tbl(cc, false, NULL, 0);
	else if (*total & FIF_ALLMULTI || !fp->enabled)
		cc33xx_acx_group_address_tbl(cc, false, NULL, 0);
	else
		cc33xx_acx_group_address_tbl(cc, true, fp->mc_list, fp->mc_list_length);

out:
	mutex_unlock(&cc->mutex);
	kfree(fp);
}

static int cc33xx_record_ap_key(struct cc33xx *cc, struct cc33xx_vif *wlvif,
				u8 id, u8 key_type, u8 key_size, const u8 *key,
				u8 hlid, u32 tx_seq_32,	u16 tx_seq_16)
{
	struct cc33xx_ap_key *ap_key;
	int i;

	cc33xx_debug(DEBUG_CRYPT, "record ap key id %d", (int)id);

	if (key_size > MAX_KEY_SIZE)
		return -EINVAL;

	/* Find next free entry in ap_keys. Also check we are not replacing
	 * an existing key.
	 */
	for (i = 0; i < MAX_NUM_KEYS; i++) {
		if (!wlvif->ap.recorded_keys[i])
			break;

		if (wlvif->ap.recorded_keys[i]->id == id) {
			cc33xx_warning("trying to record key replacement");
			return -EINVAL;
		}
	}

	if (i == MAX_NUM_KEYS)
		return -EBUSY;

	ap_key = kzalloc(sizeof(*ap_key), GFP_KERNEL);
	if (!ap_key)
		return -ENOMEM;

	ap_key->id = id;
	ap_key->key_type = key_type;
	ap_key->key_size = key_size;
	memcpy(ap_key->key, key, key_size);
	ap_key->hlid = hlid;
	ap_key->tx_seq_32 = tx_seq_32;
	ap_key->tx_seq_16 = tx_seq_16;

	wlvif->ap.recorded_keys[i] = ap_key;
	return 0;
}

static void cc33xx_free_ap_keys(struct cc33xx *cc, struct cc33xx_vif *wlvif)
{
	int i;

	for (i = 0; i < MAX_NUM_KEYS; i++) {
		kfree(wlvif->ap.recorded_keys[i]);
		wlvif->ap.recorded_keys[i] = NULL;
	}
}

static int cc33xx_ap_init_hwenc(struct cc33xx *cc, struct cc33xx_vif *wlvif)
{
	int i, ret = 0;
	struct cc33xx_ap_key *key;
	bool wep_key_added = false;

	for (i = 0; i < MAX_NUM_KEYS; i++) {
		u8 hlid;

		if (!wlvif->ap.recorded_keys[i])
			break;

		key = wlvif->ap.recorded_keys[i];
		hlid = key->hlid;
		if (hlid == CC33XX_INVALID_LINK_ID)
			hlid = wlvif->ap.bcast_hlid;

		ret = cc33xx_cmd_set_ap_key(cc, wlvif, KEY_ADD_OR_REPLACE,
					    key->id, key->key_type,
					    key->key_size, key->key, hlid,
					    key->tx_seq_32, key->tx_seq_16);
		if (ret < 0)
			goto out;

		if (key->key_type == KEY_WEP)
			wep_key_added = true;
	}

	if (wep_key_added) {
		ret = cc33xx_cmd_set_default_wep_key(cc, wlvif->default_key,
						     wlvif->ap.bcast_hlid);
		if (ret < 0)
			goto out;
	}

out:
	cc33xx_free_ap_keys(cc, wlvif);
	return ret;
}

static int cc33xx_config_key(struct cc33xx *cc, struct cc33xx_vif *wlvif,
			     u16 action, u8 id, u8 key_type, u8 key_size,
			     const u8 *key, u32 tx_seq_32, u16 tx_seq_16,
			     struct ieee80211_sta *sta)
{
	int ret;
	bool is_ap = (wlvif->bss_type == BSS_TYPE_AP_BSS);

	if (is_ap) {
		struct cc33xx_station *wl_sta;
		u8 hlid;

		if (sta) {
			wl_sta = (struct cc33xx_station *)sta->drv_priv;
			hlid = wl_sta->hlid;
		} else {
			hlid = wlvif->ap.bcast_hlid;
		}

		if (!test_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags)) {
			/* We do not support removing keys after AP shutdown.
			 * Pretend we do to make mac80211 happy.
			 */
			if (action != KEY_ADD_OR_REPLACE)
				return 0;

			ret = cc33xx_record_ap_key(cc, wlvif, id, key_type,
						   key_size, key, hlid,
						   tx_seq_32, tx_seq_16);
		} else {
			ret = cc33xx_cmd_set_ap_key(cc, wlvif, action, id,
						    key_type, key_size, key,
						    hlid, tx_seq_32, tx_seq_16);
		}

		if (ret < 0)
			return ret;
	} else {
		const u8 *addr;
		static const u8 bcast_addr[ETH_ALEN] = {
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff
		};

		addr = sta ? sta->addr : bcast_addr;

		if (is_zero_ether_addr(addr)) {
			/* We dont support TX only encryption */
			return -EOPNOTSUPP;
		}

		/* The cc33xx does not allow to remove unicast keys - they
		 * will be cleared automatically on next CMD_JOIN. Ignore the
		 * request silently, as we dont want the mac80211 to emit
		 * an error message.
		 */
		if (action == KEY_REMOVE && !is_broadcast_ether_addr(addr))
			return 0;

		/* don't remove key if hlid was already deleted */
		if (action == KEY_REMOVE &&
		    wlvif->sta.hlid == CC33XX_INVALID_LINK_ID)
			return 0;

		ret = cc33xx_cmd_set_sta_key(cc, wlvif, action, id, key_type,
					     key_size, key, addr, tx_seq_32,
					     tx_seq_16);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int cc33xx_set_key(struct cc33xx *cc, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key_conf)
{
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	int ret;
	u32 tx_seq_32 = 0;
	u16 tx_seq_16 = 0;
	u8 key_type;
	u8 hlid;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 set key");

	cc33xx_debug(DEBUG_CRYPT, "CMD: 0x%x sta: %p", cmd, sta);
	cc33xx_debug(DEBUG_CRYPT, "Key: algo:0x%x, id:%d, len:%d flags 0x%x",
		     key_conf->cipher, key_conf->keyidx,
		     key_conf->keylen, key_conf->flags);
	cc33xx_dump(DEBUG_CRYPT, "KEY: ", key_conf->key, key_conf->keylen);

	if (wlvif->bss_type == BSS_TYPE_AP_BSS) {
		if (sta) {
			struct cc33xx_station *wl_sta = (void *)sta->drv_priv;

			hlid = wl_sta->hlid;
		} else {
			hlid = wlvif->ap.bcast_hlid;
		}
	} else {
		hlid = wlvif->sta.hlid;
	}

	if (hlid != CC33XX_INVALID_LINK_ID) {
		u64 tx_seq = cc->links[hlid].total_freed_pkts;

		tx_seq_32 = CC33XX_TX_SECURITY_HI32(tx_seq);
		tx_seq_16 = CC33XX_TX_SECURITY_LO16(tx_seq);
	}

	switch (key_conf->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		key_type = KEY_WEP;
		key_conf->hw_key_idx = key_conf->keyidx;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		key_type = KEY_TKIP;
		key_conf->hw_key_idx = key_conf->keyidx;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key_type = KEY_AES;
		key_conf->flags |= IEEE80211_KEY_FLAG_PUT_IV_SPACE;
		break;
	case WLAN_CIPHER_SUITE_GCMP:
		key_type = KEY_GCMP128;
		key_conf->flags |= IEEE80211_KEY_FLAG_PUT_IV_SPACE;
		break;
	case WLAN_CIPHER_SUITE_CCMP_256:
		key_type = KEY_CCMP256;
		key_conf->flags |= IEEE80211_KEY_FLAG_PUT_IV_SPACE;
		break;
	case WLAN_CIPHER_SUITE_GCMP_256:
		key_type = KEY_GCMP_256;
		key_conf->flags |= IEEE80211_KEY_FLAG_PUT_IV_SPACE;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		key_type = KEY_IGTK;
		break;
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		key_type = KEY_CMAC_256;
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		key_type =  KEY_GMAC_128;
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		key_type =  KEY_GMAC_256;
		break;
	case CC33XX_CIPHER_SUITE_GEM:
		key_type = KEY_GEM;
		break;
	default:
		cc33xx_error("Unknown key algo 0x%x", key_conf->cipher);

		return -EOPNOTSUPP;
	}

	switch (cmd) {
	case SET_KEY:
		ret = cc33xx_config_key(cc, wlvif, KEY_ADD_OR_REPLACE,
					key_conf->keyidx, key_type, key_conf->keylen,
				 key_conf->key, tx_seq_32, tx_seq_16, sta);
		if (ret < 0) {
			cc33xx_error("Could not add or replace key");
			return ret;
		}

		/* reconfiguring arp response if the unicast (or common)
		 * encryption key type was changed
		 */
		if (wlvif->bss_type == BSS_TYPE_STA_BSS &&
		    (sta || key_type == KEY_WEP) &&
		    wlvif->encryption_type != key_type) {
			wlvif->encryption_type = key_type;
			if (ret < 0) {
				cc33xx_warning("build arp rsp failed: %d", ret);
				return ret;
			}
		}
		break;

	case DISABLE_KEY:
		ret = cc33xx_config_key(cc, wlvif, KEY_REMOVE, key_conf->keyidx,
					key_type, key_conf->keylen,
					key_conf->key, 0, 0, sta);
		if (ret < 0) {
			cc33xx_error("Could not remove key");
			return ret;
		}
		break;

	default:
		cc33xx_error("Unsupported key cmd 0x%x", cmd);
		return -EOPNOTSUPP;
	}

	return ret;
}

static int cc33xx_hw_set_key(struct cc33xx *cc, enum set_key_cmd cmd,
			     struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key_conf)
{
	bool special_enc;
	int ret;

	cc33xx_debug(DEBUG_CRYPT, "extra spare keys before: %d",
		     cc->extra_spare_key_count);

	special_enc = key_conf->cipher == CC33XX_CIPHER_SUITE_GEM ||
		      key_conf->cipher == WLAN_CIPHER_SUITE_TKIP;

	ret = cc33xx_set_key(cc, cmd, vif, sta, key_conf);
	if (ret < 0)
		goto out;

	/* when adding the first or removing the last GEM/TKIP key,
	 * we have to adjust the number of spare blocks.
	 */
	if (special_enc) {
		if (cmd == SET_KEY) {
			/* first key */
			cc->extra_spare_key_count++;
		} else if (cmd == DISABLE_KEY) {
			/* last key */
			cc->extra_spare_key_count--;
		}
	}

	cc33xx_debug(DEBUG_CRYPT, "extra spare keys after: %d",
		     cc->extra_spare_key_count);

out:
	return ret;
}

static int cc33xx_op_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct ieee80211_key_conf *key_conf)
{
	struct cc33xx *cc = hw->priv;
	int ret;
	bool might_change_spare = key_conf->cipher == CC33XX_CIPHER_SUITE_GEM ||
				key_conf->cipher == WLAN_CIPHER_SUITE_TKIP;

	if (might_change_spare) {
		/* stop the queues and flush to ensure the next packets are
		 * in sync with FW spare block accounting
		 */
		cc33xx_stop_queues(cc, CC33XX_QUEUE_STOP_REASON_SPARE_BLK);
		cc33xx_tx_flush(cc);
	}

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON)) {
		ret = -EAGAIN;
		goto out_wake_queues;
	}

	ret = cc33xx_hw_set_key(cc, cmd, vif, sta, key_conf);

out_wake_queues:
	if (might_change_spare)
		cc33xx_wake_queues(cc, CC33XX_QUEUE_STOP_REASON_SPARE_BLK);

	mutex_unlock(&cc->mutex);

	return ret;
}

static void cc33xx_op_set_default_key_idx(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  int key_idx)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);

	cc33xx_debug(DEBUG_MAC80211,
		     "mac80211 set default key idx %d", key_idx);

	/* we don't handle unsetting of default key */
	if (key_idx == -1)
		return;

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out_unlock;

	wlvif->default_key = key_idx;

	/* the default WEP key needs to be configured at least once */
	if (wlvif->encryption_type == KEY_WEP)
		cc33xx_cmd_set_default_wep_key(cc, key_idx, wlvif->sta.hlid);

out_unlock:
	mutex_unlock(&cc->mutex);
}

static int cc33xx_op_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			     struct ieee80211_scan_request *hw_req)
{
	struct cfg80211_scan_request *req = &hw_req->req;
	struct cc33xx *cc = hw->priv;
	int ret;
	u8 *ssid = NULL;
	size_t len = 0;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 hw scan");

	if (req->n_ssids) {
		ssid = req->ssids[0].ssid;
		len = req->ssids[0].ssid_len;
	}

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON)) {
		/* We cannot return -EBUSY here because cfg80211 will expect
		 * a call to ieee80211_scan_completed if we do - in this case
		 * there won't be any call.
		 */
		ret = -EAGAIN;
		goto out;
	}

	/* fail if there is any role in ROC */
	if (find_first_bit(cc->roc_map, CC33XX_MAX_ROLES) < CC33XX_MAX_ROLES) {
		/* don't allow scanning right now */
		ret = -EBUSY;
		goto out;
	}

	ret = cc33xx_scan(hw->priv, vif, ssid, len, req);

out:
	mutex_unlock(&cc->mutex);

	return ret;
}

static void cc33xx_op_cancel_hw_scan(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	struct cfg80211_scan_info info = {
		.aborted = true,
	};
	int ret;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 cancel hw scan");

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	if (cc->scan.state == CC33XX_SCAN_STATE_IDLE)
		goto out;

	if (cc->scan.state != CC33XX_SCAN_STATE_DONE) {
		ret = cc33xx_scan_stop(cc, wlvif);
		if (ret < 0)
			goto out;
	}

	/* Rearm the tx watchdog just before idling scan. This
	 * prevents just-finished scans from triggering the watchdog
	 */
	cc33xx_rearm_tx_watchdog_locked(cc);

	cc->scan.state = CC33XX_SCAN_STATE_IDLE;
	memset(cc->scan.scanned_ch, 0, sizeof(cc->scan.scanned_ch));
	cc->scan_wlvif = NULL;
	cc->scan.req = NULL;
	ieee80211_scan_completed(cc->hw, &info);

out:
	mutex_unlock(&cc->mutex);

	cancel_delayed_work_sync(&cc->scan_complete_work);
}

static int cc33xx_op_sched_scan_start(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct cfg80211_sched_scan_request *req,
				      struct ieee80211_scan_ies *ies)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	int ret;

	cc33xx_debug(DEBUG_MAC80211, "cc33xx_op_sched_scan_start");

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = cc33xx_sched_scan_start(cc, wlvif, req, ies);
	if (ret < 0)
		goto out;

	cc->sched_vif = wlvif;

out:
	mutex_unlock(&cc->mutex);
	return ret;
}

static int cc33xx_op_sched_scan_stop(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);

	cc33xx_debug(DEBUG_MAC80211, "cc33xx_op_sched_scan_stop");

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	/* command to stop periodic scan was sent from mac80211
	 * mark than stop command is from mac80211 and release sched_vif
	 */
	cc->mac80211_scan_stopped = true;
	cc->sched_vif = NULL;
	cc33xx_scan_sched_scan_stop(cc, wlvif);

out:
	mutex_unlock(&cc->mutex);

	return 0;
}

static int cc33xx_op_set_frag_threshold(struct ieee80211_hw *hw, u32 value)
{
	return 0;
}

static int cc33xx_op_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	return 0;
}

static int cc33xx_bss_erp_info_changed(struct cc33xx *cc,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *bss_conf,
				       u64 changed)
{
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	int ret = 0;

	if (changed & BSS_CHANGED_ERP_SLOT) {
		if (bss_conf->use_short_slot)
			ret = cc33xx_acx_slot(cc, wlvif, SLOT_TIME_SHORT);
		else
			ret = cc33xx_acx_slot(cc, wlvif, SLOT_TIME_LONG);
		if (ret < 0) {
			cc33xx_warning("Set slot time failed %d", ret);
			goto out;
		}
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		if (bss_conf->use_short_preamble)
			cc33xx_acx_set_preamble(cc, wlvif, ACX_PREAMBLE_SHORT);
		else
			cc33xx_acx_set_preamble(cc, wlvif, ACX_PREAMBLE_LONG);
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		if (bss_conf->use_cts_prot) {
			ret = cc33xx_acx_cts_protect(cc, wlvif,
						     CTSPROTECT_ENABLE);
		} else {
			ret = cc33xx_acx_cts_protect(cc, wlvif,
						     CTSPROTECT_DISABLE);
		}

		if (ret < 0) {
			cc33xx_warning("Set ctsprotect failed %d", ret);
			goto out;
		}
	}

out:
	return ret;
}

static int cc33xx_set_beacon_template(struct cc33xx *cc,
				      struct ieee80211_vif *vif, bool is_ap)
{
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	int ret;
	int ieoffset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	struct sk_buff *beacon = ieee80211_beacon_get(cc->hw, vif, 0);

	struct cc33xx_cmd_set_beacon_info *cmd;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	if (!beacon) {
		ret = -EINVAL;
		goto end_bcn;
	}

	cc33xx_debug(DEBUG_MASTER, "beacon updated");

	ret = cc33xx_ssid_set(wlvif, beacon, ieoffset);
	if (ret < 0)
		goto end_bcn;

	cmd->role_id =  wlvif->role_id;
	cmd->beacon_len = cpu_to_le16(beacon->len);

	memcpy(cmd->beacon, beacon->data, beacon->len);

	ret = cc33xx_cmd_send(cc, CMD_AP_SET_BEACON_INFO, cmd, sizeof(*cmd), 0);
	if (ret < 0)
		goto end_bcn;

end_bcn:
	dev_kfree_skb(beacon);
	kfree(cmd);
out:
	return ret;
}

static int cc33xx_bss_beacon_info_changed(struct cc33xx *cc,
					  struct ieee80211_vif *vif,
					  struct ieee80211_bss_conf *bss_conf,
					  u32 changed)
{
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	bool is_ap = (wlvif->bss_type == BSS_TYPE_AP_BSS);
	int ret = 0;

	if (changed & BSS_CHANGED_BEACON_INT) {
		cc33xx_debug(DEBUG_MASTER, "beacon interval updated: %d",
			     bss_conf->beacon_int);

		wlvif->beacon_int = bss_conf->beacon_int;
	}

	if (changed & BSS_CHANGED_BEACON) {
		ret = cc33xx_set_beacon_template(cc, vif, is_ap);
		if (ret < 0)
			goto out;

		if (test_and_clear_bit(WLVIF_FLAG_BEACON_DISABLED,
				       &wlvif->flags)) {
			ret = cmd_dfs_master_restart(cc, wlvif);
			if (ret < 0)
				goto out;
		}
	}
out:
	if (ret != 0)
		cc33xx_error("beacon info change failed: %d", ret);

	return ret;
}

/* AP mode changes */
static void cc33xx_bss_info_changed_ap(struct cc33xx *cc,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *bss_conf,
				       u64 changed)
{
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	int ret = 0;

	if (changed & BSS_CHANGED_BASIC_RATES) {
		u32 rates = bss_conf->basic_rates;
		u32 supported_rates = 0;

		wlvif->basic_rate_set = cc33xx_tx_enabled_rates_get(cc, rates,
								    wlvif->band);
		wlvif->basic_rate = cc33xx_tx_min_rate_get(cc,
							   wlvif->basic_rate_set);

		supported_rates = CONF_TX_ENABLED_RATES | CONF_TX_MCS_RATES;
		ret = cc33xx_update_ap_rates(cc, wlvif->role_id,
					     wlvif->basic_rate_set,
					     supported_rates);

		ret = cc33xx_set_beacon_template(cc, vif, true);
		if (ret < 0)
			goto out;
	}

	ret = cc33xx_bss_beacon_info_changed(cc, vif, bss_conf, changed);
	if (ret < 0)
		goto out;

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		if (bss_conf->enable_beacon) {
			if (!test_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags)) {
				ret = cc33xx_cmd_role_start_ap(cc, wlvif);
				if (ret < 0)
					goto out;

				ret = cc33xx_ap_init_hwenc(cc, wlvif);
				if (ret < 0)
					goto out;

				set_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags);
				cc33xx_debug(DEBUG_AP, "started AP");
			}
		} else {
			if (test_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags)) {
				/* AP might be in ROC in case we have just
				 * sent auth reply. handle it.
				 */
				if (test_bit(wlvif->role_id, cc->roc_map))
					cc33xx_croc(cc, wlvif->role_id);

				ret = cc33xx_cmd_role_stop_ap(cc, wlvif);
				if (ret < 0)
					goto out;

				clear_bit(WLVIF_FLAG_AP_STARTED, &wlvif->flags);
				clear_bit(WLVIF_FLAG_AP_PROBE_RESP_SET,
					  &wlvif->flags);
				cc33xx_debug(DEBUG_AP, "stopped AP");
			}
		}
	}

	ret = cc33xx_bss_erp_info_changed(cc, vif, bss_conf, changed);
	if (ret < 0)
		goto out;

out:
	return;
}

static int cc33xx_set_bssid(struct cc33xx *cc, struct cc33xx_vif *wlvif,
			    struct ieee80211_bss_conf *bss_conf,
			struct ieee80211_vif *vif,
			u32 sta_rate_set)
{
	u32 rates;

	cc33xx_debug(DEBUG_MAC80211, "changed_bssid: %pM, aid: %d, bcn_int: %d, brates: 0x%x sta_rate_set: 0x%x, nontx: %d",
		     bss_conf->bssid, vif->cfg.aid, bss_conf->beacon_int,
			bss_conf->basic_rates, sta_rate_set,
			bss_conf->nontransmitted);

	wlvif->beacon_int = bss_conf->beacon_int;
	rates = bss_conf->basic_rates;
	wlvif->basic_rate_set =	cc33xx_tx_enabled_rates_get(cc, rates,
							    wlvif->band);
	wlvif->basic_rate = cc33xx_tx_min_rate_get(cc, wlvif->basic_rate_set);

	if (sta_rate_set) {
		wlvif->rate_set = cc33xx_tx_enabled_rates_get(cc, sta_rate_set,
							      wlvif->band);
	}

	wlvif->nontransmitted = bss_conf->nontransmitted;
	cc33xx_debug(DEBUG_MAC80211, "changed_mbssid: nonTxbssid: %d, idx: %d, max_ind: %d, trans_bssid: %pM, ema_ap: %d",
		     bss_conf->nontransmitted, bss_conf->bssid_index,
		     bss_conf->bssid_indicator, bss_conf->transmitter_bssid,
		     bss_conf->ema_ap);

	if (bss_conf->nontransmitted) {
		wlvif->bssid_index = bss_conf->bssid_index;
		wlvif->bssid_indicator = bss_conf->bssid_indicator;
		memcpy(wlvif->transmitter_bssid,
		       bss_conf->transmitter_bssid,
			ETH_ALEN);
	}

	/* we only support sched_scan while not connected */
	if (cc->sched_vif == wlvif)
		cc33xx_scan_sched_scan_stop(cc, wlvif);

	cc33xx_set_ssid(cc, wlvif);

	set_bit(WLVIF_FLAG_IN_USE, &wlvif->flags);

	return 0;
}

static int cc33xx_clear_bssid(struct cc33xx *cc, struct cc33xx_vif *wlvif)
{
	int ret;

	/* revert back to minimum rates for the current band */
	cc33xx_set_band_rate(cc, wlvif);
	wlvif->basic_rate = cc33xx_tx_min_rate_get(cc, wlvif->basic_rate_set);

	if (wlvif->bss_type == BSS_TYPE_STA_BSS &&
	    test_bit(WLVIF_FLAG_IN_USE, &wlvif->flags)) {
		ret = cc33xx_cmd_role_stop_sta(cc, wlvif);
		if (ret < 0)
			return ret;
	}

	clear_bit(WLVIF_FLAG_IN_USE, &wlvif->flags);
	return 0;
}

static void cc33xx_sta_set_he(struct cc33xx *cc, struct cc33xx_vif *wlvif, bool has_he)
{
	struct cc33xx_vif *wlvif_itr;
	u8 he_count = 0;

	wlvif->sta_has_he = has_he;

	if (has_he)
		cc33xx_info("HE Enabled");
	else
		cc33xx_info("HE Disabled");

	cc33xx_for_each_wlvif_sta(cc, wlvif_itr) {
		/* check for all valid link id's */
		if (wlvif_itr->role_id != 0xFF && wlvif_itr->sta_has_he)
			he_count++;
	}

	/* There can't be two stations connected with HE supported links */
	if (he_count > 1)
		cc33xx_error("Both station interfaces has HE enabled!");
}

/* STA/IBSS mode changes */
static void cc33xx_bss_info_changed_sta(struct cc33xx *cc,
					struct ieee80211_vif *vif,
					struct ieee80211_bss_conf *bss_conf,
					u64 changed)
{
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	bool do_join = false;
	bool is_ibss = (wlvif->bss_type == BSS_TYPE_IBSS);
	bool ibss_joined = false;
	u32 sta_rate_set = 0;
	int ret;
	struct ieee80211_sta *sta = NULL;
	bool sta_exists = false;
	struct ieee80211_sta_ht_cap sta_ht_cap;
	struct ieee80211_sta_he_cap sta_he_cap;

	if (is_ibss) {
		ret = cc33xx_bss_beacon_info_changed(cc, vif,
						     bss_conf, changed);
		if (ret < 0)
			goto out;
	}

	if (changed & BSS_CHANGED_IBSS) {
		if (vif->cfg.ibss_joined) {
			set_bit(WLVIF_FLAG_IBSS_JOINED, &wlvif->flags);
			ibss_joined = true;
		} else {
			cc33xx_unset_assoc(cc, wlvif);
			cc33xx_cmd_role_stop_sta(cc, wlvif);
		}
	}

	if ((changed & BSS_CHANGED_BEACON_INT) && ibss_joined)
		do_join = true;

	/* Need to update the SSID (for filtering etc) */
	if ((changed & BSS_CHANGED_BEACON) && ibss_joined)
		do_join = true;

	if ((changed & BSS_CHANGED_BEACON_ENABLED) && ibss_joined) {
		cc33xx_debug(DEBUG_ADHOC, "ad-hoc beaconing: %s",
			     bss_conf->enable_beacon ? "enabled" : "disabled");

		do_join = true;
	}

	if (changed & BSS_CHANGED_IDLE && !is_ibss)
		cc33xx_sta_handle_idle(cc, wlvif, vif->cfg.idle);

	if (changed & BSS_CHANGED_CQM)
		wlvif->rssi_thold = bss_conf->cqm_rssi_thold;

	if (changed & (BSS_CHANGED_BSSID | BSS_CHANGED_HT | BSS_CHANGED_ASSOC)) {
		rcu_read_lock();
		sta = ieee80211_find_sta(vif, bss_conf->bssid);
		if (sta) {
			u8 *rx_mask = sta->deflink.ht_cap.mcs.rx_mask;

			/* save the supp_rates of the ap */
			sta_rate_set = sta->deflink.supp_rates[wlvif->band];
			if (sta->deflink.ht_cap.ht_supported) {
				sta_rate_set |=
					(rx_mask[0] << HW_HT_RATES_OFFSET) |
					(rx_mask[1] << HW_MIMO_RATES_OFFSET);
			}
			sta_ht_cap = sta->deflink.ht_cap;
			sta_he_cap = sta->deflink.he_cap;
			sta_exists = true;
		}

		rcu_read_unlock();
	}

	if (changed & BSS_CHANGED_BSSID) {
		if (!is_zero_ether_addr(bss_conf->bssid)) {
			ret = cc33xx_set_bssid(cc, wlvif,
					       bss_conf, vif, sta_rate_set);
			if (ret < 0)
				goto out;

			/* Need to update the BSSID (for filtering etc) */
			do_join = true;
		} else {
			ret = cc33xx_clear_bssid(cc, wlvif);
			if (ret < 0)
				goto out;
		}
	}

	if (changed & BSS_CHANGED_IBSS) {
		cc33xx_debug(DEBUG_ADHOC, "ibss_joined: %d",
			     vif->cfg.ibss_joined);

		if (vif->cfg.ibss_joined) {
			u32 rates = bss_conf->basic_rates;

			wlvif->basic_rate_set =
				cc33xx_tx_enabled_rates_get(cc, rates,
							    wlvif->band);
			wlvif->basic_rate =
				cc33xx_tx_min_rate_get(cc,
						       wlvif->basic_rate_set);

			/* by default, use 11b + OFDM rates */
			wlvif->rate_set = CONF_TX_IBSS_DEFAULT_RATES;
		}
	}

	if ((changed & BSS_CHANGED_BEACON_INFO) && bss_conf->dtim_period) {
		/* enable beacon filtering */
		ret = cc33xx_acx_beacon_filter_opt(cc, wlvif, true);
		if (ret < 0)
			goto out;
	}

	ret = cc33xx_bss_erp_info_changed(cc, vif, bss_conf, changed);
	if (ret < 0)
		goto out;

	if (do_join) {
		ret = cc33xx_join(cc, wlvif);
		if (ret < 0) {
			cc33xx_warning("cmd join failed %d", ret);
			goto out;
		}
	}

	if (changed & BSS_CHANGED_ASSOC) {
		if (vif->cfg.assoc) {
			ret = cc33xx_set_assoc(cc, wlvif, bss_conf, sta, vif,
					       sta_rate_set);
			if (ret < 0)
				goto out;

			if (test_bit(WLVIF_FLAG_STA_AUTHORIZED, &wlvif->flags))
				cc33xx_set_authorized(cc, wlvif);

			if (sta)
				cc33xx_sta_set_he(cc, wlvif, sta->deflink.he_cap.has_he);

		} else {
			cc33xx_unset_assoc(cc, wlvif);
		}
	}

	if (changed & BSS_CHANGED_PS) {
		if (vif->cfg.ps &&
		    test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags) &&
		    !test_bit(WLVIF_FLAG_IN_PS, &wlvif->flags)) {
			int ps_mode;
			char *ps_mode_str;

			if (cc->conf.host_conf.conn.forced_ps) {
				ps_mode = STATION_POWER_SAVE_MODE;
				ps_mode_str = "forced";
			} else {
				ps_mode = STATION_AUTO_PS_MODE;
				ps_mode_str = "auto";
			}

			cc33xx_debug(DEBUG_PSM, "%s ps enabled", ps_mode_str);

			ret = cc33xx_ps_set_mode(cc, wlvif, ps_mode);
			if (ret < 0)
				cc33xx_warning("enter %s ps failed %d",
					       ps_mode_str, ret);
		} else if (!vif->cfg.ps && test_bit(WLVIF_FLAG_IN_PS,
						     &wlvif->flags)) {
			cc33xx_debug(DEBUG_PSM, "auto ps disabled");

			ret = cc33xx_ps_set_mode(cc, wlvif,
						 STATION_ACTIVE_MODE);
			if (ret < 0)
				cc33xx_warning("exit auto ps failed %d", ret);
		}
	}

	/* Handle new association with HT. Do this after join. */
	if (sta_exists) {
		bool enabled = bss_conf->chandef.width !=
						NL80211_CHAN_WIDTH_20_NOHT;
		cc33xx_debug(DEBUG_CMD, "cc33xx_hw_set_peer_cap %x",
			     wlvif->rate_set);
		ret = cc33xx_acx_set_peer_cap(cc, &sta_ht_cap, &sta_he_cap,
					      wlvif, enabled, wlvif->rate_set,
					      wlvif->sta.hlid);
		if (ret < 0) {
			cc33xx_warning("Set ht cap failed %d", ret);
			goto out;
		}

		if (enabled) {
			ret = cc33xx_acx_set_ht_information(cc, wlvif,
							    bss_conf->ht_operation_mode,
						bss_conf->he_oper.params,
						bss_conf->he_oper.nss_set);
			if (ret < 0) {
				cc33xx_warning("Set ht information failed %d",
					       ret);
				goto out;
			}
		}
	}

	/* Handle arp filtering. Done after join. */
	if ((changed & BSS_CHANGED_ARP_FILTER) ||
	    (!is_ibss && (changed & BSS_CHANGED_QOS))) {
		__be32 addr = vif->cfg.arp_addr_list[0];

		wlvif->sta.qos = bss_conf->qos;
		WARN_ON(wlvif->bss_type != BSS_TYPE_STA_BSS);

		if (vif->cfg.arp_addr_cnt == 1 && vif->cfg.assoc) {
			wlvif->ip_addr = addr;
			/* The template should have been configured only upon
			 * association. however, it seems that the correct ip
			 * isn't being set (when sending), so we have to
			 * reconfigure the template upon every ip change.
			 */
			if (ret < 0) {
				cc33xx_warning("build arp rsp failed: %d", ret);
				goto out;
			}

		} else {
			wlvif->ip_addr = 0;
		}

		if (ret < 0)
			goto out;
	}

out:
	return;
}

static void cc33xx_op_bss_info_changed(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *bss_conf,
				       u64 changed)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	bool is_ap = (wlvif->bss_type == BSS_TYPE_AP_BSS);
	int ret, set_power;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 bss info role %d changed 0x%x",
		     wlvif->role_id, (int)changed);

	/* make sure to cancel pending disconnections if our association
	 * state changed
	 */
	if (!is_ap && (changed & BSS_CHANGED_ASSOC))
		cancel_delayed_work_sync(&wlvif->connection_loss_work);

	if (is_ap && (changed & BSS_CHANGED_BEACON_ENABLED) &&
	    !bss_conf->enable_beacon)
		cc33xx_tx_flush(cc);

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	if (unlikely(!test_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags)))
		goto out;

	if ((changed & BSS_CHANGED_TXPOWER) && bss_conf->txpower != wlvif->power_level) {
		/* bss_conf->txpower is initialized with a default value,
		 * meaning the power has not been set and should be ignored, use
		 * max value instead
		 */
		set_power = (bss_conf->txpower == INT_MIN) ?
					   CC33XX_MAX_TXPWR : bss_conf->txpower;
		ret = cc33xx_acx_tx_power(cc, wlvif, set_power);

		if (ret < 0)
			goto out;
	}

	if (is_ap)
		cc33xx_bss_info_changed_ap(cc, vif, bss_conf, changed);
	else
		cc33xx_bss_info_changed_sta(cc, vif, bss_conf, changed);

out:
	mutex_unlock(&cc->mutex);
}

static int cc33xx_op_add_chanctx(struct ieee80211_hw *hw,
				 struct ieee80211_chanctx_conf *ctx)
{
	cc33xx_debug(DEBUG_MAC80211, "mac80211 add chanctx %d (type %d)",
		     ieee80211_frequency_to_channel(ctx->def.chan->center_freq),
		     cfg80211_get_chandef_type(&ctx->def));
	return 0;
}

static void cc33xx_op_remove_chanctx(struct ieee80211_hw *hw,
				     struct ieee80211_chanctx_conf *ctx)
{
	cc33xx_debug(DEBUG_MAC80211, "mac80211 remove chanctx %d (type %d)",
		     ieee80211_frequency_to_channel(ctx->def.chan->center_freq),
		     cfg80211_get_chandef_type(&ctx->def));
}

static void cc33xx_op_change_chanctx(struct ieee80211_hw *hw,
				     struct ieee80211_chanctx_conf *ctx,
				     u32 changed)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif;
	int channel = ieee80211_frequency_to_channel(ctx->def.chan->center_freq);

	cc33xx_debug(DEBUG_MAC80211,
		     "mac80211 change chanctx %d (type %d) changed 0x%x",
		     channel, cfg80211_get_chandef_type(&ctx->def), changed);

	mutex_lock(&cc->mutex);

	cc33xx_for_each_wlvif(cc, wlvif) {
		struct ieee80211_vif *vif = cc33xx_wlvif_to_vif(wlvif);

		rcu_read_lock();
		if (rcu_access_pointer(vif->bss_conf.chanctx_conf) != ctx) {
			rcu_read_unlock();
			continue;
		}
		rcu_read_unlock();

		/* start radar if needed */
		if (changed & IEEE80211_CHANCTX_CHANGE_RADAR &&
		    wlvif->bss_type == BSS_TYPE_AP_BSS &&
		    ctx->radar_enabled && !wlvif->radar_enabled &&
		    ctx->def.chan->dfs_state == NL80211_DFS_USABLE) {
			cc33xx_debug(DEBUG_MAC80211, "Start radar detection");
			cmd_set_cac(cc, wlvif, true);
			wlvif->radar_enabled = true;
		}
	}

	mutex_unlock(&cc->mutex);
}

static int cc33xx_op_assign_vif_chanctx(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_bss_conf *link_conf,
					struct ieee80211_chanctx_conf *ctx)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	int channel = ieee80211_frequency_to_channel(ctx->def.chan->center_freq);

	cc33xx_debug(DEBUG_MAC80211, "mac80211 assign chanctx (role %d) %d (type %d) (radar %d dfs_state %d)",
		     wlvif->role_id,
		     channel, cfg80211_get_chandef_type(&ctx->def),
		     ctx->radar_enabled, ctx->def.chan->dfs_state);

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	if (unlikely(!test_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags)))
		goto out;

	wlvif->band = ctx->def.chan->band;
	wlvif->channel = channel;
	wlvif->channel_type = cfg80211_get_chandef_type(&ctx->def);

	/* update default rates according to the band */
	cc33xx_set_band_rate(cc, wlvif);

	if (ctx->radar_enabled && ctx->def.chan->dfs_state == NL80211_DFS_USABLE) {
		cc33xx_debug(DEBUG_MAC80211, "Start radar detection");
		cmd_set_cac(cc, wlvif, true);
		wlvif->radar_enabled = true;
	}

out:
	mutex_unlock(&cc->mutex);

	return 0;
}

static void cc33xx_op_unassign_vif_chanctx(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   struct ieee80211_bss_conf *link_conf,
					   struct ieee80211_chanctx_conf *ctx)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);

	cc33xx_debug(DEBUG_MAC80211,
		     "mac80211 unassign chanctx (role %d) %d (type %d)",
		     wlvif->role_id,
		     ieee80211_frequency_to_channel(ctx->def.chan->center_freq),
		     cfg80211_get_chandef_type(&ctx->def));

	cc33xx_tx_flush(cc);

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	if (unlikely(!test_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags)))
		goto out;

	if (wlvif->radar_enabled) {
		cc33xx_debug(DEBUG_MAC80211, "Stop radar detection");
		cmd_set_cac(cc, wlvif, false);
		wlvif->radar_enabled = false;
	}

out:
	mutex_unlock(&cc->mutex);
}

static int cc33xx_switch_vif_chan(struct cc33xx *cc, struct cc33xx_vif *wlvif,
				  struct ieee80211_chanctx_conf *new_ctx)
{
	int channel = ieee80211_frequency_to_channel(new_ctx->def.chan->center_freq);

	cc33xx_debug(DEBUG_MAC80211,
		     "switch vif (role %d) %d -> %d chan_type: %d",
		     wlvif->role_id, wlvif->channel, channel,
		     cfg80211_get_chandef_type(&new_ctx->def));

	cc33xx_debug(DEBUG_MAC80211, "switch vif bss_type: %d", wlvif->bss_type);

	wlvif->band = new_ctx->def.chan->band;
	wlvif->channel = channel;
	wlvif->channel_type = cfg80211_get_chandef_type(&new_ctx->def);

	if (wlvif->bss_type != BSS_TYPE_AP_BSS)
		return 0;

	WARN_ON(!test_bit(WLVIF_FLAG_BEACON_DISABLED, &wlvif->flags));

	if (wlvif->radar_enabled) {
		cc33xx_debug(DEBUG_MAC80211, "Stop radar detection");
		cmd_set_cac(cc, wlvif, false);
		wlvif->radar_enabled = false;
	}

	/* start radar if needed */
	if (new_ctx->radar_enabled) {
		cc33xx_debug(DEBUG_MAC80211, "Start radar detection");
		cmd_set_cac(cc, wlvif, true);
		wlvif->radar_enabled = true;
	}

	return 0;
}

static int cc33xx_op_switch_vif_chanctx(struct ieee80211_hw *hw,
					struct ieee80211_vif_chanctx_switch *vifs,
					int n_vifs,
					enum ieee80211_chanctx_switch_mode mode)
{
	struct cc33xx *cc = hw->priv;
	int i, ret;

	cc33xx_debug(DEBUG_MAC80211,
		     "mac80211 switch chanctx n_vifs %d mode %d", n_vifs, mode);

	mutex_lock(&cc->mutex);

	for (i = 0; i < n_vifs; i++) {
		struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vifs[i].vif);

		ret = cc33xx_switch_vif_chan(cc, wlvif, vifs[i].new_ctx);
		if (ret)
			goto out;
	}

out:
	mutex_unlock(&cc->mutex);

	return 0;
}

static int cc33xx_op_conf_tx(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     unsigned int link_id, u16 queue,
			     const struct ieee80211_tx_queue_params *params)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	u8 ps_scheme;
	int ret = 0;

	if (cc33xx_is_p2p_mgmt(wlvif))
		return 0;

	mutex_lock(&cc->mutex);

	cc33xx_debug(DEBUG_MAC80211, "mac80211 conf tx %d", queue);

	if (params->uapsd)
		ps_scheme = CONF_PS_SCHEME_UPSD_TRIGGER;
	else
		ps_scheme = CONF_PS_SCHEME_LEGACY;

	if (!test_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags))
		goto out;

	ret = cc33xx_tx_param_cfg(cc, wlvif, cc33xx_tx_get_queue(queue),
				  params->cw_min, params->cw_max, params->aifs,
			      params->txop << 5, params->acm, ps_scheme,
			      params->mu_edca, params->mu_edca_param_rec.aifsn,
			      params->mu_edca_param_rec.ecw_min_max,
			      params->mu_edca_param_rec.mu_edca_timer);

out:
	mutex_unlock(&cc->mutex);

	return ret;
}

static u64 cc33xx_op_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	u64 mactime = ULLONG_MAX;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 get tsf");

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	cc33xx_acx_tsf_info(cc, wlvif, &mactime);

out:
	mutex_unlock(&cc->mutex);

	return mactime;
}

static int cc33xx_op_get_survey(struct ieee80211_hw *hw, int idx,
				struct survey_info *survey)
{
	struct ieee80211_conf *conf = &hw->conf;

	if (idx != 0)
		return -ENOENT;

	survey->channel = conf->chandef.chan;
	survey->filled = 0;
	return 0;
}

static int cc33xx_allocate_sta(struct cc33xx *cc,
			       struct cc33xx_vif *wlvif,
			     struct ieee80211_sta *sta)
{
	struct cc33xx_station *wl_sta;
	int ret;

	if (cc->active_sta_count >= CC33XX_MAX_AP_STATIONS) {
		cc33xx_warning("could not allocate HLID - too much stations");
		return -EBUSY;
	}

	wl_sta = (struct cc33xx_station *)sta->drv_priv;

	ret = cc33xx_set_link(cc, wlvif, wl_sta->hlid);

	if (ret < 0) {
		cc33xx_warning("could not allocate HLID - too many links");
		return -EBUSY;
	}

	/* use the previous security seq, if this is a recovery/resume */
	cc->links[wl_sta->hlid].total_freed_pkts = wl_sta->total_freed_pkts;

	set_bit(wl_sta->hlid, wlvif->ap.sta_hlid_map);
	memcpy(cc->links[wl_sta->hlid].addr, sta->addr, ETH_ALEN);
	cc->active_sta_count++;
	return 0;
}

void cc33xx_free_sta(struct cc33xx *cc, struct cc33xx_vif *wlvif, u8 hlid)
{
	if (!test_bit(hlid, wlvif->ap.sta_hlid_map))
		return;

	clear_bit(hlid, wlvif->ap.sta_hlid_map);
	__clear_bit(hlid, &cc->ap_ps_map);
	__clear_bit(hlid, &cc->ap_fw_ps_map);

	/* save the last used PN in the private part of iee80211_sta,
	 * in case of recovery/suspend
	 */
	cc33xx_save_freed_pkts_addr(cc, wlvif, hlid, cc->links[hlid].addr);

	cc33xx_clear_link(cc, wlvif, &hlid);
	cc->active_sta_count--;

	/* rearm the tx watchdog when the last STA is freed - give the FW a
	 * chance to return STA-buffered packets before complaining.
	 */
	if (cc->active_sta_count == 0)
		cc33xx_rearm_tx_watchdog_locked(cc);
}

static int cc33xx_sta_add(struct cc33xx *cc,
			  struct cc33xx_vif *wlvif,
			  struct ieee80211_sta *sta)
{
	struct cc33xx_station *wl_sta;
	int ret = 0;
	u8 hlid;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 add sta %d", (int)sta->aid);

	wl_sta = (struct cc33xx_station *)sta->drv_priv;
	ret = cc33xx_cmd_add_peer(cc, wlvif, sta, &hlid, 0);
	if (ret < 0)
		return ret;

	wl_sta->hlid = hlid;
	ret = cc33xx_allocate_sta(cc, wlvif, sta);

	return ret;
}

static int cc33xx_sta_remove(struct cc33xx *cc,
			     struct cc33xx_vif *wlvif,
			     struct ieee80211_sta *sta)
{
	struct cc33xx_station *wl_sta;
	int ret = 0, id;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 remove sta %d", (int)sta->aid);

	wl_sta = (struct cc33xx_station *)sta->drv_priv;
	id = wl_sta->hlid;
	if (WARN_ON(!test_bit(id, wlvif->ap.sta_hlid_map)))
		return -EINVAL;

	ret = cc33xx_cmd_remove_peer(cc, wlvif, wl_sta->hlid);
	if (ret < 0)
		return ret;

	cc33xx_free_sta(cc, wlvif, wl_sta->hlid);
	return ret;
}

static void cc33xx_roc_if_possible(struct cc33xx *cc, struct cc33xx_vif *wlvif)
{
	if (find_first_bit(cc->roc_map, CC33XX_MAX_ROLES) < CC33XX_MAX_ROLES)
		return;

	if (WARN_ON(wlvif->role_id == CC33XX_INVALID_ROLE_ID))
		return;

	cc33xx_roc(cc, wlvif, wlvif->role_id, wlvif->band, wlvif->channel);
}

/* when wl_sta is NULL, we treat this call as if coming from a
 * pending auth reply.
 * cc->mutex must be taken and the FW must be awake when the call
 * takes place.
 */
void cc33xx_update_inconn_sta(struct cc33xx *cc, struct cc33xx_vif *wlvif,
			      struct cc33xx_station *wl_sta, bool in_conn)
{
	cc33xx_debug(DEBUG_CMD, "update_inconn_sta: in_conn=%d count=%d, pending_auth=%d",
		     in_conn,
		     wlvif->inconn_count, wlvif->ap_pending_auth_reply);

	if (in_conn) {
		if (WARN_ON(wl_sta && wl_sta->in_connection))
			return;

		if (!wlvif->ap_pending_auth_reply && !wlvif->inconn_count) {
			cc33xx_roc_if_possible(cc, wlvif);
			if (test_bit(wlvif->role_id, cc->roc_map)) {
				unsigned long roc_cmplt_jiffies =
					msecs_to_jiffies(CC33xx_PEND_ROC_COMPLETE_TIMEOUT);

				/* set timer on croc timeout */
				wlvif->pending_auth_reply_time = jiffies;
				cancel_delayed_work(&wlvif->roc_timeout_work);

				cc33xx_debug(DEBUG_AP, "delay queue roc_timeout_work");

				ieee80211_queue_delayed_work(cc->hw,
							     &wlvif->roc_timeout_work,
							     roc_cmplt_jiffies);
			}
		}

		if (wl_sta) {
			wl_sta->in_connection = true;
			wlvif->inconn_count++;
		} else {
			wlvif->ap_pending_auth_reply = true;
		}
	} else {
		if (wl_sta && !wl_sta->in_connection)
			return;

		if (WARN_ON(!wl_sta && !wlvif->ap_pending_auth_reply))
			return;

		if (WARN_ON(wl_sta && !wlvif->inconn_count))
			return;

		if (wl_sta) {
			wl_sta->in_connection = false;
			wlvif->inconn_count--;
		} else {
			wlvif->ap_pending_auth_reply = false;
		}

		if (!wlvif->inconn_count && !wlvif->ap_pending_auth_reply &&
		    test_bit(wlvif->role_id, cc->roc_map)) {
			cc33xx_croc(cc, wlvif->role_id);
			/* remove timer for croc t/o */
			cc33xx_debug(DEBUG_AP, "Cancel pending_roc timeout");
			cancel_delayed_work(&wlvif->roc_timeout_work);
		}
	}
	cc33xx_debug(DEBUG_CMD, "update_inconn_sta done: in_conn=%d count=%d, pending_auth=%d",
		     in_conn, wlvif->inconn_count,
		     wlvif->ap_pending_auth_reply);
}

static int cc33xx_update_sta_state(struct cc33xx *cc,
				   struct cc33xx_vif *wlvif,
				   struct ieee80211_sta *sta,
				   enum ieee80211_sta_state old_state,
				   enum ieee80211_sta_state new_state)
{
	struct cc33xx_station *wl_sta;
	bool is_ap = wlvif->bss_type == BSS_TYPE_AP_BSS;
	bool is_sta = wlvif->bss_type == BSS_TYPE_STA_BSS;
	int ret;

	wl_sta = (struct cc33xx_station *)sta->drv_priv;

	/* Add station (AP mode) */
	if (is_ap && old_state == IEEE80211_STA_NOTEXIST && new_state == IEEE80211_STA_NONE) {
		ret = cc33xx_sta_add(cc, wlvif, sta);
		if (ret)
			return ret;

		cc33xx_update_inconn_sta(cc, wlvif, wl_sta, true);
	}

	/* Remove station (AP mode) */
	if (is_ap && old_state == IEEE80211_STA_NONE && new_state == IEEE80211_STA_NOTEXIST) {
		/* must not fail */
		cc33xx_sta_remove(cc, wlvif, sta);

		cc33xx_update_inconn_sta(cc, wlvif, wl_sta, false);
	}

	/* Authorize station (AP mode) */
	if (is_ap && new_state == IEEE80211_STA_AUTHORIZED) {
		/* reconfigure peer */
		ret = cc33xx_cmd_add_peer(cc, wlvif, sta, NULL, true);
		if (ret < 0)
			return ret;

		cc33xx_update_inconn_sta(cc, wlvif, wl_sta, false);
	}

	/* Authorize station */
	if (is_sta && new_state == IEEE80211_STA_AUTHORIZED) {
		set_bit(WLVIF_FLAG_STA_AUTHORIZED, &wlvif->flags);
		ret = cc33xx_set_authorized(cc, wlvif);
		if (ret)
			return ret;
	}

	if (is_sta && old_state == IEEE80211_STA_AUTHORIZED && new_state == IEEE80211_STA_ASSOC) {
		clear_bit(WLVIF_FLAG_STA_AUTHORIZED, &wlvif->flags);
		clear_bit(WLVIF_FLAG_STA_STATE_SENT, &wlvif->flags);
	}

	/* save seq number on disassoc (suspend) */
	if (is_sta && old_state == IEEE80211_STA_ASSOC && new_state == IEEE80211_STA_AUTH) {
		cc33xx_save_freed_pkts(cc, wlvif, wlvif->sta.hlid, sta);
		wlvif->total_freed_pkts = 0;
	}

	/* restore seq number on assoc (resume) */
	if (is_sta && old_state == IEEE80211_STA_AUTH && new_state == IEEE80211_STA_ASSOC)
		wlvif->total_freed_pkts = wl_sta->total_freed_pkts;

	/* clear ROCs on failure or authorization */
	if (is_sta &&
	    (new_state == IEEE80211_STA_AUTHORIZED ||
	    new_state == IEEE80211_STA_NOTEXIST)) {
		if (test_bit(wlvif->role_id, cc->roc_map))
			cc33xx_croc(cc, wlvif->role_id);
	}

	if (is_sta && (old_state == IEEE80211_STA_NOTEXIST &&
		       new_state == IEEE80211_STA_NONE)) {
		if (find_first_bit(cc->roc_map,
				   CC33XX_MAX_ROLES) >= CC33XX_MAX_ROLES) {
			WARN_ON(wlvif->role_id == CC33XX_INVALID_ROLE_ID);
			cc33xx_roc(cc, wlvif, wlvif->role_id,
				   wlvif->band, wlvif->channel);
		}
	}

	return 0;
}

static int cc33xx_op_sta_state(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       enum ieee80211_sta_state old_state,
			       enum ieee80211_sta_state new_state)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	int ret;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 sta %d state=%d->%d",
		     sta->aid, old_state, new_state);

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON)) {
		ret = -EBUSY;
		goto out;
	}

	ret = cc33xx_update_sta_state(cc, wlvif, sta, old_state, new_state);

out:
	mutex_unlock(&cc->mutex);
	if (new_state < old_state)
		return 0;
	return ret;
}

static int cc33xx_op_ampdu_action(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_ampdu_params *params)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	int ret;
	u8 hlid, *ba_bitmap;
	struct ieee80211_sta *sta = params->sta;
	enum ieee80211_ampdu_mlme_action action = params->action;
	u16 tid = params->tid;
	u16 *ssn = &params->ssn;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 ampdu action %d tid %d",
		     action, tid);

	/* sanity check - the fields in FW are only 8bits wide */
	if (WARN_ON(tid > 0xFF))
		return -EOPNOTSUPP;

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON)) {
		ret = -EAGAIN;
		goto out;
	}

	if (wlvif->bss_type == BSS_TYPE_STA_BSS) {
		hlid = wlvif->sta.hlid;
	} else if (wlvif->bss_type == BSS_TYPE_AP_BSS) {
		struct cc33xx_station *wl_sta;

		wl_sta = (struct cc33xx_station *)sta->drv_priv;
		hlid = wl_sta->hlid;
	} else {
		ret = -EINVAL;
		goto out;
	}

	if (hlid == CC33XX_INVALID_LINK_ID) {
		ret = 0;
		goto out;
	}

	if (WARN_ON(hlid >= CC33XX_MAX_LINKS)) {
		ret = -EINVAL;
		goto out;
	}

	ba_bitmap = &cc->links[hlid].ba_bitmap;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 ampdu: Rx tid %d action %d",
		     tid, action);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		if (!wlvif->ba_support || !wlvif->ba_allowed) {
			ret = -EOPNOTSUPP;
			break;
		}

		if (cc->ba_rx_session_count >= CC33XX_RX_BA_MAX_SESSIONS) {
			ret = -EBUSY;
			cc33xx_error("exceeded max RX BA sessions");
			break;
		}

		if (*ba_bitmap & BIT(tid)) {
			ret = -EINVAL;
			cc33xx_error("cannot enable RX BA session on active tid: %d",
				     tid);
			break;
		}

		ret = cc33xx_acx_set_ba_receiver_session(cc, tid, *ssn,
							 true, hlid,
							 params->buf_size);

		if (!ret) {
			*ba_bitmap |= BIT(tid);
			cc->ba_rx_session_count++;
		}
		break;

	case IEEE80211_AMPDU_RX_STOP:
		if (!(*ba_bitmap & BIT(tid))) {
			/* this happens on reconfig - so only output a debug
			 * message for now, and don't fail the function.
			 */
			cc33xx_debug(DEBUG_MAC80211,
				     "no active RX BA session on tid: %d", tid);
			ret = 0;
			break;
		}

		ret = cc33xx_acx_set_ba_receiver_session(cc, tid, 0,
							 false, hlid, 0);
		if (!ret) {
			*ba_bitmap &= ~BIT(tid);
			cc->ba_rx_session_count--;
		}
		break;

	/* The BA initiator session management in FW independently.
	 * Falling break here on purpose for all TX APDU commands.
	 */
	case IEEE80211_AMPDU_TX_START:
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		ret = -EINVAL;
		break;

	default:
		cc33xx_error("Incorrect ampdu action id=%x\n", action);
		ret = -EINVAL;
	}

out:
	mutex_unlock(&cc->mutex);

	return ret;
}

static int cc33xx_set_bitrate_mask(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   const struct cfg80211_bitrate_mask *mask)
{
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	struct cc33xx *cc = hw->priv;
	int ret = 0;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 set_bitrate_mask 0x%x 0x%x",
		     mask->control[NL80211_BAND_2GHZ].legacy,
		mask->control[NL80211_BAND_5GHZ].legacy);

	mutex_lock(&cc->mutex);

	wlvif->bitrate_masks[0] = cc33xx_tx_enabled_rates_get(cc,
							      mask->control[0].legacy, 0);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	if (wlvif->bss_type == BSS_TYPE_STA_BSS &&
	    !test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags)) {
		cc33xx_set_band_rate(cc, wlvif);
		wlvif->basic_rate = cc33xx_tx_min_rate_get(cc,
							   wlvif->basic_rate_set);
	}
out:
	mutex_unlock(&cc->mutex);

	return ret;
}

static void cc33xx_op_channel_switch(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_channel_switch *ch_switch)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	int ret;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 channel switch");

	cc33xx_tx_flush(cc);

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state == CC33XX_STATE_OFF)) {
		if (test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags))
			ieee80211_chswitch_done(vif, false);
		goto out;
	} else if (unlikely(cc->state != CC33XX_STATE_ON)) {
		goto out;
	}

	/* TODO: change mac80211 to pass vif as param */

	if (test_bit(WLVIF_FLAG_STA_ASSOCIATED, &wlvif->flags)) {
		unsigned long delay_usec;

		ret = cmd_channel_switch(cc, wlvif, ch_switch);
		if (ret)
			goto out;

		set_bit(WLVIF_FLAG_CS_PROGRESS, &wlvif->flags);

		/* indicate failure 5 seconds after channel switch time */
		delay_usec = ieee80211_tu_to_usec(wlvif->beacon_int) *
							       ch_switch->count;
		ieee80211_queue_delayed_work(hw, &wlvif->channel_switch_work,
					     usecs_to_jiffies(delay_usec) +
					     msecs_to_jiffies(5000));
	}

out:
	mutex_unlock(&cc->mutex);
}

static inline void cc33xx_op_channel_switch_beacon(struct ieee80211_hw *hw,
						   struct ieee80211_vif *vif,
						   struct cfg80211_chan_def *chandef)
{
	cc33xx_error("AP channel switch is not supported");
}

static inline void cc33xx_op_flush(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   u32 queues, bool drop)
{
	cc33xx_tx_flush(hw->priv);
}

static int cc33xx_op_remain_on_channel(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_channel *chan,
				       int duration,
				       enum ieee80211_roc_type type)
{
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	struct cc33xx *cc = hw->priv;
	int channel, active_roc, ret = 0;

	channel = ieee80211_frequency_to_channel(chan->center_freq);

	cc33xx_debug(DEBUG_MAC80211,
		     "mac80211 roc %d (role %d)", channel, wlvif->role_id);

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	/* return EBUSY if we can't ROC right now */
	active_roc = find_first_bit(cc->roc_map, CC33XX_MAX_ROLES);
	if (cc->roc_vif || active_roc < CC33XX_MAX_ROLES) {
		cc33xx_warning("active roc on role %d", active_roc);
		ret = -EBUSY;
		goto out;
	}

	cc33xx_debug(DEBUG_MAC80211,
		     "call cc33xx_start_dev, band = %d, channel = %d",
		     chan->band, channel);
	ret = cc33xx_start_dev(cc, wlvif, chan->band, channel);
	if (ret < 0)
		goto out;

	cc->roc_vif = vif;
	ieee80211_queue_delayed_work(hw, &cc->roc_complete_work,
				     msecs_to_jiffies(duration));

out:
	mutex_unlock(&cc->mutex);
	return ret;
}

static int __cc33xx_roc_completed(struct cc33xx *cc)
{
	struct cc33xx_vif *wlvif;
	int ret;

	/* already completed */
	if (unlikely(!cc->roc_vif))
		return 0;

	wlvif = cc33xx_vif_to_data(cc->roc_vif);

	if (!test_bit(WLVIF_FLAG_INITIALIZED, &wlvif->flags))
		return -EBUSY;

	ret = cc33xx_stop_dev(cc, wlvif);
	if (ret < 0)
		return ret;

	cc->roc_vif = NULL;

	return 0;
}

static int cc33xx_roc_completed(struct cc33xx *cc)
{
	int ret;

	cc33xx_debug(DEBUG_MAC80211, "roc complete");

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON)) {
		ret = -EBUSY;
		goto out;
	}

	ret = __cc33xx_roc_completed(cc);

out:
	mutex_unlock(&cc->mutex);

	return ret;
}

static void cc33xx_roc_complete_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct cc33xx *cc;
	int ret;

	dwork = to_delayed_work(work);
	cc = container_of(dwork, struct cc33xx, roc_complete_work);

	ret = cc33xx_roc_completed(cc);
	if (!ret)
		ieee80211_remain_on_channel_expired(cc->hw);
}

static int cc33xx_op_cancel_remain_on_channel(struct ieee80211_hw *hw,
					      struct ieee80211_vif *vif)
{
	struct cc33xx *cc = hw->priv;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 croc");

	cc33xx_tx_flush(cc);

	/* we can't just flush_work here, because it might deadlock
	 * (as we might get called from the same workqueue)
	 */
	cancel_delayed_work_sync(&cc->roc_complete_work);
	cc33xx_roc_completed(cc);

	return 0;
}

static void cc33xx_op_sta_rc_update(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta,
				    u32 changed)
{
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);

	cc33xx_debug(DEBUG_MAC80211, "mac80211 sta_rc_update");

	if (!(changed & IEEE80211_RC_BW_CHANGED))
		return;

	/* this callback is atomic, so schedule a new work */
	wlvif->rc_update_bw = sta->deflink.bandwidth;
	memcpy(&wlvif->rc_ht_cap, &sta->deflink.ht_cap, sizeof(sta->deflink.ht_cap));
	ieee80211_queue_work(hw, &wlvif->rc_update_work);
}

static void cc33xx_op_sta_statistics(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta,
				     struct station_info *sinfo)
{
	struct cc33xx *cc = hw->priv;
	struct cc33xx_vif *wlvif = cc33xx_vif_to_data(vif);
	s8 rssi_dbm;
	int ret;

	cc33xx_debug(DEBUG_MAC80211, "mac80211 get_rssi");

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	ret = cc33xx_acx_average_rssi(cc, wlvif, &rssi_dbm);
	if (ret < 0)
		goto out;

	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL);
	sinfo->signal = rssi_dbm;

	ret = cc33xx_acx_get_tx_rate(cc, wlvif, sinfo);
	if (ret < 0)
		goto out;

out:
	mutex_unlock(&cc->mutex);
}

static u32 cc33xx_op_get_expected_throughput(struct ieee80211_hw *hw,
					     struct ieee80211_sta *sta)
{
	struct cc33xx_station *wl_sta = (struct cc33xx_station *)sta->drv_priv;
	struct cc33xx *cc = hw->priv;
	u8 hlid = wl_sta->hlid;

	/* return in units of Kbps */
	return (cc->links[hlid].fw_rate_mbps * 1000);
}

static bool cc33xx_tx_frames_pending(struct ieee80211_hw *hw)
{
	struct cc33xx *cc = hw->priv;
	bool ret = false;

	mutex_lock(&cc->mutex);

	if (unlikely(cc->state != CC33XX_STATE_ON))
		goto out;

	/* packets are considered pending if in the TX queue or the FW */
	ret = (cc33xx_tx_total_queue_count(cc) > 0) || (cc->tx_frames_cnt > 0);
out:
	mutex_unlock(&cc->mutex);

	return ret;
}

#ifdef CONFIG_PM
static const struct ieee80211_ops cc33xx_ops = {
	.start = cc33xx_op_start,
	.stop = cc33xx_op_stop,
	.add_interface = cc33xx_op_add_interface,
	.remove_interface = cc33xx_op_remove_interface,
	.change_interface = cc33xx_op_change_interface,
	.suspend = cc33xx_op_suspend,
	.resume = cc33xx_op_resume,
	.config = cc33xx_op_config,
	.prepare_multicast = cc33xx_op_prepare_multicast,
	.configure_filter = cc33xx_op_configure_filter,
	.tx = cc33xx_op_tx,
	.wake_tx_queue = ieee80211_handle_wake_tx_queue,
	.set_key = cc33xx_op_set_key,
	.hw_scan = cc33xx_op_hw_scan,
	.cancel_hw_scan = cc33xx_op_cancel_hw_scan,
	.sched_scan_start = cc33xx_op_sched_scan_start,
	.sched_scan_stop = cc33xx_op_sched_scan_stop,
	.bss_info_changed = cc33xx_op_bss_info_changed,
	.set_frag_threshold = cc33xx_op_set_frag_threshold,
	.set_rts_threshold = cc33xx_op_set_rts_threshold,
	.conf_tx = cc33xx_op_conf_tx,
	.get_tsf = cc33xx_op_get_tsf,
	.get_survey = cc33xx_op_get_survey,
	.sta_state = cc33xx_op_sta_state,
	.ampdu_action = cc33xx_op_ampdu_action,
	.tx_frames_pending = cc33xx_tx_frames_pending,
	.set_bitrate_mask = cc33xx_set_bitrate_mask,
	.set_default_unicast_key = cc33xx_op_set_default_key_idx,
	.channel_switch = cc33xx_op_channel_switch,
	.channel_switch_beacon = cc33xx_op_channel_switch_beacon,
	.flush = cc33xx_op_flush,
	.remain_on_channel = cc33xx_op_remain_on_channel,
	.cancel_remain_on_channel = cc33xx_op_cancel_remain_on_channel,
	.add_chanctx = cc33xx_op_add_chanctx,
	.remove_chanctx = cc33xx_op_remove_chanctx,
	.change_chanctx = cc33xx_op_change_chanctx,
	.assign_vif_chanctx = cc33xx_op_assign_vif_chanctx,
	.unassign_vif_chanctx = cc33xx_op_unassign_vif_chanctx,
	.switch_vif_chanctx = cc33xx_op_switch_vif_chanctx,
	.sta_rc_update = cc33xx_op_sta_rc_update,
	.sta_statistics = cc33xx_op_sta_statistics,
	.get_expected_throughput = cc33xx_op_get_expected_throughput,
	CFG80211_TESTMODE_CMD(cc33xx_tm_cmd)
};

static const struct wiphy_wowlan_support cc33xx_wowlan_support = {
	.flags = WIPHY_WOWLAN_ANY,
	.n_patterns = CC33XX_MAX_RX_FILTERS,
	.pattern_min_len = 1,
	.pattern_max_len = CC33XX_RX_FILTER_MAX_PATTERN_SIZE,
};

static void setup_wake_irq(struct cc33xx *cc)
{
	struct platform_device *pdev = cc->pdev;
	struct cc33xx_platdev_data *pdev_data = dev_get_platdata(&pdev->dev);

	struct resource *res;
	int ret;

	device_init_wakeup(cc->dev, true);

	if (pdev_data->pwr_in_suspend)
		cc->hw->wiphy->wowlan = &cc33xx_wowlan_support;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res) {
		cc->wakeirq = res->start;
		ret = dev_pm_set_dedicated_wake_irq(cc->dev, cc->wakeirq);
		if (ret)
			cc->wakeirq = -ENODEV;
	} else {
		cc->wakeirq = -ENODEV;
	}

	cc->keep_device_power = true;
}
#else
static const struct ieee80211_ops cc33xx_ops = {
	.start = cc33xx_op_start,
	.stop = cc33xx_op_stop,
	.add_interface = cc33xx_op_add_interface,
	.remove_interface = cc33xx_op_remove_interface,
	.change_interface = cc33xx_op_change_interface,
	.config = cc33xx_op_config,
	.prepare_multicast = cc33xx_op_prepare_multicast,
	.configure_filter = cc33xx_op_configure_filter,
	.tx = cc33xx_op_tx,
	.wake_tx_queue = ieee80211_handle_wake_tx_queue,
	.set_key = cc33xx_op_set_key,
	.hw_scan = cc33xx_op_hw_scan,
	.cancel_hw_scan = cc33xx_op_cancel_hw_scan,
	.sched_scan_start = cc33xx_op_sched_scan_start,
	.sched_scan_stop = cc33xx_op_sched_scan_stop,
	.bss_info_changed = cc33xx_op_bss_info_changed,
	.set_frag_threshold = cc33xx_op_set_frag_threshold,
	.set_rts_threshold = cc33xx_op_set_rts_threshold,
	.conf_tx = cc33xx_op_conf_tx,
	.get_tsf = cc33xx_op_get_tsf,
	.get_survey = cc33xx_op_get_survey,
	.sta_state = cc33xx_op_sta_state,
	.ampdu_action = cc33xx_op_ampdu_action,
	.tx_frames_pending = cc33xx_tx_frames_pending,
	.set_bitrate_mask = cc33xx_set_bitrate_mask,
	.set_default_unicast_key = cc33xx_op_set_default_key_idx,
	.channel_switch = cc33xx_op_channel_switch,
	.channel_switch_beacon = cc33xx_op_channel_switch_beacon,
	.flush = cc33xx_op_flush,
	.remain_on_channel = cc33xx_op_remain_on_channel,
	.cancel_remain_on_channel = cc33xx_op_cancel_remain_on_channel,
	.add_chanctx = cc33xx_op_add_chanctx,
	.remove_chanctx = cc33xx_op_remove_chanctx,
	.change_chanctx = cc33xx_op_change_chanctx,
	.assign_vif_chanctx = cc33xx_op_assign_vif_chanctx,
	.unassign_vif_chanctx = cc33xx_op_unassign_vif_chanctx,
	.switch_vif_chanctx = cc33xx_op_switch_vif_chanctx,
	.sta_rc_update = cc33xx_op_sta_rc_update,
	.sta_statistics = cc33xx_op_sta_statistics,
	.get_expected_throughput = cc33xx_op_get_expected_throughput,
	CFG80211_TESTMODE_CMD(cc33xx_tm_cmd)
};

static inline void setup_wake_irq(struct cc33xx *cc)
{
	cc->keep_device_power = true;
}
#endif /* CONFIG_PM */

u8 cc33xx_rate_to_idx(struct cc33xx *cc, u8 rate, enum nl80211_band band)
{
	u8 idx;

	if (WARN_ON(band >= 2))
		return 0;

	if (unlikely(rate > CONF_HW_RATE_INDEX_MAX)) {
		cc33xx_error("Illegal RX rate from HW: %d", rate);
		return 0;
	}

	idx = cc33xx_band_rate_to_idx[band][rate];
	if (unlikely(idx == CONF_HW_RXTX_RATE_UNSUPPORTED)) {
		cc33xx_error("Unsupported RX rate from HW: %d", rate);
		return 0;
	}

	return idx;
}

static void cc33xx_derive_mac_addresses(struct cc33xx *cc)
{
	const u8 zero_mac[ETH_ALEN] = {0};
	u8 base_addr[ETH_ALEN];
	u8 bd_addr[ETH_ALEN];
	bool use_nvs = false;
	bool use_efuse = false;
	bool use_random = false;

	if (cc->nvs_mac_addr_len != ETH_ALEN) {
		if (unlikely(cc->nvs_mac_addr_len > 0))
			cc33xx_warning("NVS MAC address present but has a wrong size, ignoring.");

		if (!ether_addr_equal(zero_mac, cc->efuse_mac_address)) {
			use_efuse = true;
			ether_addr_copy(base_addr, cc->efuse_mac_address);
			cc33xx_debug(DEBUG_BOOT,
				     "MAC address derived from EFUSE");
		} else {
			use_random = true;
			eth_random_addr(base_addr);
			cc33xx_warning("No EFUSE / NVS data, using random locally administered address.");
		}
	} else {
		u8 *nvs_addr = cc->nvs_mac_addr;
		const u8 efuse_magic_addr[ETH_ALEN] = {
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
		const u8 random_magic_addr[ETH_ALEN] = {
					0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

		/* In NVS, addresses 00-00-00-00-00-00 and 00-00-00-00-00-01
		 * have special meaning:
		 */

		if (ether_addr_equal(nvs_addr, efuse_magic_addr)) {
			use_efuse = true;
			ether_addr_copy(base_addr, cc->efuse_mac_address);
			cc33xx_debug(DEBUG_BOOT,
				     "NVS file selects address from EFUSE");
		} else if (ether_addr_equal(nvs_addr, random_magic_addr)) {
			use_random = true;
			eth_random_addr(base_addr);
			cc33xx_debug(DEBUG_BOOT,
				     "NVS file sets random MAC address");
		} else {
			use_nvs = true;
			ether_addr_copy(base_addr, nvs_addr);
			cc33xx_debug(DEBUG_BOOT,
				     "NVS file sets explicit MAC address");
		}
	}

	if (use_nvs || use_efuse) {
		u8 oui_laa_bit = BIT(1);
		u8 oui_multicast_bit = BIT(0);

		base_addr[0] &= ~oui_multicast_bit;

		ether_addr_copy(cc->addresses[0].addr, base_addr);
		ether_addr_copy(cc->addresses[1].addr, base_addr);
		ether_addr_copy(cc->addresses[2].addr, base_addr);
		ether_addr_copy(bd_addr, base_addr);

		cc->addresses[1].addr[0] |= oui_laa_bit;
		cc->addresses[2].addr[0] |= oui_laa_bit;

		eth_addr_inc(cc->addresses[2].addr);
		eth_addr_inc(bd_addr);
	} else if (use_random) {
		ether_addr_copy(cc->addresses[0].addr, base_addr);
		ether_addr_copy(cc->addresses[1].addr, base_addr);
		ether_addr_copy(cc->addresses[2].addr, base_addr);
		ether_addr_copy(bd_addr, base_addr);

		eth_addr_inc(bd_addr);
		eth_addr_inc(cc->addresses[1].addr);
		eth_addr_inc(cc->addresses[1].addr);
		eth_addr_inc(cc->addresses[2].addr);
		eth_addr_inc(cc->addresses[2].addr);
		eth_addr_inc(cc->addresses[2].addr);
	} else {
		WARN_ON(1);
	}

	cc33xx_debug(DEBUG_BOOT, "Base MAC address: %pM",
		     cc->addresses[0].addr);

	cc->hw->wiphy->n_addresses = CC33XX_NUM_MAC_ADDRESSES;
	cc->hw->wiphy->addresses = cc->addresses;

	cmd_set_bd_addr(cc, bd_addr);
}

static int cc33xx_register_hw(struct cc33xx *cc)
{
	int ret;

	if (cc->mac80211_registered)
		return 0;

	cc33xx_derive_mac_addresses(cc);

	ret = ieee80211_register_hw(cc->hw);
	if (ret < 0) {
		cc33xx_error("unable to register mac80211 hw: %d", ret);
		goto out;
	}

	cc->mac80211_registered = true;

out:
	return ret;
}

static void cc33xx_unregister_hw(struct cc33xx *cc)
{
	if (cc->plt)
		cc33xx_plt_stop(cc);

	ieee80211_unregister_hw(cc->hw);
	cc->mac80211_registered = false;
}

static int cc33xx_init_ieee80211(struct cc33xx *cc)
{
	unsigned int i;

	if (cc->conf.core.mixed_mode_support) {
		static const u32 cipher_suites[] = {
			WLAN_CIPHER_SUITE_CCMP,
			WLAN_CIPHER_SUITE_AES_CMAC,
			WLAN_CIPHER_SUITE_TKIP,
			WLAN_CIPHER_SUITE_GCMP,
			WLAN_CIPHER_SUITE_GCMP_256,
			WLAN_CIPHER_SUITE_BIP_GMAC_128,
			WLAN_CIPHER_SUITE_BIP_GMAC_256,
		};
		cc->hw->wiphy->cipher_suites = cipher_suites;
		cc->hw->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);

	} else {
		static const u32 cipher_suites[] = {
			WLAN_CIPHER_SUITE_CCMP,
			WLAN_CIPHER_SUITE_AES_CMAC,
			WLAN_CIPHER_SUITE_GCMP,
			WLAN_CIPHER_SUITE_GCMP_256,
			WLAN_CIPHER_SUITE_BIP_GMAC_128,
			WLAN_CIPHER_SUITE_BIP_GMAC_256,
		};
		cc->hw->wiphy->cipher_suites = cipher_suites;
		cc->hw->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);
	}

	/* The tx descriptor buffer */
	cc->hw->extra_tx_headroom = CC33XX_TX_EXTRA_HEADROOM;

	if (cc->quirks & CC33XX_QUIRK_TKIP_HEADER_SPACE)
		cc->hw->extra_tx_headroom += CC33XX_EXTRA_SPACE_TKIP;

	/* unit us */
	/* FIXME: find a proper value */
	cc->hw->max_listen_interval =
				cc->conf.host_conf.conn.max_listen_interval;

	ieee80211_hw_set(cc->hw, SUPPORT_FAST_XMIT);
	ieee80211_hw_set(cc->hw, CHANCTX_STA_CSA);
	ieee80211_hw_set(cc->hw, QUEUE_CONTROL);
	ieee80211_hw_set(cc->hw, TX_AMPDU_SETUP_IN_HW);
	ieee80211_hw_set(cc->hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(cc->hw, AP_LINK_PS);
	ieee80211_hw_set(cc->hw, SPECTRUM_MGMT);
	ieee80211_hw_set(cc->hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(cc->hw, CONNECTION_MONITOR);
	ieee80211_hw_set(cc->hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(cc->hw, SUPPORTS_DYNAMIC_PS);
	ieee80211_hw_set(cc->hw, SIGNAL_DBM);
	ieee80211_hw_set(cc->hw, SUPPORTS_PS);
	ieee80211_hw_set(cc->hw, SUPPORTS_TX_FRAG);
	ieee80211_hw_set(cc->hw, SUPPORTS_MULTI_BSSID);
	ieee80211_hw_set(cc->hw, SUPPORTS_AMSDU_IN_AMPDU);

	cc->hw->wiphy->interface_modes = cc33xx_wiphy_interface_modes();

	cc->hw->wiphy->max_scan_ssids = 1;
	cc->hw->wiphy->max_sched_scan_ssids = 16;
	cc->hw->wiphy->max_match_sets = 16;
	/* Maximum length of elements in scanning probe request templates
	 * should be the maximum length possible for a template, without
	 * the IEEE80211 header of the template
	 */
	cc->hw->wiphy->max_scan_ie_len = CC33XX_CMD_TEMPL_MAX_SIZE -
			sizeof(struct ieee80211_header);

	cc->hw->wiphy->max_sched_scan_reqs = 1;
	cc->hw->wiphy->max_sched_scan_ie_len = CC33XX_CMD_TEMPL_MAX_SIZE -
		sizeof(struct ieee80211_header);

	cc->hw->wiphy->max_remain_on_channel_duration = 30000;

	cc->hw->wiphy->features |= NL80211_FEATURE_AP_SCAN;

	/* clear channel flags from the previous usage
	 * and restore max_power & max_antenna_gain values.
	 */
	for (i = 0; i < ARRAY_SIZE(cc33xx_channels); i++) {
		cc33xx_band_2ghz.channels[i].flags = 0;
		cc33xx_band_2ghz.channels[i].max_power = CC33XX_MAX_TXPWR;
		cc33xx_band_2ghz.channels[i].max_antenna_gain = 0;
	}

	for (i = 0; i < ARRAY_SIZE(cc33xx_channels_5ghz); i++) {
		cc33xx_band_5ghz.channels[i].flags = 0;
		cc33xx_band_5ghz.channels[i].max_power = CC33XX_MAX_TXPWR;
		cc33xx_band_5ghz.channels[i].max_antenna_gain = 0;
	}

	/* Enable/Disable He based on conf file params */
	if (!cc->conf.mac.he_enable) {
		cc33xx_band_2ghz.iftype_data = NULL;
		cc33xx_band_2ghz.n_iftype_data = 0;

		cc33xx_band_5ghz.iftype_data = NULL;
		cc33xx_band_5ghz.n_iftype_data = 0;
	}

	/* We keep local copies of the band structs because we need to
	 * modify them on a per-device basis.
	 */
	memcpy(&cc->bands[NL80211_BAND_2GHZ], &cc33xx_band_2ghz,
	       sizeof(cc33xx_band_2ghz));
	memcpy(&cc->bands[NL80211_BAND_2GHZ].ht_cap,
	       &cc->ht_cap[NL80211_BAND_2GHZ],
	       sizeof(*cc->ht_cap));

	memcpy(&cc->bands[NL80211_BAND_5GHZ], &cc33xx_band_5ghz,
	       sizeof(cc33xx_band_5ghz));
	memcpy(&cc->bands[NL80211_BAND_5GHZ].ht_cap,
	       &cc->ht_cap[NL80211_BAND_5GHZ],
	       sizeof(*cc->ht_cap));

	cc->hw->wiphy->bands[NL80211_BAND_2GHZ] =
		&cc->bands[NL80211_BAND_2GHZ];

	if (!cc->disable_5g && cc->conf.core.enable_5ghz)
		cc->hw->wiphy->bands[NL80211_BAND_5GHZ] =
			&cc->bands[NL80211_BAND_5GHZ];

	/* allow 4 queues per mac address we support +
	 * 1 cab queue per mac + one global offchannel Tx queue
	 */
	cc->hw->queues = (NUM_TX_QUEUES + 1) * CC33XX_NUM_MAC_ADDRESSES + 1;

	/* the last queue is the offchannel queue */
	cc->hw->offchannel_tx_hw_queue = cc->hw->queues - 1;
	cc->hw->max_rates = 1;

	/* allowed interface combinations */
	cc->hw->wiphy->iface_combinations = cc33xx_iface_combinations;
	cc->hw->wiphy->n_iface_combinations = ARRAY_SIZE(cc33xx_iface_combinations);

	SET_IEEE80211_DEV(cc->hw, cc->dev);

	cc->hw->sta_data_size = sizeof(struct cc33xx_station);
	cc->hw->vif_data_size = sizeof(struct cc33xx_vif);

	cc->hw->max_rx_aggregation_subframes = cc->conf.host_conf.ht.rx_ba_win_size;

	/* For all ps schemes don't use UAPSD, except for UAPSD scheme
	 * As these are the currently supportedd PS schemes, use the default
	 * legacy otherwise
	 */
	if (cc->conf.mac.ps_scheme == PS_SCHEME_UPSD_TRIGGER) {
		cc->hw->uapsd_queues = IEEE80211_WMM_IE_STA_QOSINFO_AC_MASK;
	} else if ((cc->conf.mac.ps_scheme != PS_SCHEME_LEGACY) &&
		   (cc->conf.mac.ps_scheme != PS_SCHEME_NOPSPOLL)) {
		cc->hw->uapsd_queues = 0;
		cc->conf.mac.ps_scheme = PS_SCHEME_LEGACY;
	} else {
		cc->hw->uapsd_queues = 0;
	}

	return 0;
}

#define create_high_prio_freezable_workqueue(name)			\
	alloc_workqueue("%s", __WQ_LEGACY | WQ_FREEZABLE | WQ_UNBOUND |	\
			WQ_MEM_RECLAIM | WQ_HIGHPRI, 1, (name))

static struct ieee80211_hw *cc33xx_alloc_hw(u32 aggr_buf_size)
{
	struct ieee80211_hw *hw;
	struct cc33xx *cc;
	int i, j;
	unsigned int order;

	hw = ieee80211_alloc_hw(sizeof(*cc), &cc33xx_ops);
	if (!hw) {
		cc33xx_error("could not alloc ieee80211_hw");
		goto err_hw_alloc;
	}

	cc = hw->priv;
	memset(cc, 0, sizeof(*cc));

	INIT_LIST_HEAD(&cc->wlvif_list);

	cc->hw = hw;

	/* cc->num_links is not configured yet, so just use CC33XX_MAX_LINKS.
	 * we don't allocate any additional resource here, so that's fine.
	 */
	for (i = 0; i < NUM_TX_QUEUES; i++)
		for (j = 0; j < CC33XX_MAX_LINKS; j++)
			skb_queue_head_init(&cc->links[j].tx_queue[i]);

	skb_queue_head_init(&cc->deferred_rx_queue);
	skb_queue_head_init(&cc->deferred_tx_queue);

	init_llist_head(&cc->event_list);

	INIT_WORK(&cc->netstack_work, cc33xx_netstack_work);
	INIT_WORK(&cc->tx_work, cc33xx_tx_work);
	INIT_WORK(&cc->recovery_work, cc33xx_recovery_work);
	INIT_WORK(&cc->irq_deferred_work, irq_deferred_work);
	INIT_DELAYED_WORK(&cc->scan_complete_work, cc33xx_scan_complete_work);
	INIT_DELAYED_WORK(&cc->roc_complete_work, cc33xx_roc_complete_work);
	INIT_DELAYED_WORK(&cc->tx_watchdog_work, cc33xx_tx_watchdog_work);

	cc->freezable_netstack_wq = create_freezable_workqueue("cc33xx_netstack_wq");
	if (!cc->freezable_netstack_wq)
		goto err_hw_alloc;

	cc->freezable_wq = create_high_prio_freezable_workqueue("cc33xx_wq");
	if (!cc->freezable_wq)
		goto err_ns_wq;

	cc->rx_counter = 0;
	cc->power_level = CC33XX_MAX_TXPWR;
	cc->band = NL80211_BAND_2GHZ;
	cc->flags = 0;
	cc->sleep_auth = CC33XX_PSM_ILLEGAL;

	cc->ap_ps_map = 0;
	cc->ap_fw_ps_map = 0;
	cc->quirks = 0;
	cc->active_sta_count = 0;
	cc->active_link_count = 0;
	cc->fwlog_size = 0;

	/* The system link is always allocated */
	__set_bit(CC33XX_SYSTEM_HLID, cc->links_map);

	memset(cc->tx_frames_map, 0, sizeof(cc->tx_frames_map));
	for (i = 0; i < CC33XX_NUM_TX_DESCRIPTORS; i++)
		cc->tx_frames[i] = NULL;

	spin_lock_init(&cc->cc_lock);

	cc->state = CC33XX_STATE_OFF;
	mutex_init(&cc->mutex);
	mutex_init(&cc->flush_mutex);
	init_completion(&cc->nvs_loading_complete);

	order = get_order(aggr_buf_size);
	cc->aggr_buf = (u8 *)__get_free_pages(GFP_KERNEL, order);
	if (!cc->aggr_buf)
		goto err_all_wq;

	cc->aggr_buf_size = aggr_buf_size;

	cc->dummy_packet = cc33xx_alloc_dummy_packet(cc);
	if (!cc->dummy_packet)
		goto err_aggr;

	/* Allocate one page for the FW log */
	cc->fwlog = (u8 *)get_zeroed_page(GFP_KERNEL);
	if (!cc->fwlog)
		goto err_dummy_packet;

	cc->buffer_32 = kmalloc(sizeof(*cc->buffer_32), GFP_KERNEL);
	if (!cc->buffer_32)
		goto err_fwlog;

	cc->core_status = kzalloc(sizeof(*cc->core_status), GFP_KERNEL);
	if (!cc->core_status)
		goto err_buf32;

	return hw;

err_buf32:
	kfree(cc->buffer_32);

err_fwlog:
	free_page((unsigned long)cc->fwlog);

err_dummy_packet:
	dev_kfree_skb(cc->dummy_packet);

err_aggr:
	free_pages((unsigned long)cc->aggr_buf, order);

err_all_wq:
	destroy_workqueue(cc->freezable_wq);

err_ns_wq:
	destroy_workqueue(cc->freezable_netstack_wq);

err_hw_alloc:
	return NULL;
}

static int cc33xx_free_hw(struct cc33xx *cc)
{
	/* Unblock any fwlog readers */
	mutex_lock(&cc->mutex);
	cc->fwlog_size = -1;
	mutex_unlock(&cc->mutex);

	kfree(cc->buffer_32);
	kfree(cc->core_status);
	free_page((unsigned long)cc->fwlog);
	dev_kfree_skb(cc->dummy_packet);
	free_pages((unsigned long)cc->aggr_buf, get_order(cc->aggr_buf_size));

	kfree(cc->nvs_mac_addr);
	cc->nvs_mac_addr = NULL;

	destroy_workqueue(cc->freezable_wq);
	destroy_workqueue(cc->freezable_netstack_wq);
	flush_deferred_event_list(cc);

	ieee80211_free_hw(cc->hw);

	return 0;
}

static int cc33xx_identify_chip(struct cc33xx *cc)
{
	int ret = 0;

	cc->quirks |= CC33XX_QUIRK_RX_BLOCKSIZE_ALIGN |
		      CC33XX_QUIRK_TX_BLOCKSIZE_ALIGN |
		      CC33XX_QUIRK_NO_SCHED_SCAN_WHILE_CONN |
		      CC33XX_QUIRK_TX_PAD_LAST_FRAME |
		      CC33XX_QUIRK_DUAL_PROBE_TMPL;

	if (cc->if_ops->get_max_transaction_len)
		cc->max_transaction_len =
			cc->if_ops->get_max_transaction_len(cc->dev);
	else
		cc->max_transaction_len = 0;

	return ret;
}

static int read_version_info(struct cc33xx *cc)
{
	int ret;

	ret = cc33xx_acx_init_get_fw_versions(cc);
	if (ret < 0) {
		cc33xx_error("Get FW version FAILED!");
		return ret;
	}

	cc33xx_debug(DEBUG_BOOT, "Wireless firmware version %u.%u.%u.%u",
		     cc->fw_ver->major_version,
		     cc->fw_ver->minor_version,
		     cc->fw_ver->api_version,
		     cc->fw_ver->build_version);

	cc33xx_debug(DEBUG_BOOT, "Wireless PHY version %u.%u.%u.%u.%u.%u",
		     cc->fw_ver->phy_version[5],
		     cc->fw_ver->phy_version[4],
		     cc->fw_ver->phy_version[3],
		     cc->fw_ver->phy_version[2],
		     cc->fw_ver->phy_version[1],
		     cc->fw_ver->phy_version[0]);

	return 0;
}

static void cc33xx_nvs_cb(const struct firmware *fw, void *context)
{
	struct cc33xx *cc = context;
	struct platform_device *pdev = cc->pdev;
	struct cc33xx_platdev_data *pdev_data = dev_get_platdata(&pdev->dev);

	int ret;

	if (fw) {
		cc->nvs_mac_addr = kmemdup(fw->data, fw->size, GFP_KERNEL);
		if (!cc->nvs_mac_addr) {
			cc33xx_error("Could not allocate nvs data");
			goto out;
		}
		cc->nvs_mac_addr_len = fw->size;
	} else if (pdev_data->family->nvs_name) {
		cc33xx_debug(DEBUG_BOOT, "Could not get nvs file %s",
			     pdev_data->family->nvs_name);
		cc->nvs_mac_addr = NULL;
		cc->nvs_mac_addr_len = 0;
	} else {
		cc->nvs_mac_addr = NULL;
		cc->nvs_mac_addr_len = 0;
	}

	ret = cc33xx_setup(cc);
	if (ret < 0)
		goto out_free_nvs;

	BUILD_BUG_ON(CC33XX_NUM_TX_DESCRIPTORS > CC33XX_MAX_TX_DESCRIPTORS);

	/* adjust some runtime configuration parameters */
	cc33xx_adjust_conf(cc);

	cc->if_ops = pdev_data->if_ops;
	cc->if_ops->set_irq_handler(cc->dev, irq_wrapper);

	cc33xx_power_off(cc);

	setup_wake_irq(cc);

	ret = cc33xx_init_fw(cc);
	if (ret < 0) {
		cc33xx_error("FW download failed");
		cc33xx_power_off(cc);
		goto out_irq;
	}

	ret = cc33xx_identify_chip(cc);
	if (ret < 0)
		goto out_irq;

	ret = read_version_info(cc);
	if (ret < 0)
		goto out_irq;

	ret = cc33xx_init_ieee80211(cc);
	if (ret)
		goto out_irq;

	ret = cc33xx_register_hw(cc);
	if (ret)
		goto out_irq;

	cc->initialized = true;
	goto out;

out_irq:
	if (cc->wakeirq >= 0)
		dev_pm_clear_wake_irq(cc->dev);
	device_init_wakeup(cc->dev, false);

out_free_nvs:
	kfree(cc->nvs_mac_addr);

out:
	release_firmware(fw);
	complete_all(&cc->nvs_loading_complete);
	cc33xx_debug(DEBUG_CC33xx, "%s complete", __func__);
}

static int cc33xx_load_ini_bin_file(struct device *dev,
				    struct cc33xx_conf_file *conf,
				    const char *file)
{
	struct cc33xx_conf_file *conf_file;
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, file, dev);
	if (ret < 0) {
		cc33xx_error("could not get configuration binary %s: %d",
			     file, ret);
		return ret;
	}

	if (fw->size != CC33X_CONF_SIZE) {
		cc33xx_error("%s configuration binary size is wrong, expected %zu got %zu",
			     file, CC33X_CONF_SIZE,
			     fw->size);
		ret = -EINVAL;
		goto out_release;
	}

	conf_file = (struct cc33xx_conf_file *)fw->data;

	if (conf_file->header.magic != cpu_to_le32(CC33XX_CONF_MAGIC)) {
		cc33xx_error("conf file magic number mismatch, expected 0x%0x got 0x%0x",
			     CC33XX_CONF_MAGIC, conf_file->header.magic);
		ret = -EINVAL;
		goto out_release;
	}

	memcpy(conf, conf_file, sizeof(*conf));

out_release:
	release_firmware(fw);
	return ret;
}

static int cc33xx_ini_bin_init(struct cc33xx *cc, struct device *dev)
{
	struct platform_device *pdev = cc->pdev;
	struct cc33xx_platdev_data *pdata = dev_get_platdata(&pdev->dev);

	if (cc33xx_load_ini_bin_file(dev, &cc->conf,
				     pdata->family->cfg_name) < 0)
		cc33xx_warning("falling back to default config");

	return 0;
}

static inline void cc33xx_set_ht_cap(struct cc33xx *cc, enum nl80211_band band,
				     struct ieee80211_sta_ht_cap *ht_cap)
{
	memcpy(&cc->ht_cap[band], ht_cap, sizeof(*ht_cap));
}

static int cc33xx_setup(struct cc33xx *cc)
{
	int ret;

	BUILD_BUG_ON(CC33XX_MAX_AP_STATIONS > CC33XX_MAX_LINKS);

	ret = cc33xx_ini_bin_init(cc, cc->dev);
	if (ret < 0)
		return ret;

	if (cc->conf.host_conf.ht.mode == HT_MODE_DEFAULT) {
		cc33xx_set_ht_cap(cc, NL80211_BAND_2GHZ,
				  &cc33xx_siso40_ht_cap_2ghz);

		/* 5Ghz is always wide */
		cc33xx_set_ht_cap(cc, NL80211_BAND_5GHZ,
				  &cc33xx_siso40_ht_cap_5ghz);
	} else if (cc->conf.host_conf.ht.mode == HT_MODE_WIDE) {
		cc33xx_set_ht_cap(cc, NL80211_BAND_2GHZ,
				  &cc33xx_siso40_ht_cap_2ghz);
		cc33xx_set_ht_cap(cc, NL80211_BAND_5GHZ,
				  &cc33xx_siso40_ht_cap_5ghz);
	} else if (cc->conf.host_conf.ht.mode == HT_MODE_SISO20) {
		cc33xx_set_ht_cap(cc, NL80211_BAND_2GHZ, &cc33xx_siso20_ht_cap);
		cc33xx_set_ht_cap(cc, NL80211_BAND_5GHZ, &cc33xx_siso20_ht_cap);
	}

	cc->event_mask = BSS_LOSS_EVENT_ID | SCAN_COMPLETE_EVENT_ID |
			 RADAR_DETECTED_EVENT_ID | RSSI_SNR_TRIGGER_0_EVENT_ID |
			 PERIODIC_SCAN_COMPLETE_EVENT_ID |
			 PERIODIC_SCAN_REPORT_EVENT_ID | DUMMY_PACKET_EVENT_ID |
			 PEER_REMOVE_COMPLETE_EVENT_ID |
			 BA_SESSION_RX_CONSTRAINT_EVENT_ID |
			 REMAIN_ON_CHANNEL_COMPLETE_EVENT_ID |
			 CHANNEL_SWITCH_COMPLETE_EVENT_ID |
			 DFS_CHANNELS_CONFIG_COMPLETE_EVENT |
			 SMART_CONFIG_SYNC_EVENT_ID | INACTIVE_STA_EVENT_ID |
			 SMART_CONFIG_DECODE_EVENT_ID | TIME_SYNC_EVENT_ID |
			 FW_LOGGER_INDICATION | RX_BA_WIN_SIZE_CHANGE_EVENT_ID;

	cc->ap_event_mask = MAX_TX_FAILURE_EVENT_ID;

	return 0;
}

static int cc33xx_probe(struct platform_device *pdev)
{
	struct cc33xx *cc;
	struct ieee80211_hw *hw;
	struct cc33xx_platdev_data *pdev_data = dev_get_platdata(&pdev->dev);
	const char *nvs_name;
	int ret;

	if (!pdev_data) {
		cc33xx_error("can't access platform data");
		return -EINVAL;
	}

	hw = cc33xx_alloc_hw(CC33XX_AGGR_BUFFER_SIZE);
	if (!hw) {
		ret = -ENOMEM;
		goto out;
	}
	cc = hw->priv;
	cc->dev = &pdev->dev;
	cc->pdev = pdev;
	platform_set_drvdata(pdev, cc);

	if (pdev_data->family && pdev_data->family->nvs_name) {
		nvs_name = pdev_data->family->nvs_name;
		ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_UEVENT,
					      nvs_name, &pdev->dev, GFP_KERNEL,
					      cc, cc33xx_nvs_cb);
		if (ret < 0) {
			cc33xx_error("request_firmware_nowait failed for %s: %d",
				     nvs_name, ret);
			complete_all(&cc->nvs_loading_complete);
		}
	} else {
		cc33xx_nvs_cb(NULL, cc);
		cc33xx_error("Invalid platform data entry");
		ret = -EINVAL;
	}

out:
	return ret;
}

static int cc33xx_remove(struct platform_device *pdev)
{
	struct cc33xx_platdev_data *pdev_data = dev_get_platdata(&pdev->dev);
	struct cc33xx *cc = platform_get_drvdata(pdev);

	set_bit(CC33XX_FLAG_DRIVER_REMOVED, &cc->flags);

	cc->dev->driver->pm = NULL;

	if (pdev_data->family && pdev_data->family->nvs_name)
		wait_for_completion(&cc->nvs_loading_complete);

	if (!cc->initialized)
		goto out;

	if (cc->wakeirq >= 0) {
		dev_pm_clear_wake_irq(cc->dev);
		cc->wakeirq = -ENODEV;
	}

	device_init_wakeup(cc->dev, false);
	cc33xx_unregister_hw(cc);
	cc33xx_turn_off(cc);

out:
	cc33xx_free_hw(cc);
	return 0;
}

static const struct platform_device_id cc33xx_id_table[] = {
	{ "cc33xx", 0 },
	{  }
};
MODULE_DEVICE_TABLE(platform, cc33xx_id_table);

static struct platform_driver cc33xx_driver = {
	.probe		= cc33xx_probe,
	.remove		= cc33xx_remove,
	.id_table	= cc33xx_id_table,
	.driver = {
		.name	= "cc33xx_driver",
	}
};

module_platform_driver(cc33xx_driver);

module_param_named(debug_level, cc33xx_debug_level, uint, 0600);
MODULE_PARM_DESC(debug_level, "cc33xx debugging level");

module_param(no_recovery, int, 0600);
MODULE_PARM_DESC(no_recovery, "Prevent HW recovery. FW will remain stuck.");

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Texas Instruments CC33xx WLAN driver");
MODULE_AUTHOR("Michael Nemanov <michael.nemanov@ti.com>");
MODULE_AUTHOR("Sabeeh Khan <sabeeh-khan@ti.com>");

MODULE_FIRMWARE(SECOND_LOADER_NAME);
MODULE_FIRMWARE(FW_NAME);

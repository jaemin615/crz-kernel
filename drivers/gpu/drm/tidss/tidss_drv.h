/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#ifndef __TIDSS_DRV_H__
#define __TIDSS_DRV_H__

#include <linux/spinlock.h>

#define TIDSS_MAX_PORTS 4
#define TIDSS_MAX_PLANES 4
#define TIDSS_MAX_OLDI_TXES 2

typedef u32 dispc_irq_t;

struct tidss_device {
	struct drm_device ddev;		/* DRM device for DSS */
	struct device *dev;		/* Underlying DSS device */

	const struct dispc_features *feat;
	struct dispc_device *dispc;

	unsigned int num_crtcs;
	struct drm_crtc *crtcs[TIDSS_MAX_PORTS];

	unsigned int num_planes;
	struct drm_plane *planes[TIDSS_MAX_PLANES];

	unsigned int num_oldis;
	struct tidss_oldi *oldis[TIDSS_MAX_OLDI_TXES];

	unsigned int irq;

	spinlock_t wait_lock;	/* protects the irq masks */
	dispc_irq_t irq_mask;	/* enabled irqs in addition to wait_list */

	bool shared_mode; /* DSS resources shared between remote core and Linux */
	/* 1: VP owned by Linux 0: VP is owned by remote and shared with Linux */
	u32 shared_mode_owned_vps[TIDSS_MAX_PORTS];
	bool shared_mode_own_oldi; /* Linux needs to configure OLDI in shared mode */

	int num_domains; /* Handle attached PM domains */
	struct device **pd_dev;
	struct device_link **pd_link;

	u32 boot_enabled_vp_mask;
	bool simplefb_enabled;
};

#define to_tidss(__dev) container_of(__dev, struct tidss_device, ddev)

int tidss_runtime_get(struct tidss_device *tidss);
void tidss_runtime_put(struct tidss_device *tidss);

#endif
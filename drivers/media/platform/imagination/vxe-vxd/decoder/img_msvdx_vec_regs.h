/* SPDX-License-Identifier: GPL-2.0 */
/*
 * IMG MSVDX VEC Registers
 *
 * Copyright (c) Imagination Technologies Ltd.
 * Copyright (c) 2021 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Authors:
 *	Amit Makani <amit.makani@ti.com>
 *
 * Re-written for upstreamimg
 *	Sidraya Jayagond <sidraya.bj@pathpartnertech.com>
 */

#if !defined(__MSVDX_VEC_REGS_H__)
#define __MSVDX_VEC_REGS_H__

#ifdef __cplusplus
extern "C" {
#endif

	/* MSVDX_VEC, CR_VEC_VLR_COMMANDS_NUM, VLR_COMMANDS_STORE_NUMBER_OF_CMDS */
#define MSVDX_VEC_CR_VEC_VLC_TABLE_ADDR0_OFFSET         (0x00EC)

	/* MSVDX_VEC, CR_VEC_VLC_TABLE_ADDR0, VLC_TABLE_ADDR0 */
#define MSVDX_VEC_CR_VEC_VLC_TABLE_ADDR0_VLC_TABLE_ADDR0_MASK           (0x000007FF)

	/* MSVDX_VEC, CR_VEC_VLC_TABLE_ADDR15, VLC_TABLE_ADDR31 */
#define MSVDX_VEC_CR_VEC_VLC_TABLE_ADDR16_OFFSET                (0x01C0)

	/* MSVDX_VEC, CR_VEC_VLC_TABLE_ADDR15, VLC_TABLE_ADDR31 */
#define MSVDX_VEC_CR_VEC_VLC_TABLE_ADDR16_OFFSET                (0x01C0)

	/* MSVDX_VEC, CR_VEC_VLC_TABLE_ADDR18, VLC_TABLE_ADDR37 */
#define MSVDX_VEC_CR_VEC_VLC_TABLE_INITIAL_WIDTH0_OFFSET                (0x012C)

	/* MSVDX_VEC, CR_VEC_VLC_TABLE_ADDR0, VLC_TABLE_ADDR1 */
#define MSVDX_VEC_CR_VEC_VLC_TABLE_ADDR0_VLC_TABLE_ADDR1_SHIFT          (11)

	/* MSVDX_VEC, CR_VEC_VLC_TABLE_INITIAL_WIDTH0, VLC_TABLE_INITIAL_WIDTH0 */
#define MSVDX_VEC_CR_VEC_VLC_TABLE_INITIAL_WIDTH0_VLC_TABLE_INITIAL_WIDTH0_MASK         (0x00000007)

	/* MSVDX_VEC, CR_VEC_VLC_TABLE_INITIAL_WIDTH0, VLC_TABLE_INITIAL_WIDTH1 */
#define MSVDX_VEC_CR_VEC_VLC_TABLE_INITIAL_WIDTH0_VLC_TABLE_INITIAL_WIDTH1_SHIFT	(3)

	/* MSVDX_VEC, CR_VEC_VLC_TABLE_INITIAL_OPCODE0, VLC_TABLE_INITIAL_OPCODE0 */
#define MSVDX_VEC_CR_VEC_VLC_TABLE_INITIAL_OPCODE0_VLC_TABLE_INITIAL_OPCODE0_MASK \
	(0x00000003)

	/* MSVDX_VEC, CR_VEC_VLC_TABLE_INITIAL_WIDTH3, VLC_TABLE_INITIAL_WIDTH37 */
#define MSVDX_VEC_CR_VEC_VLC_TABLE_INITIAL_OPCODE0_OFFSET               (0x013C)

	/* MSVDX_VEC, CR_VEC_VLC_TABLE_INITIAL_OPCODE0, VLC_TABLE_INITIAL_OPCODE1 */
#define MSVDX_VEC_CR_VEC_VLC_TABLE_INITIAL_OPCODE0_VLC_TABLE_INITIAL_OPCODE1_SHIFT              (2)

#ifdef __cplusplus
}
#endif

#endif /* __MSVDX_VEC_REGS_H__ */
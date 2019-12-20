/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2019 Joyent, Inc.
 */

#ifndef	_TOPO_SAS_H
#define	_TOPO_SAS_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	TOPO_VTX_EXPANDER	"expander"
#define	TOPO_VTX_INITIATOR	"initiator"
#define	TOPO_VTX_PORT		"port"
#define	TOPO_VTX_TARGET		"target"

#define	TOPO_PGROUP_EXPANDER		"expander-properties"
#define	TOPO_PROP_EXPANDER_DEVFS_PATH	"devfs-path"
#define	TOPO_PROP_EXPANDER_MANUF	"manufacturer"
#define	TOPO_PROP_EXPANDER_MODEL	"model"

#define	TOPO_PGROUP_INITIATOR		"initiator-properties"
#define	TOPO_PROP_INITIATOR_DEV_FMRI	"dev-fmri"
#define	TOPO_PROP_INITIATOR_DEVFS_PATH	"devfs-path"
#define	TOPO_PROP_INITIATOR_FMRI	"hc-fmri"
#define	TOPO_PROP_INITIATOR_LABEL	"location"
#define	TOPO_PROP_INITIATOR_MANUF	"manufacturer"
#define	TOPO_PROP_INITIATOR_MODEL	"model"

#define	TOPO_PGROUP_SASPORT		"port-properties"
#define	TOPO_PROP_SASPORT_APORT		"adapter-port"
#define	TOPO_PROP_SASPORT_ATTACH_ADDR	"attached-sas-address"
#define	TOPO_PROP_SASPORT_LOCAL_ADDR	"local-sas-address"
#define	TOPO_PROP_SASPORT_ANAME		"phy-error-source"

#define	TOPO_PROP_SASPORT_TYPE		"sas-port-type"
#define	TOPO_SASPORT_TYPE_INITIATOR	"initiator-port"
#define	TOPO_SASPORT_TYPE_EXPANDER	"expander-port"
#define	TOPO_SASPORT_TYPE_TARGET	"target-port"

/*
 * PHY link state error counters
 */
#define	TOPO_PROP_SASPORT_INV_DWORD	"invalid-dword"
#define	TOPO_PROP_SASPORT_RUN_DISP	"running-disparity-error"
#define	TOPO_PROP_SASPORT_LOSS_SYNC	"loss-dword-sync"
#define	TOPO_PROP_SASPORT_RESET_PROB	"reset-problem-count"

/*
 * PHY link transmission rate states
 */
#define	TOPO_PROP_SASPORT_MAX_RATE	"max-link-rate"
#define	TOPO_PROP_SASPORT_PROG_RATE	"programmed-link-rate"
#define	TOPO_PROP_SASPORT_NEG_RATE	"negotiated-link-rate"

#define	TOPO_PGROUP_TARGET		"target-properties"
#define	TOPO_PROP_TARGET_DEV_FMRI	"dev-fmri"
#define	TOPO_PROP_TARGET_DEVFS_PATH	"devfs-path"
#define	TOPO_PROP_TARGET_DEVID		"devid"
#define	TOPO_PROP_TARGET_HC_FMRI	"hc-fmri"
#define	TOPO_PROP_TARGET_LABEL		"location"
#define	TOPO_PROP_TARGET_LOGICAL_DISK	"logical-disk"
#define	TOPO_PROP_TARGET_MANUF		"manufacturer"
#define	TOPO_PROP_TARGET_MODEL		"model"
#define	TOPO_PROP_TARGET_SERIAL		"serial-number"

#ifdef	__cplusplus
}
#endif

#endif	/* _TOPO_SAS_H */

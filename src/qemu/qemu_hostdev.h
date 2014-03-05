/*
 * qemu_hostdev.h: QEMU hostdev management
 *
 * Copyright (C) 2006-2007, 2009-2013 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Daniel P. Berrange <berrange@redhat.com>
 */

#ifndef __QEMU_HOSTDEV_H__
# define __QEMU_HOSTDEV_H__

# include "qemu_conf.h"
# include "domain_conf.h"

typedef enum {
    VIR_HOSTDEV_STRICT_ACS_CHECK     = (1 << 0), /* strict acs check */
    VIR_HOSTDEV_COLD_BOOT            = (1 << 1), /* cold boot */
} virHostdevFlag;

int qemuUpdateActivePciHostdevs(virQEMUDriverPtr driver,
                                virDomainDefPtr def);
int qemuUpdateActiveUsbHostdevs(virQEMUDriverPtr driver,
                                virDomainDefPtr def);
int qemuUpdateActiveScsiHostdevs(virQEMUDriverPtr driver,
                                 virDomainDefPtr def);
bool qemuHostdevHostSupportsPassthroughLegacy(void);
bool qemuHostdevHostSupportsPassthroughVFIO(void);
int qemuPrepareHostdevPCIDevices(virQEMUDriverPtr driver,
                                 const char *name,
                                 const unsigned char *uuid,
                                 virDomainHostdevDefPtr *hostdevs,
                                 int nhostdevs,
                                 virQEMUCapsPtr qemuCaps,
                                 unsigned int flags);
int
qemuPrepareHostUSBDevices(virQEMUDriverPtr driver,
                          const char *name,
                          virDomainHostdevDefPtr *hostdevs,
                          int nhostdevs,
                          unsigned int flags);
int qemuPrepareHostdevSCSIDevices(virQEMUDriverPtr driver,
                                  const char *name,
                                  virDomainHostdevDefPtr *hostdevs,
                                  int nhostdevs);
int qemuPrepareHostDevices(virQEMUDriverPtr driver,
                           virDomainDefPtr def,
                           virQEMUCapsPtr qemuCaps,
                           unsigned int flags);
void
qemuDomainReAttachHostUsbDevices(virQEMUDriverPtr driver,
                                 const char *name,
                                 virDomainHostdevDefPtr *hostdevs,
                                 int nhostdevs);
void qemuDomainReAttachHostScsiDevices(virQEMUDriverPtr driver,
                                       const char *name,
                                       virDomainHostdevDefPtr *hostdevs,
                                       int nhostdevs);
void qemuDomainReAttachHostdevDevices(virQEMUDriverPtr driver,
                                      const char *name,
                                      virDomainHostdevDefPtr *hostdevs,
                                      int nhostdevs);
void qemuDomainReAttachHostDevices(virQEMUDriverPtr driver,
                                   virDomainDefPtr def);

#endif /* __QEMU_HOSTDEV_H__ */

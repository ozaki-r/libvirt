/*
 * parallels_sdk.c: core driver functions for managing
 * Parallels Cloud Server hosts
 *
 * Copyright (C) 2014 Parallels, Inc.
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
 */

#include <config.h>

#include "virerror.h"
#include "viralloc.h"
#include "virstring.h"
#include "nodeinfo.h"
#include "virlog.h"

#include "parallels_sdk.h"

#define VIR_FROM_THIS VIR_FROM_PARALLELS
#define JOB_INFINIT_WAIT_TIMEOUT UINT_MAX

PRL_UINT32 defaultJobTimeout = JOB_INFINIT_WAIT_TIMEOUT;

VIR_LOG_INIT("parallels.sdk");

/*
 * Log error description
 */
static void
logPrlErrorHelper(PRL_RESULT err, const char *filename,
                  const char *funcname, size_t linenr)
{
    char *msg1 = NULL, *msg2 = NULL;
    PRL_UINT32 len = 0;

    /* Get required buffer length */
    PrlApi_GetResultDescription(err, PRL_TRUE, PRL_FALSE, NULL, &len);

    if (VIR_ALLOC_N(msg1, len) < 0)
        goto cleanup;

    /* get short error description */
    PrlApi_GetResultDescription(err, PRL_TRUE, PRL_FALSE, msg1, &len);

    PrlApi_GetResultDescription(err, PRL_FALSE, PRL_FALSE, NULL, &len);

    if (VIR_ALLOC_N(msg2, len) < 0)
        goto cleanup;

    /* get long error description */
    PrlApi_GetResultDescription(err, PRL_FALSE, PRL_FALSE, msg2, &len);

    virReportErrorHelper(VIR_FROM_THIS, VIR_ERR_INTERNAL_ERROR,
                         filename, funcname, linenr,
                         _("%s %s"), msg1, msg2);

 cleanup:
    VIR_FREE(msg1);
    VIR_FREE(msg2);
}

#define logPrlError(code)                          \
    logPrlErrorHelper(code, __FILE__,              \
                         __FUNCTION__, __LINE__)

#define prlsdkCheckRetGoto(ret, label)             \
    do {                                           \
        if (PRL_FAILED(ret)) {                     \
            logPrlError(ret);                      \
            goto label;                            \
        }                                          \
    } while (0)

static PRL_RESULT
logPrlEventErrorHelper(PRL_HANDLE event, const char *filename,
                       const char *funcname, size_t linenr)
{
    PRL_RESULT ret, retCode;
    char *msg1 = NULL, *msg2 = NULL;
    PRL_UINT32 len = 0;
    int err = -1;

    if ((ret = PrlEvent_GetErrCode(event, &retCode))) {
        logPrlError(ret);
        return ret;
    }

    PrlEvent_GetErrString(event, PRL_TRUE, PRL_FALSE, NULL, &len);

    if (VIR_ALLOC_N(msg1, len) < 0)
        goto cleanup;

    PrlEvent_GetErrString(event, PRL_TRUE, PRL_FALSE, msg1, &len);

    PrlEvent_GetErrString(event, PRL_FALSE, PRL_FALSE, NULL, &len);

    if (VIR_ALLOC_N(msg2, len) < 0)
        goto cleanup;

    PrlEvent_GetErrString(event, PRL_FALSE, PRL_FALSE, msg2, &len);

    virReportErrorHelper(VIR_FROM_THIS, VIR_ERR_INTERNAL_ERROR,
                         filename, funcname, linenr,
                         _("%s %s"), msg1, msg2);
    err = 0;

 cleanup:
    VIR_FREE(msg1);
    VIR_FREE(msg2);

    return err;
}

#define logPrlEventError(event)                    \
    logPrlEventErrorHelper(event, __FILE__,        \
                         __FUNCTION__, __LINE__)

static PRL_HANDLE
getJobResultHelper(PRL_HANDLE job, unsigned int timeout,
                   const char *filename, const char *funcname,
                   size_t linenr)
{
    PRL_RESULT ret, retCode;
    PRL_HANDLE result = NULL;

    if ((ret = PrlJob_Wait(job, timeout))) {
        logPrlErrorHelper(ret, filename, funcname, linenr);
        goto cleanup;
    }

    if ((ret = PrlJob_GetRetCode(job, &retCode))) {
        logPrlErrorHelper(ret, filename, funcname, linenr);
        goto cleanup;
    }

    if (retCode) {
        PRL_HANDLE err_handle;

        /* Sometimes it's possible to get additional error info. */
        if ((ret = PrlJob_GetError(job, &err_handle))) {
            logPrlErrorHelper(ret, filename, funcname, linenr);
            goto cleanup;
        }

        if (logPrlEventErrorHelper(err_handle, filename, funcname, linenr))
            logPrlErrorHelper(retCode, filename, funcname, linenr);

        PrlHandle_Free(err_handle);
    } else {
        ret = PrlJob_GetResult(job, &result);
        if (PRL_FAILED(ret)) {
            logPrlErrorHelper(ret, filename, funcname, linenr);
            PrlHandle_Free(result);
            result = NULL;
            goto cleanup;
        }
   }

 cleanup:
    PrlHandle_Free(job);
    return result;
}

#define getJobResult(job, timeout)                  \
    getJobResultHelper(job, timeout, __FILE__,      \
                         __FUNCTION__, __LINE__)

static int
waitJobHelper(PRL_HANDLE job, unsigned int timeout,
              const char *filename, const char *funcname,
              size_t linenr)
{
    PRL_HANDLE result = NULL;

    result = getJobResultHelper(job, timeout, filename, funcname, linenr);
    if (result)
        PrlHandle_Free(result);

    return result ? 0 : -1;
}

#define waitJob(job, timeout)                  \
    waitJobHelper(job, timeout, __FILE__,      \
                         __FUNCTION__, __LINE__)

int
prlsdkInit(parallelsConnPtr privconn)
{
    PRL_RESULT ret;

    ret = PrlApi_InitEx(PARALLELS_API_VER, PAM_SERVER, 0, 0);
    if (PRL_FAILED(ret)) {
        logPrlError(ret);
        return -1;
    }

    privconn->jobTimeout = JOB_INFINIT_WAIT_TIMEOUT;

    return 0;
};

void
prlsdkDeinit(void)
{
    PrlApi_Deinit();
};

int
prlsdkConnect(parallelsConnPtr privconn)
{
    PRL_RESULT ret;
    PRL_HANDLE job = PRL_INVALID_HANDLE;

    ret = PrlSrv_Create(&privconn->server);
    if (PRL_FAILED(ret)) {
        logPrlError(ret);
        return -1;
    }

    job = PrlSrv_LoginLocalEx(privconn->server, NULL, 0,
                              PSL_HIGH_SECURITY, PACF_NON_INTERACTIVE_MODE);

    if (waitJob(job, privconn->jobTimeout)) {
        PrlHandle_Free(privconn->server);
        return -1;
    }

    return 0;
}

void
prlsdkDisconnect(parallelsConnPtr privconn)
{
    PRL_HANDLE job;

    job = PrlSrv_Logoff(privconn->server);
    waitJob(job, privconn->jobTimeout);

    PrlHandle_Free(privconn->server);
}

static int
prlsdkSdkDomainLookup(parallelsConnPtr privconn,
                      const char *id,
                      unsigned int flags,
                      PRL_HANDLE *sdkdom)
{
    PRL_HANDLE job = PRL_INVALID_HANDLE;
    PRL_HANDLE result = PRL_INVALID_HANDLE;
    PRL_RESULT pret = PRL_ERR_UNINITIALIZED;
    int ret = -1;

    job = PrlSrv_GetVmConfig(privconn->server, id, flags);
    if (!(result = getJobResult(job, privconn->jobTimeout)))
        goto cleanup;

    pret = PrlResult_GetParamByIndex(result, 0, sdkdom);
    prlsdkCheckRetGoto(pret, cleanup);

    ret = 0;

 cleanup:
    PrlHandle_Free(result);
    return ret;
}

static void
prlsdkUUIDFormat(const unsigned char *uuid, char *uuidstr)
{
    virUUIDFormat(uuid, uuidstr + 1);

    uuidstr[0] = '{';
    uuidstr[VIR_UUID_STRING_BUFLEN] = '}';
    uuidstr[VIR_UUID_STRING_BUFLEN + 1] = '\0';
}

static PRL_HANDLE
prlsdkSdkDomainLookupByUUID(parallelsConnPtr privconn, const unsigned char *uuid)
{
    char uuidstr[VIR_UUID_STRING_BUFLEN + 2];
    PRL_HANDLE sdkdom = PRL_INVALID_HANDLE;

    prlsdkUUIDFormat(uuid, uuidstr);

    if (prlsdkSdkDomainLookup(privconn, uuidstr,
                              PGVC_SEARCH_BY_UUID, &sdkdom) < 0) {
        virUUIDFormat(uuid, uuidstr);
        virReportError(VIR_ERR_NO_DOMAIN,
                       _("no domain with matching uuid '%s'"), uuidstr);
        return PRL_INVALID_HANDLE;
    }

    return sdkdom;
}

static int
prlsdkUUIDParse(const char *uuidstr, unsigned char *uuid)
{
    char *tmp = NULL;
    int ret = -1;

    virCheckNonNullArgGoto(uuidstr, error);
    virCheckNonNullArgGoto(uuid, error);

    if (VIR_STRDUP(tmp, uuidstr) < 0)
        goto error;

    tmp[strlen(tmp) - 1] = '\0';

    /* trim curly braces */
    if (virUUIDParse(tmp + 1, uuid) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("UUID in config file malformed"));
        ret = -1;
        goto error;
    }

    ret = 0;
 error:
    VIR_FREE(tmp);
    return ret;
}

static int
prlsdkGetDomainIds(PRL_HANDLE sdkdom,
                   char **name,
                   unsigned char *uuid)
{
    char uuidstr[VIR_UUID_STRING_BUFLEN + 2];
    PRL_UINT32 len;
    PRL_RESULT pret;

    len = 0;
    /* get name length */
    pret = PrlVmCfg_GetName(sdkdom, NULL, &len);
    prlsdkCheckRetGoto(pret, error);

    if (VIR_ALLOC_N(*name, len) < 0)
        goto error;

    PrlVmCfg_GetName(sdkdom, *name, &len);
    prlsdkCheckRetGoto(pret, error);

    len = sizeof(uuidstr);
    PrlVmCfg_GetUuid(sdkdom, uuidstr, &len);
    prlsdkCheckRetGoto(pret, error);

    if (prlsdkUUIDParse(uuidstr, uuid) < 0)
        goto error;

    return 0;

 error:
    VIR_FREE(*name);
    return -1;
}

static int
prlsdkGetDomainState(parallelsConnPtr privconn,
                     PRL_HANDLE sdkdom,
                     VIRTUAL_MACHINE_STATE_PTR vmState)
{
    PRL_HANDLE job = PRL_INVALID_HANDLE;
    PRL_HANDLE result = PRL_INVALID_HANDLE;
    PRL_HANDLE vmInfo = PRL_INVALID_HANDLE;
    PRL_RESULT pret;
    int ret = -1;

    job = PrlVm_GetState(sdkdom);

    if (!(result = getJobResult(job, privconn->jobTimeout)))
        goto cleanup;

    pret = PrlResult_GetParamByIndex(result, 0, &vmInfo);
    prlsdkCheckRetGoto(pret, cleanup);

    pret = PrlVmInfo_GetState(vmInfo, vmState);
    prlsdkCheckRetGoto(pret, cleanup);

    ret = 0;

 cleanup:
    PrlHandle_Free(vmInfo);
    PrlHandle_Free(result);
    return ret;
}

static void
prlsdkDomObjFreePrivate(void *p)
{
    parallelsDomObjPtr pdom = p;

    if (!pdom)
        return;

    PrlHandle_Free(pdom->sdkdom);
    virBitmapFree(pdom->cpumask);
    VIR_FREE(pdom->uuid);
    VIR_FREE(pdom->home);
    VIR_FREE(p);
};

static int
prlsdkAddDomainVideoInfo(PRL_HANDLE sdkdom, virDomainDefPtr def)
{
    virDomainVideoDefPtr video = NULL;
    virDomainVideoAccelDefPtr accel = NULL;
    PRL_RESULT ret;
    PRL_UINT32 videoRam;

    /* video info */
    ret = PrlVmCfg_GetVideoRamSize(sdkdom, &videoRam);
    prlsdkCheckRetGoto(ret, error);

    if (VIR_ALLOC(video) < 0)
        goto error;

    if (VIR_ALLOC(accel) < 0)
        goto error;

    if (VIR_APPEND_ELEMENT_COPY(def->videos, def->nvideos, video) < 0)
        goto error;

    video->type = VIR_DOMAIN_VIDEO_TYPE_VGA;
    video->vram = videoRam << 10; /* from mbibytes to kbibytes */
    video->heads = 1;
    video->accel = accel;

    return 0;

 error:
    VIR_FREE(accel);
    virDomainVideoDefFree(video);
    return -1;
}

static int
prlsdkGetDiskInfo(PRL_HANDLE prldisk,
                  virDomainDiskDefPtr disk)
{
    char *buf = NULL;
    PRL_UINT32 buflen = 0;
    PRL_RESULT pret;
    PRL_UINT32 emulatedType;
    PRL_UINT32 ifType;
    PRL_UINT32 pos;
    PRL_UINT32 prldiskIndex;
    int ret = -1;

    pret = PrlVmDev_GetEmulatedType(prldisk, &emulatedType);
    prlsdkCheckRetGoto(pret, cleanup);
    if (emulatedType == PDT_USE_IMAGE_FILE) {
        virDomainDiskSetType(disk, VIR_STORAGE_TYPE_FILE);
        virDomainDiskSetFormat(disk, VIR_STORAGE_FILE_PLOOP);
    } else {
        virDomainDiskSetType(disk, VIR_STORAGE_TYPE_BLOCK);
    }

    pret = PrlVmDev_GetFriendlyName(prldisk, NULL, &buflen);
    prlsdkCheckRetGoto(pret, cleanup);

    if (VIR_ALLOC_N(buf, buflen) < 0)
        goto cleanup;

    pret = PrlVmDev_GetFriendlyName(prldisk, buf, &buflen);
    prlsdkCheckRetGoto(pret, cleanup);

    if (virDomainDiskSetSource(disk, buf) < 0)
        goto cleanup;

    pret = PrlVmDev_GetIfaceType(prldisk, &ifType);
    prlsdkCheckRetGoto(pret, cleanup);
    switch (ifType) {
    case PMS_IDE_DEVICE:
        disk->bus = VIR_DOMAIN_DISK_BUS_IDE;
        break;
    case PMS_SCSI_DEVICE:
        disk->bus = VIR_DOMAIN_DISK_BUS_SCSI;
        break;
    case PMS_SATA_DEVICE:
        disk->bus = VIR_DOMAIN_DISK_BUS_SATA;
        break;
    default:
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unknown disk bus: %X"), ifType);
        goto cleanup;
        break;
    }

    pret = PrlVmDev_GetStackIndex(prldisk, &pos);
    prlsdkCheckRetGoto(pret, cleanup);

    disk->info.type = VIR_DOMAIN_DEVICE_ADDRESS_TYPE_DRIVE;
    disk->info.addr.drive.target = pos;

    pret = PrlVmDev_GetIndex(prldisk, &prldiskIndex);
    prlsdkCheckRetGoto(pret, cleanup);

    if (!(disk->dst = virIndexToDiskName(prldiskIndex, "sd")))
        goto cleanup;

    ret = 0;

 cleanup:
    VIR_FREE(buf);
    return ret;
}

static int
prlsdkAddDomainHardDisksInfo(PRL_HANDLE sdkdom, virDomainDefPtr def)
{
    PRL_RESULT pret;
    PRL_UINT32 hddCount;
    PRL_UINT32 i;
    PRL_HANDLE hdd = PRL_INVALID_HANDLE;
    virDomainDiskDefPtr disk = NULL;

    pret = PrlVmCfg_GetHardDisksCount(sdkdom, &hddCount);
    prlsdkCheckRetGoto(pret, error);

    for (i = 0; i < hddCount; ++i) {
        pret = PrlVmCfg_GetHardDisk(sdkdom, i, &hdd);
        prlsdkCheckRetGoto(pret, error);

        if (IS_CT(def)) {
            /* TODO: convert info about disks in container
             * to virDomainFSDef structs */
            VIR_WARN("Skipping disk information for container");

            PrlHandle_Free(hdd);
            hdd = PRL_INVALID_HANDLE;
        } else {
            if (!(disk = virDomainDiskDefNew()))
                goto error;

            if (prlsdkGetDiskInfo(hdd, disk) < 0)
                goto error;

            PrlHandle_Free(hdd);
            hdd = PRL_INVALID_HANDLE;

            if (VIR_APPEND_ELEMENT(def->disks, def->ndisks, disk) < 0)
                goto error;
        }
    }

    return 0;

 error:
    PrlHandle_Free(hdd);
    virDomainDiskDefFree(disk);
    return -1;
}

static int
prlsdkGetNetInfo(PRL_HANDLE netAdapter, virDomainNetDefPtr net, bool isCt)
{
    char macstr[VIR_MAC_STRING_BUFLEN];
    PRL_UINT32 buflen;
    PRL_UINT32 netAdapterIndex;
    PRL_UINT32 emulatedType;
    PRL_RESULT pret;
    PRL_BOOL isConnected;
    int ret = -1;

    net->type = VIR_DOMAIN_NET_TYPE_NETWORK;


    /* use device name, shown by prlctl as target device
     * for identifying network adapter in virDomainDefineXML */
    pret = PrlVmDev_GetIndex(netAdapter, &netAdapterIndex);
    prlsdkCheckRetGoto(pret, cleanup);

    pret = PrlVmDevNet_GetHostInterfaceName(netAdapter, NULL, &buflen);
    prlsdkCheckRetGoto(pret, cleanup);

    if (VIR_ALLOC_N(net->ifname, buflen) < 0)
        goto cleanup;

    pret = PrlVmDevNet_GetHostInterfaceName(netAdapter, net->ifname, &buflen);
        prlsdkCheckRetGoto(pret, cleanup);

    if (isCt && netAdapterIndex == (PRL_UINT32) -1) {
        /* venet devices don't have mac address and
         * always up */
        net->linkstate = VIR_DOMAIN_NET_INTERFACE_LINK_STATE_UP;
        if (VIR_STRDUP(net->data.network.name,
                       PARALLELS_ROUTED_NETWORK_NAME) < 0)
            goto cleanup;
        return 0;
    }

    buflen = ARRAY_CARDINALITY(macstr);
    if (VIR_ALLOC_N(macstr, buflen))
        goto cleanup;
    pret = PrlVmDevNet_GetMacAddressCanonical(netAdapter, macstr, &buflen);
    prlsdkCheckRetGoto(pret, cleanup);

    if (virMacAddrParse(macstr, &net->mac) < 0)
        goto cleanup;

    pret = PrlVmDev_GetEmulatedType(netAdapter, &emulatedType);
    prlsdkCheckRetGoto(pret, cleanup);

    if (emulatedType == PNA_ROUTED) {
        if (VIR_STRDUP(net->data.network.name,
                       PARALLELS_ROUTED_NETWORK_NAME) < 0)
            goto cleanup;
    } else {
        pret = PrlVmDevNet_GetVirtualNetworkId(netAdapter, NULL, &buflen);
        prlsdkCheckRetGoto(pret, cleanup);

        if (VIR_ALLOC_N(net->data.network.name, buflen) < 0)
            goto cleanup;

        pret = PrlVmDevNet_GetVirtualNetworkId(netAdapter,
                                               net->data.network.name,
                                               &buflen);
        prlsdkCheckRetGoto(pret, cleanup);
    }

    pret = PrlVmDev_IsConnected(netAdapter, &isConnected);
    prlsdkCheckRetGoto(pret, cleanup);

    if (isConnected)
        net->linkstate = VIR_DOMAIN_NET_INTERFACE_LINK_STATE_UP;
    else
        net->linkstate = VIR_DOMAIN_NET_INTERFACE_LINK_STATE_DOWN;

    ret = 0;
 cleanup:
    return ret;
}

static int
prlsdkAddDomainNetInfo(PRL_HANDLE sdkdom, virDomainDefPtr def)
{
    virDomainNetDefPtr net = NULL;
    PRL_RESULT ret;
    PRL_HANDLE netAdapter;
    PRL_UINT32 netAdaptersCount;
    PRL_UINT32 i;

    ret = PrlVmCfg_GetNetAdaptersCount(sdkdom, &netAdaptersCount);
    prlsdkCheckRetGoto(ret, error);
    for (i = 0; i < netAdaptersCount; ++i) {
        ret = PrlVmCfg_GetNetAdapter(sdkdom, i, &netAdapter);
        prlsdkCheckRetGoto(ret, error);

        if (VIR_ALLOC(net) < 0)
            goto error;

        if (prlsdkGetNetInfo(netAdapter, net, IS_CT(def)) < 0)
            goto error;

        PrlHandle_Free(netAdapter);
        netAdapter = PRL_INVALID_HANDLE;

        if (VIR_APPEND_ELEMENT(def->nets, def->nnets, net) < 0)
            goto error;
    }

    return 0;

 error:
    PrlHandle_Free(netAdapter);
    virDomainNetDefFree(net);
    return -1;
}

static int
prlsdkGetSerialInfo(PRL_HANDLE serialPort, virDomainChrDefPtr chr)
{
    PRL_RESULT pret;
    PRL_UINT32 serialPortIndex;
    PRL_UINT32 emulatedType;
    char *friendlyName = NULL;
    PRL_UINT32 buflen;

    chr->deviceType = VIR_DOMAIN_CHR_DEVICE_TYPE_SERIAL;
    chr->targetTypeAttr = false;
    pret = PrlVmDev_GetIndex(serialPort, &serialPortIndex);
    prlsdkCheckRetGoto(pret, error);
    chr->target.port = serialPortIndex;

    pret = PrlVmDev_GetEmulatedType(serialPort, &emulatedType);
    prlsdkCheckRetGoto(pret, error);

    pret = PrlVmDev_GetFriendlyName(serialPort, NULL, &buflen);
    prlsdkCheckRetGoto(pret, error);

    if (VIR_ALLOC_N(friendlyName, buflen) < 0)
        goto error;

    pret = PrlVmDev_GetFriendlyName(serialPort, friendlyName, &buflen);
    prlsdkCheckRetGoto(pret, error);

    switch (emulatedType) {
    case PDT_USE_OUTPUT_FILE:
        chr->source.type = VIR_DOMAIN_CHR_TYPE_FILE;
        chr->source.data.file.path = friendlyName;
        break;
    case PDT_USE_SERIAL_PORT_SOCKET_MODE:
        chr->source.type = VIR_DOMAIN_CHR_TYPE_UNIX;
        chr->source.data.nix.path = friendlyName;
        break;
    case PDT_USE_REAL_DEVICE:
        chr->source.type = VIR_DOMAIN_CHR_TYPE_DEV;
        chr->source.data.file.path = friendlyName;
        break;
    default:
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unknown serial type: %X"), emulatedType);
        goto error;
        break;
    }

    return 0;
 error:
    VIR_FREE(friendlyName);
    return -1;
}


static int
prlsdkAddSerialInfo(PRL_HANDLE sdkdom,
                    virDomainChrDefPtr **serials,
                    size_t *nserials)
{
    PRL_RESULT ret;
    PRL_HANDLE serialPort;
    PRL_UINT32 serialPortsCount;
    PRL_UINT32 i;
    virDomainChrDefPtr chr = NULL;

    ret = PrlVmCfg_GetSerialPortsCount(sdkdom, &serialPortsCount);
    prlsdkCheckRetGoto(ret, cleanup);
    for (i = 0; i < serialPortsCount; ++i) {
        ret = PrlVmCfg_GetSerialPort(sdkdom, i, &serialPort);
        prlsdkCheckRetGoto(ret, cleanup);

        if (!(chr = virDomainChrDefNew()))
            goto cleanup;

        if (prlsdkGetSerialInfo(serialPort, chr))
            goto cleanup;

        PrlHandle_Free(serialPort);
        serialPort = PRL_INVALID_HANDLE;

        if (VIR_APPEND_ELEMENT(*serials, *nserials, chr) < 0)
            goto cleanup;
    }

    return 0;

 cleanup:
    PrlHandle_Free(serialPort);
    virDomainChrDefFree(chr);
    return -1;
}


static int
prlsdkAddDomainHardware(PRL_HANDLE sdkdom, virDomainDefPtr def)
{
    if (!IS_CT(def))
        if (prlsdkAddDomainVideoInfo(sdkdom, def) < 0)
            goto error;

    if (prlsdkAddDomainHardDisksInfo(sdkdom, def) < 0)
        goto error;

    if (prlsdkAddDomainNetInfo(sdkdom, def) < 0)
        goto error;

    if (prlsdkAddSerialInfo(sdkdom,
                            &def->serials,
                            &def->nserials) < 0)
        goto error;

    return 0;
 error:
    return -1;
}


static int
prlsdkAddVNCInfo(PRL_HANDLE sdkdom, virDomainDefPtr def)
{
    virDomainGraphicsDefPtr gr = NULL;
    PRL_VM_REMOTE_DISPLAY_MODE vncMode;
    PRL_UINT32 port;
    PRL_UINT32 buflen = 0;
    PRL_RESULT pret;

    pret = PrlVmCfg_GetVNCMode(sdkdom, &vncMode);
    prlsdkCheckRetGoto(pret, error);

    if (vncMode == PRD_DISABLED)
        return 0;

    if (VIR_ALLOC(gr) < 0)
        goto error;

    pret = PrlVmCfg_GetVNCPort(sdkdom, &port);
    prlsdkCheckRetGoto(pret, error);

    gr->data.vnc.autoport = (vncMode == PRD_AUTO);
    gr->type = VIR_DOMAIN_GRAPHICS_TYPE_VNC;
    gr->data.vnc.port = port;
    gr->data.vnc.keymap = NULL;
    gr->data.vnc.socket = NULL;
    gr->data.vnc.auth.passwd = NULL;
    gr->data.vnc.auth.expires = false;
    gr->data.vnc.auth.connected = 0;

    if (VIR_ALLOC(gr->listens) < 0)
        goto error;

    gr->nListens = 1;

    pret = PrlVmCfg_GetVNCHostName(sdkdom, NULL, &buflen);
    prlsdkCheckRetGoto(pret, error);

    if (VIR_ALLOC_N(gr->listens[0].address, buflen) < 0)
        goto error;

    pret = PrlVmCfg_GetVNCHostName(sdkdom, gr->listens[0].address, &buflen);
    prlsdkCheckRetGoto(pret, error);

    gr->listens[0].type = VIR_DOMAIN_GRAPHICS_LISTEN_TYPE_ADDRESS;

    if (VIR_APPEND_ELEMENT(def->graphics, def->ngraphics, gr) < 0)
        goto error;

    return 0;

 error:
    virDomainGraphicsDefFree(gr);
    return -1;
}

static int
prlsdkConvertDomainState(VIRTUAL_MACHINE_STATE domainState,
                         PRL_UINT32 envId,
                         virDomainObjPtr dom)
{
    switch (domainState) {
    case VMS_STOPPED:
    case VMS_MOUNTED:
        virDomainObjSetState(dom, VIR_DOMAIN_SHUTOFF,
                             VIR_DOMAIN_SHUTOFF_SHUTDOWN);
        dom->def->id = -1;
        break;
    case VMS_STARTING:
    case VMS_COMPACTING:
    case VMS_RESETTING:
    case VMS_PAUSING:
    case VMS_RECONNECTING:
    case VMS_RUNNING:
        virDomainObjSetState(dom, VIR_DOMAIN_RUNNING,
                             VIR_DOMAIN_RUNNING_BOOTED);
        dom->def->id = envId;
        break;
    case VMS_PAUSED:
        virDomainObjSetState(dom, VIR_DOMAIN_PAUSED,
                             VIR_DOMAIN_PAUSED_USER);
        dom->def->id = envId;
        break;
    case VMS_SUSPENDED:
    case VMS_DELETING_STATE:
    case VMS_SUSPENDING_SYNC:
        virDomainObjSetState(dom, VIR_DOMAIN_SHUTOFF,
                             VIR_DOMAIN_SHUTOFF_SAVED);
        dom->def->id = -1;
        break;
    case VMS_STOPPING:
        virDomainObjSetState(dom, VIR_DOMAIN_SHUTDOWN,
                             VIR_DOMAIN_SHUTDOWN_USER);
        dom->def->id = envId;
        break;
    case VMS_SNAPSHOTING:
        virDomainObjSetState(dom, VIR_DOMAIN_PAUSED,
                             VIR_DOMAIN_PAUSED_SNAPSHOT);
        dom->def->id = envId;
        break;
    case VMS_MIGRATING:
        virDomainObjSetState(dom, VIR_DOMAIN_PAUSED,
                             VIR_DOMAIN_PAUSED_MIGRATION);
        dom->def->id = envId;
        break;
    case VMS_SUSPENDING:
        virDomainObjSetState(dom, VIR_DOMAIN_PAUSED,
                             VIR_DOMAIN_PAUSED_SAVE);
        dom->def->id = envId;
        break;
    case VMS_RESTORING:
        virDomainObjSetState(dom, VIR_DOMAIN_RUNNING,
                             VIR_DOMAIN_RUNNING_RESTORED);
        dom->def->id = envId;
        break;
    case VMS_CONTINUING:
        virDomainObjSetState(dom, VIR_DOMAIN_RUNNING,
                             VIR_DOMAIN_RUNNING_UNPAUSED);
        dom->def->id = envId;
        break;
    case VMS_RESUMING:
        virDomainObjSetState(dom, VIR_DOMAIN_RUNNING,
                             VIR_DOMAIN_RUNNING_RESTORED);
        dom->def->id = envId;
        break;
    case VMS_UNKNOWN:
        virDomainObjSetState(dom, VIR_DOMAIN_NOSTATE,
                             VIR_DOMAIN_NOSTATE_UNKNOWN);
        break;
    default:
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unknown domain state: %X"), domainState);
        return -1;
        break;
    }

    return 0;
}

static int
prlsdkConvertCpuInfo(PRL_HANDLE sdkdom,
                     virDomainDefPtr def,
                     parallelsDomObjPtr pdom)
{
    char *buf;
    PRL_UINT32 buflen = 0;
    int hostcpus;
    PRL_UINT32 cpuCount;
    PRL_RESULT pret;
    int ret = -1;

    if ((hostcpus = nodeGetCPUCount()) < 0)
        goto cleanup;

    /* get number of CPUs */
    pret = PrlVmCfg_GetCpuCount(sdkdom, &cpuCount);
    prlsdkCheckRetGoto(pret, cleanup);

    if (cpuCount > hostcpus)
        cpuCount = hostcpus;

    def->vcpus = cpuCount;
    def->maxvcpus = cpuCount;

    pret = PrlVmCfg_GetCpuMask(sdkdom, NULL, &buflen);
    prlsdkCheckRetGoto(pret, cleanup);

    if (VIR_ALLOC_N(buf, buflen) < 0)
        goto cleanup;

    pret = PrlVmCfg_GetCpuMask(sdkdom, buf, &buflen);

    if (strlen(buf) == 0) {
        if (!(pdom->cpumask = virBitmapNew(hostcpus)))
            goto cleanup;
        virBitmapSetAll(pdom->cpumask);
    } else {
        if (virBitmapParse(buf, 0, &pdom->cpumask, hostcpus) < 0)
            goto cleanup;
    }

    ret = 0;
 cleanup:
    VIR_FREE(buf);
    return ret;
}

static int
prlsdkConvertDomainType(PRL_HANDLE sdkdom, virDomainDefPtr def)
{
    PRL_VM_TYPE domainType;
    PRL_RESULT pret;

    pret = PrlVmCfg_GetVmType(sdkdom, &domainType);
    prlsdkCheckRetGoto(pret, error);

    switch (domainType) {
    case PVT_VM:
        if (VIR_STRDUP(def->os.type, "hvm") < 0)
            return -1;
        break;
    case PVT_CT:
        if (VIR_STRDUP(def->os.type, "exe") < 0)
            return -1;
        if (VIR_STRDUP(def->os.init, "/sbin/init") < 0)
            return -1;
        break;
    default:
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unknown domain type: %X"), domainType);
        return -1;
    }

    return 0;

 error:
    return -1;
}

static int
prlsdkConvertCpuMode(PRL_HANDLE sdkdom, virDomainDefPtr def)
{
    PRL_RESULT pret;
    PRL_CPU_MODE cpuMode;

    pret = PrlVmCfg_GetCpuMode(sdkdom, &cpuMode);
    prlsdkCheckRetGoto(pret, error);

    switch (cpuMode) {
    case PCM_CPU_MODE_32:
        def->os.arch = VIR_ARCH_I686;
        break;
    case PCM_CPU_MODE_64:
        def->os.arch = VIR_ARCH_X86_64;
        break;
    default:
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unknown CPU mode: %X"), cpuMode);
        return -1;
    }

    return 0;
 error:
    return -1;
}

/*
 * This function retrieves information about domain.
 * If the domains is already in the domains list
 * privconn->domains, then locked 'olddom' must be
 * provided. If the domains must be added to the list,
 * olddom must be NULL.
 *
 * The function return a pointer to a locked virDomainObj.
 */
static virDomainObjPtr
prlsdkLoadDomain(parallelsConnPtr privconn,
                 PRL_HANDLE sdkdom,
                 virDomainObjPtr olddom)
{
    virDomainObjPtr dom = NULL;
    virDomainDefPtr def = NULL;
    parallelsDomObjPtr pdom = NULL;
    VIRTUAL_MACHINE_STATE domainState;

    PRL_UINT32 buflen = 0;
    PRL_RESULT pret;
    PRL_UINT32 ram;
    PRL_UINT32 envId;
    PRL_VM_AUTOSTART_OPTION autostart;

    virCheckNonNullArgGoto(privconn, error);
    virCheckNonNullArgGoto(sdkdom, error);

    if (VIR_ALLOC(def) < 0)
        goto error;

    if (!olddom) {
        if (VIR_ALLOC(pdom) < 0)
            goto error;
    } else {
        pdom = olddom->privateData;
    }

    def->virtType = VIR_DOMAIN_VIRT_PARALLELS;
    def->id = -1;

    /* we will remove this field in the near future, so let's set it
     * to NULL temporarily */
    pdom->uuid = NULL;

    if (prlsdkGetDomainIds(sdkdom, &def->name, def->uuid) < 0)
        goto error;

    def->onReboot = VIR_DOMAIN_LIFECYCLE_RESTART;
    def->onPoweroff = VIR_DOMAIN_LIFECYCLE_DESTROY;
    def->onCrash = VIR_DOMAIN_LIFECYCLE_CRASH_DESTROY;

    /* get RAM parameters */
    pret = PrlVmCfg_GetRamSize(sdkdom, &ram);
    prlsdkCheckRetGoto(pret, error);
    def->mem.max_balloon = ram << 10; /* RAM size obtained in Mbytes,
                                         convert to Kbytes */
    def->mem.cur_balloon = def->mem.max_balloon;

    if (prlsdkConvertCpuInfo(sdkdom, def, pdom) < 0)
        goto error;

    if (prlsdkConvertCpuMode(sdkdom, def) < 0)
        goto error;

    if (prlsdkConvertDomainType(sdkdom, def) < 0)
        goto error;

    if (prlsdkAddDomainHardware(sdkdom, def) < 0)
        goto error;

    if (prlsdkAddVNCInfo(sdkdom, def) < 0)
        goto error;

    pret = PrlVmCfg_GetEnvId(sdkdom, &envId);
    prlsdkCheckRetGoto(pret, error);
    pdom->id = envId;

    buflen = 0;
    pret = PrlVmCfg_GetHomePath(sdkdom, NULL, &buflen);
    prlsdkCheckRetGoto(pret, error);

    VIR_FREE(pdom->home);
    if (VIR_ALLOC_N(pdom->home, buflen) < 0)
        goto error;

    pret = PrlVmCfg_GetHomePath(sdkdom, pdom->home, &buflen);
    prlsdkCheckRetGoto(pret, error);

    if (olddom) {
        /* assign new virDomainDef without any checks */
        /* we can't use virDomainObjAssignDef, because it checks
         * for state and domain name */
        dom = olddom;
        virDomainDefFree(dom->def);
        virDomainDefFree(dom->newDef);
        dom->def = def;
        dom->newDef = def;
    } else {
        if (!(dom = virDomainObjListAdd(privconn->domains, def,
                                        privconn->xmlopt,
                                        0, NULL)))
        goto error;
    }
    /* dom is locked here */

    dom->privateData = pdom;
    dom->privateDataFreeFunc = prlsdkDomObjFreePrivate;
    dom->persistent = 1;

    if (prlsdkGetDomainState(privconn, sdkdom, &domainState) < 0)
        goto error;

    if (prlsdkConvertDomainState(domainState, envId, dom) < 0)
        goto error;

    pret = PrlVmCfg_GetAutoStart(sdkdom, &autostart);
    prlsdkCheckRetGoto(pret, error);

    switch (autostart) {
    case PAO_VM_START_ON_LOAD:
        dom->autostart = 1;
        break;
    case PAO_VM_START_MANUAL:
        dom->autostart = 0;
        break;
    default:
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unknown autostart mode: %X"), autostart);
        goto error;
    }

    if (!pdom->sdkdom) {
        pret = PrlHandle_AddRef(sdkdom);
        prlsdkCheckRetGoto(pret, error);
        pdom->sdkdom = sdkdom;
    }

    return dom;
 error:
    if (dom && !olddom)
        virDomainObjListRemove(privconn->domains, dom);
    virDomainDefFree(def);
    prlsdkDomObjFreePrivate(pdom);
    return NULL;
}

int
prlsdkLoadDomains(parallelsConnPtr privconn)
{
    PRL_HANDLE job = PRL_INVALID_HANDLE;
    PRL_HANDLE result;
    PRL_HANDLE sdkdom;
    PRL_UINT32 paramsCount;
    PRL_RESULT pret;
    size_t i = 0;
    virDomainObjPtr dom;

    job = PrlSrv_GetVmListEx(privconn->server, PVTF_VM | PVTF_CT);

    if (!(result = getJobResult(job, privconn->jobTimeout)))
        return -1;

    pret = PrlResult_GetParamsCount(result, &paramsCount);
    prlsdkCheckRetGoto(pret, error);

    for (i = 0; i < paramsCount; i++) {
        pret = PrlResult_GetParamByIndex(result, i, &sdkdom);
        if (PRL_FAILED(pret)) {
            logPrlError(pret);
            PrlHandle_Free(sdkdom);
            goto error;
        }

        dom = prlsdkLoadDomain(privconn, sdkdom, NULL);
        PrlHandle_Free(sdkdom);

        if (!dom)
            goto error;
        else
            virObjectUnlock(dom);
    }

    PrlHandle_Free(result);
    return 0;

 error:
    PrlHandle_Free(result);
    PrlHandle_Free(job);
    return -1;
}

virDomainObjPtr
prlsdkAddDomain(parallelsConnPtr privconn, const unsigned char *uuid)
{
    PRL_HANDLE sdkdom = PRL_INVALID_HANDLE;
    virDomainObjPtr dom;

    dom = virDomainObjListFindByUUID(privconn->domains, uuid);
    if (dom) {
        /* domain is already in the list */
        return dom;
    }

    sdkdom = prlsdkSdkDomainLookupByUUID(privconn, uuid);
    if (sdkdom == PRL_INVALID_HANDLE)
        return NULL;

    dom = prlsdkLoadDomain(privconn, sdkdom, NULL);
    PrlHandle_Free(sdkdom);
    return dom;
}

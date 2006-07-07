/*
 * xml.c: XML based interfaces for the libvir library
 *
 * Copyright (C) 2005 Red Hat, Inc.
 *
 * See COPYING.LIB for the License of this software
 *
 * Daniel Veillard <veillard@redhat.com>
 */

#include "libvirt/libvirt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <xs.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include "internal.h"
#include "hash.h"
#include "sexpr.h"
#include "xml.h"

static void
virXMLError(virErrorNumber error, const char *info, int value)
{
    const char *errmsg;

    if (error == VIR_ERR_OK)
        return;

    errmsg = __virErrorMsg(error, info);
    __virRaiseError(NULL, NULL, VIR_FROM_XML, error, VIR_ERR_ERROR,
                    errmsg, info, NULL, value, 0, errmsg, info, value);
}

/**
 * virBufferGrow:
 * @buf:  the buffer
 * @len:  the minimum free size to allocate
 *
 * Grow the available space of an XML buffer.
 *
 * Returns the new available space or -1 in case of error
 */
static int
virBufferGrow(virBufferPtr buf, unsigned int len)
{
    int size;
    char *newbuf;

    if (buf == NULL)
        return (-1);
    if (len + buf->use < buf->size)
        return (0);

    size = buf->use + len + 1000;

    newbuf = (char *) realloc(buf->content, size);
    if (newbuf == NULL) {
        virXMLError(VIR_ERR_NO_MEMORY, "growing buffer", size);
        return (-1);
    }
    buf->content = newbuf;
    buf->size = size;
    return (buf->size - buf->use);
}

/**
 * virBufferAdd:
 * @buf:  the buffer to dump
 * @str:  the string
 * @len:  the number of bytes to add
 *
 * Add a string range to an XML buffer. if len == -1, the length of
 * str is recomputed to the full string.
 *
 * Returns 0 successful, -1 in case of internal or API error.
 */
int
virBufferAdd(virBufferPtr buf, const char *str, int len)
{
    unsigned int needSize;

    if ((str == NULL) || (buf == NULL)) {
        return -1;
    }
    if (len == 0)
        return 0;

    if (len < 0)
        len = strlen(str);

    needSize = buf->use + len + 2;
    if (needSize > buf->size) {
        if (!virBufferGrow(buf, needSize)) {
            return (-1);
        }
    }
    /* XXX: memmove() is 2x slower than memcpy(), do we really need it? */
    memmove(&buf->content[buf->use], str, len);
    buf->use += len;
    buf->content[buf->use] = 0;
    return (0);
}

virBufferPtr
virBufferNew(unsigned int size)
{
    virBufferPtr buf;

    if (!(buf = malloc(sizeof(*buf)))) {
        virXMLError(VIR_ERR_NO_MEMORY, "allocate new buffer", sizeof(*buf));
        return NULL;
    }
    if (size && (buf->content = malloc(size))==NULL) {
        virXMLError(VIR_ERR_NO_MEMORY, "allocate buffer content", size);
        free(buf);
        return NULL;
    }
    buf->size = size;
    buf->use = 0;

    return buf;
}
	
void
virBufferFree(virBufferPtr buf)
{
    if (buf) {
        if (buf->content)
	   free(buf->content);
       	free(buf);
    }
}

/**
 * virBufferVSprintf:
 * @buf:  the buffer to dump
 * @format:  the format
 * @argptr:  the variable list of arguments
 *
 * Do a formatted print to an XML buffer.
 *
 * Returns 0 successful, -1 in case of internal or API error.
 */
int
virBufferVSprintf(virBufferPtr buf, const char *format, ...)
{
    int size, count;
    va_list locarg, argptr;

    if ((format == NULL) || (buf == NULL)) {
        return (-1);
    }
    size = buf->size - buf->use - 1;
    va_start(argptr, format);
    va_copy(locarg, argptr);
    while (((count = vsnprintf(&buf->content[buf->use], size, format,
                               locarg)) < 0) || (count >= size - 1)) {
        buf->content[buf->use] = 0;
        va_end(locarg);
        if (virBufferGrow(buf, 1000) < 0) {
            return (-1);
        }
        size = buf->size - buf->use - 1;
        va_copy(locarg, argptr);
    }
    va_end(locarg);
    buf->use += count;
    buf->content[buf->use] = 0;
    return (0);
}

/**
 * virBufferStrcat:
 * @buf:  the buffer to dump
 * @argptr:  the variable list of strings, the last argument must be NULL
 *
 * Concatenate strings to an XML buffer.
 *
 * Returns 0 successful, -1 in case of internal or API error.
 */
int
virBufferStrcat(virBufferPtr buf, ...)
{
    va_list ap;
    char *str;
    
    va_start(ap, buf);
    
    while ((str = va_arg(ap, char *)) != NULL) {
        unsigned int len = strlen(str);
        unsigned int needSize = buf->use + len + 2;

        if (needSize > buf->size) {
           if (!virBufferGrow(buf, needSize))
              return -1;
	}
        memcpy(&buf->content[buf->use], str, len);
        buf->use += len;
        buf->content[buf->use] = 0;
    }
    va_end(ap);
    return 0;
}

#if 0

/*
 * This block of function are now implemented by a xend poll in
 * xend_internal.c instead of querying the Xen store, code is kept
 * for reference of in case Xend may not be available in the future ...
 */

/**
 * virDomainGetXMLDevice:
 * @domain: a domain object
 * @sub: the xenstore subsection 'vbd', 'vif', ...
 * @dev: the xenstrore internal device number
 * @name: the value's name
 *
 * Extract one information the device used by the domain from xensttore
 *
 * Returns the new string or NULL in case of error
 */
static char *
virDomainGetXMLDeviceInfo(virDomainPtr domain, const char *sub,
                          long dev, const char *name)
{
    char s[256];
    unsigned int len = 0;

    snprintf(s, 255, "/local/domain/0/backend/%s/%d/%ld/%s",
             sub, domain->handle, dev, name);
    s[255] = 0;

    return xs_read(domain->conn->xshandle, 0, &s[0], &len);
}

/**
 * virDomainGetXMLDevice:
 * @domain: a domain object
 * @buf: the output buffer object
 * @dev: the xenstrore internal device number
 *
 * Extract and dump in the buffer informations on the device used by the domain
 *
 * Returns 0 in case of success, -1 in case of failure
 */
static int
virDomainGetXMLDevice(virDomainPtr domain, virBufferPtr buf, long dev)
{
    char *type, *val;

    type = virDomainGetXMLDeviceInfo(domain, "vbd", dev, "type");
    if (type == NULL)
        return (-1);
    if (!strcmp(type, "file")) {
        virBufferVSprintf(buf, "    <disk type='file'>\n");
        val = virDomainGetXMLDeviceInfo(domain, "vbd", dev, "params");
        if (val != NULL) {
            virBufferVSprintf(buf, "      <source file='%s'/>\n", val);
            free(val);
        }
        val = virDomainGetXMLDeviceInfo(domain, "vbd", dev, "dev");
        if (val != NULL) {
            virBufferVSprintf(buf, "      <target dev='%s'/>\n", val);
            free(val);
        }
        val = virDomainGetXMLDeviceInfo(domain, "vbd", dev, "read-only");
        if (val != NULL) {
            virBufferVSprintf(buf, "      <readonly/>\n", val);
            free(val);
        }
        virBufferAdd(buf, "    </disk>\n", 12);
    } else if (!strcmp(type, "phy")) {
        virBufferVSprintf(buf, "    <disk type='device'>\n");
        val = virDomainGetXMLDeviceInfo(domain, "vbd", dev, "params");
        if (val != NULL) {
            virBufferVSprintf(buf, "      <source device='%s'/>\n", val);
            free(val);
        }
        val = virDomainGetXMLDeviceInfo(domain, "vbd", dev, "dev");
        if (val != NULL) {
            virBufferVSprintf(buf, "      <target dev='%s'/>\n", val);
            free(val);
        }
        val = virDomainGetXMLDeviceInfo(domain, "vbd", dev, "read-only");
        if (val != NULL) {
            virBufferVSprintf(buf, "      <readonly/>\n", val);
            free(val);
        }
        virBufferAdd(buf, "    </disk>\n", 12);
    } else {
        TODO fprintf(stderr, "Don't know how to handle device type %s\n",
                     type);
    }
    free(type);

    return (0);
}

/**
 * virDomainGetXMLDevices:
 * @domain: a domain object
 * @buf: the output buffer object
 *
 * Extract the devices used by the domain and dumps then in the buffer
 *
 * Returns 0 in case of success, -1 in case of failure
 */
static int
virDomainGetXMLDevices(virDomainPtr domain, virBufferPtr buf)
{
    int ret = -1;
    unsigned int num, i;
    long id;
    char **list = NULL, *endptr;
    char backend[200];
    virConnectPtr conn;

    if (!VIR_IS_CONNECTED_DOMAIN(domain))
        return (-1);

    conn = domain->conn;

    snprintf(backend, 199, "/local/domain/0/backend/vbd/%d",
             virDomainGetID(domain));
    backend[199] = 0;
    list = xs_directory(conn->xshandle, 0, backend, &num);
    ret = 0;
    if (list == NULL)
        goto done;

    for (i = 0; i < num; i++) {
        id = strtol(list[i], &endptr, 10);
        if ((endptr == list[i]) || (*endptr != 0)) {
            ret = -1;
            goto done;
        }
        virDomainGetXMLDevice(domain, buf, id);
    }

  done:
    if (list != NULL)
        free(list);

    return (ret);
}

/**
 * virDomainGetXMLInterface:
 * @domain: a domain object
 * @buf: the output buffer object
 * @dev: the xenstrore internal device number
 *
 * Extract and dump in the buffer informations on the interface used by
 * the domain
 *
 * Returns 0 in case of success, -1 in case of failure
 */
static int
virDomainGetXMLInterface(virDomainPtr domain, virBufferPtr buf, long dev)
{
    char *type, *val;

    type = virDomainGetXMLDeviceInfo(domain, "vif", dev, "bridge");
    if (type == NULL) {
        virBufferVSprintf(buf, "    <interface type='default'>\n");
        val = virDomainGetXMLDeviceInfo(domain, "vif", dev, "mac");
        if (val != NULL) {
            virBufferVSprintf(buf, "      <mac address='%s'/>\n", val);
            free(val);
        }
        val = virDomainGetXMLDeviceInfo(domain, "vif", dev, "script");
        if (val != NULL) {
            virBufferVSprintf(buf, "      <script path='%s'/>\n", val);
            free(val);
        }
        virBufferAdd(buf, "    </interface>\n", 17);
    } else {
        virBufferVSprintf(buf, "    <interface type='bridge'>\n");
        virBufferVSprintf(buf, "      <source bridge='%s'/>\n", type);
        val = virDomainGetXMLDeviceInfo(domain, "vif", dev, "mac");
        if (val != NULL) {
            virBufferVSprintf(buf, "      <mac address='%s'/>\n", val);
            free(val);
        }
        val = virDomainGetXMLDeviceInfo(domain, "vif", dev, "script");
        if (val != NULL) {
            virBufferVSprintf(buf, "      <script path='%s'/>\n", val);
            free(val);
        }
        virBufferAdd(buf, "    </interface>\n", 17);
    }
    free(type);

    return (0);
}

/**
 * virDomainGetXMLInterfaces:
 * @domain: a domain object
 * @buf: the output buffer object
 *
 * Extract the interfaces used by the domain and dumps then in the buffer
 *
 * Returns 0 in case of success, -1 in case of failure
 */
static int
virDomainGetXMLInterfaces(virDomainPtr domain, virBufferPtr buf)
{
    int ret = -1;
    unsigned int num, i;
    long id;
    char **list = NULL, *endptr;
    char backend[200];
    virConnectPtr conn;

    if (!VIR_IS_CONNECTED_DOMAIN(domain))
        return (-1);

    conn = domain->conn;

    snprintf(backend, 199, "/local/domain/0/backend/vif/%d",
             virDomainGetID(domain));
    backend[199] = 0;
    list = xs_directory(conn->xshandle, 0, backend, &num);
    ret = 0;
    if (list == NULL)
        goto done;

    for (i = 0; i < num; i++) {
        id = strtol(list[i], &endptr, 10);
        if ((endptr == list[i]) || (*endptr != 0)) {
            ret = -1;
            goto done;
        }
        virDomainGetXMLInterface(domain, buf, id);
    }

  done:
    if (list != NULL)
        free(list);

    return (ret);
}




/**
 * virDomainGetXMLBoot:
 * @domain: a domain object
 * @buf: the output buffer object
 *
 * Extract the boot informations used to start that domain
 *
 * Returns 0 in case of success, -1 in case of failure
 */
static int
virDomainGetXMLBoot(virDomainPtr domain, virBufferPtr buf)
{
    char *vm, *str;

    if (!VIR_IS_DOMAIN(domain))
        return (-1);

    vm = virDomainGetVM(domain);
    if (vm == NULL)
        return (-1);

    virBufferAdd(buf, "  <os>\n", 7);
    str = virDomainGetVMInfo(domain, vm, "image/ostype");
    if (str != NULL) {
        virBufferVSprintf(buf, "    <type>%s</type>\n", str);
        free(str);
    }
    str = virDomainGetVMInfo(domain, vm, "image/kernel");
    if (str != NULL) {
        virBufferVSprintf(buf, "    <kernel>%s</kernel>\n", str);
        free(str);
    }
    str = virDomainGetVMInfo(domain, vm, "image/ramdisk");
    if (str != NULL) {
        if (str[0] != 0)
            virBufferVSprintf(buf, "    <initrd>%s</initrd>\n", str);
        free(str);
    }
    str = virDomainGetVMInfo(domain, vm, "image/cmdline");
    if (str != NULL) {
        if (str[0] != 0)
            virBufferVSprintf(buf, "    <cmdline>%s</cmdline>\n", str);
        free(str);
    }
    virBufferAdd(buf, "  </os>\n", 8);

    free(vm);
    return (0);
}

/**
 * virDomainGetXMLDesc:
 * @domain: a domain object
 * @flags: and OR'ed set of extraction flags, not used yet
 *
 * Provide an XML description of the domain. NOTE: this API is subject
 * to changes.
 *
 * Returns a 0 terminated UTF-8 encoded XML instance, or NULL in case of error.
 *         the caller must free() the returned value.
 */
char *
virDomainGetXMLDesc(virDomainPtr domain, int flags)
{
    char *ret = NULL;
    unsigned char uuid[16];
    virBuffer buf;
    virDomainInfo info;

    if (!VIR_IS_DOMAIN(domain))
        return (NULL);
    if (flags != 0)
        return (NULL);
    if (virDomainGetInfo(domain, &info) < 0)
        return (NULL);

    ret = malloc(1000);
    if (ret == NULL)
        return (NULL);
    buf.content = ret;
    buf.size = 1000;
    buf.use = 0;

    virBufferVSprintf(&buf, "<domain type='xen' id='%d'>\n",
                      virDomainGetID(domain));
    virBufferVSprintf(&buf, "  <name>%s</name>\n",
                      virDomainGetName(domain));
    if (virDomainGetUUID(domain, &uuid[0]) == 0) {
    virBufferVSprintf(&buf,
"  <uuid>%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x</uuid>\n",
                      uuid[0], uuid[1], uuid[2], uuid[3],
                      uuid[4], uuid[5], uuid[6], uuid[7],
                      uuid[8], uuid[9], uuid[10], uuid[11],
                      uuid[12], uuid[13], uuid[14], uuid[15]);
    }
    virDomainGetXMLBoot(domain, &buf);
    virBufferVSprintf(&buf, "  <memory>%lu</memory>\n", info.maxMem);
    virBufferVSprintf(&buf, "  <vcpu>%d</vcpu>\n", (int) info.nrVirtCpu);
    virBufferAdd(&buf, "  <devices>\n", 12);
    virDomainGetXMLDevices(domain, &buf);
    virDomainGetXMLInterfaces(domain, &buf);
    virBufferAdd(&buf, "  </devices>\n", 13);
    virBufferAdd(&buf, "</domain>\n", 10);

    buf.content[buf.use] = 0;
    return (ret);
}

#endif

/**
 * virDomainParseXMLOSDesc:
 * @xmldesc: string with the XML description
 * @buf: a buffer for the result S-Expr
 *
 * Parse the OS part of the XML description and add it to the S-Expr in buf
 * This is a temporary interface as the S-Expr interface
 * will be replaced by XML-RPC in the future. However the XML format should
 * stay valid over time.
 *
 * Returns 0 in case of success, -1 in case of error.
 */
static int
virDomainParseXMLOSDesc(xmlNodePtr node, virBufferPtr buf)
{
    xmlNodePtr cur, txt;
    const xmlChar *type = NULL;
    const xmlChar *root = NULL;
    const xmlChar *kernel = NULL;
    const xmlChar *initrd = NULL;
    const xmlChar *cmdline = NULL;

    cur = node->children;
    while (cur != NULL) {
        if (cur->type == XML_ELEMENT_NODE) {
            if ((type == NULL)
                && (xmlStrEqual(cur->name, BAD_CAST "type"))) {
                txt = cur->children;
                if ((txt != NULL) && (txt->type == XML_TEXT_NODE) &&
		    (txt->next == NULL))
                    type = txt->content;
            } else if ((kernel == NULL) &&
                       (xmlStrEqual(cur->name, BAD_CAST "kernel"))) {
                txt = cur->children;
                if ((txt != NULL) && (txt->type == XML_TEXT_NODE) &&
		    (txt->next == NULL))
                    kernel = txt->content;
            } else if ((root == NULL) &&
                       (xmlStrEqual(cur->name, BAD_CAST "root"))) {
                txt = cur->children;
                if ((txt != NULL) && (txt->type == XML_TEXT_NODE) &&
		    (txt->next == NULL))
                    root = txt->content;
            } else if ((initrd == NULL) &&
                       (xmlStrEqual(cur->name, BAD_CAST "initrd"))) {
                txt = cur->children;
                if ((txt != NULL) && (txt->type == XML_TEXT_NODE) &&
		    (txt->next == NULL))
                    initrd = txt->content;
            } else if ((cmdline == NULL) &&
                       (xmlStrEqual(cur->name, BAD_CAST "cmdline"))) {
                txt = cur->children;
                if ((txt != NULL) && (txt->type == XML_TEXT_NODE) &&
		    (txt->next == NULL))
                    cmdline = txt->content;
            }
        }
        cur = cur->next;
    }
    if ((type != NULL) && (!xmlStrEqual(type, BAD_CAST "linux"))) {
        /* VIR_ERR_OS_TYPE */
        virXMLError(VIR_ERR_OS_TYPE, (const char *) type, 0);
        return (-1);
    }
    virBufferAdd(buf, "(image (linux ", 14);
    if (kernel == NULL) {
      	virXMLError(VIR_ERR_NO_KERNEL, NULL, 0);
	return (-1);
    } else {
	virBufferVSprintf(buf, "(kernel '%s')", (const char *) kernel);
    }
    if (initrd != NULL)
        virBufferVSprintf(buf, "(ramdisk '%s')", (const char *) initrd);
    if (root != NULL)
        virBufferVSprintf(buf, "(root '%s')", (const char *) root);
    if (cmdline != NULL)
        virBufferVSprintf(buf, "(args '%s')", (const char *) cmdline);
    virBufferAdd(buf, "))", 2);
    return (0);
}

/**
 * virDomainParseXMLDiskDesc:
 * @xmldesc: string with the XML description
 * @buf: a buffer for the result S-Expr
 *
 * Parse the one disk in the XML description and add it to the S-Expr in buf
 * This is a temporary interface as the S-Expr interface
 * will be replaced by XML-RPC in the future. However the XML format should
 * stay valid over time.
 *
 * Returns 0 in case of success, -1 in case of error.
 */
static int
virDomainParseXMLDiskDesc(xmlNodePtr node, virBufferPtr buf)
{
    xmlNodePtr cur;
    xmlChar *type = NULL;
    xmlChar *source = NULL;
    xmlChar *target = NULL;
    int ro = 0;
    int typ = 0;

    type = xmlGetProp(node, BAD_CAST "type");
    if (type != NULL) {
        if (xmlStrEqual(type, BAD_CAST "file"))
            typ = 0;
        else if (xmlStrEqual(type, BAD_CAST "block"))
            typ = 1;
        xmlFree(type);
    }
    cur = node->children;
    while (cur != NULL) {
        if (cur->type == XML_ELEMENT_NODE) {
            if ((source == NULL) &&
                (xmlStrEqual(cur->name, BAD_CAST "source"))) {

                if (typ == 0)
                    source = xmlGetProp(cur, BAD_CAST "file");
                else
                    source = xmlGetProp(cur, BAD_CAST "dev");
            } else if ((target == NULL) &&
                       (xmlStrEqual(cur->name, BAD_CAST "target"))) {
                target = xmlGetProp(cur, BAD_CAST "dev");
            } else if (xmlStrEqual(cur->name, BAD_CAST "readonly")) {
                ro = 1;
            }
        }
        cur = cur->next;
    }

    if (source == NULL) {
        virXMLError(VIR_ERR_NO_SOURCE, (const char *) target, 0);

        if (target != NULL)
            xmlFree(target);
        return (-1);
    }
    if (target == NULL) {
        virXMLError(VIR_ERR_NO_TARGET, (const char *) source, 0);
        if (source != NULL)
            xmlFree(source);
        return (-1);
    }
    virBufferAdd(buf, "(vbd ", 5);
    if (target[0] == '/')
        virBufferVSprintf(buf, "(dev '%s')", (const char *) target);
    else
        virBufferVSprintf(buf, "(dev '/dev/%s')", (const char *) target);
    if (typ == 0)
        virBufferVSprintf(buf, "(uname 'file:%s')", source);
    else if (typ == 1) {
        if (source[0] == '/')
            virBufferVSprintf(buf, "(uname 'phy:%s')", source);
        else
            virBufferVSprintf(buf, "(uname 'phy:/dev/%s')", source);
    }
    if (ro == 0)
        virBufferVSprintf(buf, "(mode 'w')");
    else if (ro == 1)
        virBufferVSprintf(buf, "(mode 'r')");

    virBufferAdd(buf, ")", 1);
    xmlFree(target);
    xmlFree(source);
    return (0);
}

/**
 * virDomainParseXMLIfDesc:
 * @xmldesc: string with the XML description
 * @buf: a buffer for the result S-Expr
 *
 * Parse the one interface the XML description and add it to the S-Expr in buf
 * This is a temporary interface as the S-Expr interface
 * will be replaced by XML-RPC in the future. However the XML format should
 * stay valid over time.
 *
 * Returns 0 in case of success, -1 in case of error.
 */
static int
virDomainParseXMLIfDesc(xmlNodePtr node, virBufferPtr buf)
{
    xmlNodePtr cur;
    xmlChar *type = NULL;
    xmlChar *source = NULL;
    xmlChar *mac = NULL;
    xmlChar *script = NULL;
    int typ = 0;

    type = xmlGetProp(node, BAD_CAST "type");
    if (type != NULL) {
        if (xmlStrEqual(type, BAD_CAST "bridge"))
            typ = 0;
        else if (xmlStrEqual(type, BAD_CAST "ethernet"))
            typ = 1;
        xmlFree(type);
    }
    cur = node->children;
    while (cur != NULL) {
        if (cur->type == XML_ELEMENT_NODE) {
            if ((source == NULL) &&
                (xmlStrEqual(cur->name, BAD_CAST "source"))) {

                if (typ == 0)
                    source = xmlGetProp(cur, BAD_CAST "bridge");
                else
                    source = xmlGetProp(cur, BAD_CAST "dev");
            } else if ((mac == NULL) &&
                       (xmlStrEqual(cur->name, BAD_CAST "mac"))) {
                mac = xmlGetProp(cur, BAD_CAST "address");
            } else if ((script == NULL) &&
                       (xmlStrEqual(cur->name, BAD_CAST "script"))) {
                script = xmlGetProp(cur, BAD_CAST "path");
            }
        }
        cur = cur->next;
    }

    virBufferAdd(buf, "(vif ", 5);
    if (mac != NULL)
        virBufferVSprintf(buf, "(mac '%s')", (const char *) mac);
    if (source != NULL) {
        if (typ == 0)
            virBufferVSprintf(buf, "(bridge '%s')", (const char *) source);
        else                    /* TODO does that work like that ? */
            virBufferVSprintf(buf, "(dev '%s')", (const char *) source);
    }
    if (script != NULL)
        virBufferVSprintf(buf, "(script '%s')", script);

    virBufferAdd(buf, ")", 1);
    if (mac != NULL)
        xmlFree(mac);
    if (source != NULL)
        xmlFree(source);
    if (script != NULL)
        xmlFree(script);
    return (0);
}

/**
 * virDomainParseXMLDesc:
 * @xmldesc: string with the XML description
 *
 * Parse the XML description and turn it into the xend sexp needed to
 * create the comain. This is a temporary interface as the S-Expr interface
 * will be replaced by XML-RPC in the future. However the XML format should
 * stay valid over time.
 *
 * Returns the 0 terminatedi S-Expr string or NULL in case of error.
 *         the caller must free() the returned value.
 */
char *
virDomainParseXMLDesc(const char *xmldesc, char **name)
{
    xmlDocPtr xml = NULL;
    xmlNodePtr node;
    char *ret = NULL, *nam = NULL;
    virBuffer buf;
    xmlChar *prop;
    xmlXPathObjectPtr obj = NULL;
    xmlXPathContextPtr ctxt = NULL;
    int i, res;
    int bootloader = 0;

    if (name != NULL)
        *name = NULL;
    ret = malloc(1000);
    if (ret == NULL)
        return (NULL);
    buf.content = ret;
    buf.size = 1000;
    buf.use = 0;

    xml = xmlReadDoc((const xmlChar *) xmldesc, "domain.xml", NULL,
                     XML_PARSE_NOENT | XML_PARSE_NONET |
                     XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (xml == NULL) {
        goto error;
    }
    node = xmlDocGetRootElement(xml);
    if ((node == NULL) || (!xmlStrEqual(node->name, BAD_CAST "domain")))
        goto error;

    prop = xmlGetProp(node, BAD_CAST "type");
    if (prop != NULL) {
        if (!xmlStrEqual(prop, BAD_CAST "xen")) {
            xmlFree(prop);
            goto error;
        }
        xmlFree(prop);
    }
    virBufferAdd(&buf, "(vm ", 4);
    ctxt = xmlXPathNewContext(xml);
    if (ctxt == NULL) {
        goto error;
    }
    /*
     * extract soem of the basics, name, memory, cpus ...
     */
    obj = xmlXPathEval(BAD_CAST "string(/domain/name[1])", ctxt);
    if ((obj == NULL) || (obj->type != XPATH_STRING) ||
        (obj->stringval == NULL) || (obj->stringval[0] == 0)) {
        virXMLError(VIR_ERR_NO_NAME, xmldesc, 0);
        goto error;
    }
    virBufferVSprintf(&buf, "(name '%s')", obj->stringval);
    nam = strdup((const char *) obj->stringval);
    if (nam == NULL) {
        virXMLError(VIR_ERR_NO_MEMORY, "copying name", 0);
        goto error;
    }
    xmlXPathFreeObject(obj);

    obj = xmlXPathEval(BAD_CAST "number(/domain/memory[1])", ctxt);
    if ((obj == NULL) || (obj->type != XPATH_NUMBER) ||
        (obj->floatval < 64000)) {
        virBufferVSprintf(&buf, "(memory 128)(maxmem 128)");
    } else {
        unsigned long mem = (obj->floatval / 1024);

        virBufferVSprintf(&buf, "(memory %lu)(maxmem %lu)", mem, mem);
    }
    xmlXPathFreeObject(obj);

    obj = xmlXPathEval(BAD_CAST "number(/domain/vcpu[1])", ctxt);
    if ((obj == NULL) || (obj->type != XPATH_NUMBER) ||
        (obj->floatval <= 0)) {
        virBufferVSprintf(&buf, "(vcpus 1)");
    } else {
        unsigned int cpu = (unsigned int) obj->floatval;

        virBufferVSprintf(&buf, "(vcpus %u)", cpu);
    }
    xmlXPathFreeObject(obj);

    obj = xmlXPathEval(BAD_CAST "string(/domain/bootloader[1])", ctxt);
    if ((obj != NULL) && (obj->type == XPATH_STRING) &&
        (obj->stringval != NULL) && (obj->stringval[0] != 0)) {
	virBufferVSprintf(&buf, "(bootloader '%s')", obj->stringval);
	bootloader = 1;
    }
    xmlXPathFreeObject(obj);

    obj = xmlXPathEval(BAD_CAST "string(/domain/on_poweroff[1])", ctxt);
    if ((obj != NULL) && (obj->type == XPATH_STRING) &&
        (obj->stringval != NULL) && (obj->stringval[0] != 0)) {
	virBufferVSprintf(&buf, "(on_poweroff '%s')", obj->stringval);
    }
    xmlXPathFreeObject(obj);

    obj = xmlXPathEval(BAD_CAST "string(/domain/on_reboot[1])", ctxt);
    if ((obj != NULL) && (obj->type == XPATH_STRING) &&
        (obj->stringval != NULL) && (obj->stringval[0] != 0)) {
	virBufferVSprintf(&buf, "(on_reboot '%s')", obj->stringval);
    }
    xmlXPathFreeObject(obj);

    obj = xmlXPathEval(BAD_CAST "string(/domain/on_crash[1])", ctxt);
    if ((obj != NULL) && (obj->type == XPATH_STRING) &&
        (obj->stringval != NULL) && (obj->stringval[0] != 0)) {
	virBufferVSprintf(&buf, "(on_crash '%s')", obj->stringval);
    }
    xmlXPathFreeObject(obj);

    /* analyze of the os description */
    obj = xmlXPathEval(BAD_CAST "/domain/os[1]", ctxt);
    if ((obj != NULL) && (obj->type == XPATH_NODESET) &&
        (obj->nodesetval != NULL) && (obj->nodesetval->nodeNr == 1)) {
	res = virDomainParseXMLOSDesc(obj->nodesetval->nodeTab[0], &buf);
	if (res != 0) {
	    goto error;
	}
    } else if (bootloader == 0) {
	virXMLError(VIR_ERR_NO_OS, nam, 0);
	goto error;
    }
    xmlXPathFreeObject(obj);

    /* analyze of the devices */
    obj = xmlXPathEval(BAD_CAST "/domain/devices/disk", ctxt);
    if ((obj != NULL) && (obj->type == XPATH_NODESET) &&
        (obj->nodesetval != NULL) && (obj->nodesetval->nodeNr >= 0)) {
	for (i = 0; i < obj->nodesetval->nodeNr; i++) {
	    virBufferAdd(&buf, "(device ", 8);
	    res = virDomainParseXMLDiskDesc(obj->nodesetval->nodeTab[i], &buf);
	    if (res != 0) {
		goto error;
	    }
	    virBufferAdd(&buf, ")", 1);
	}
    }
    xmlXPathFreeObject(obj);

    obj = xmlXPathEval(BAD_CAST "/domain/devices/interface", ctxt);
    if ((obj != NULL) && (obj->type == XPATH_NODESET) &&
        (obj->nodesetval != NULL) && (obj->nodesetval->nodeNr >= 0)) {
        for (i = 0; i < obj->nodesetval->nodeNr; i++) {
            virBufferAdd(&buf, "(device ", 8);
            res =
                virDomainParseXMLIfDesc(obj->nodesetval->nodeTab[i], &buf);
            if (res != 0) {
                goto error;
            }
            virBufferAdd(&buf, ")", 1);
        }
    }
    xmlXPathFreeObject(obj);


    virBufferAdd(&buf, ")", 1); /* closes (vm */
    buf.content[buf.use] = 0;

    xmlXPathFreeContext(ctxt);
    xmlFreeDoc(xml);

    if (name != NULL)
        *name = nam;

    return (ret);

  error:
    if (nam != NULL)
        free(nam);
    if (name != NULL)
        *name = NULL;
    if (obj != NULL)
        xmlXPathFreeObject(obj);
    if (ctxt != NULL)
        xmlXPathFreeContext(ctxt);
    if (xml != NULL)
        xmlFreeDoc(xml);
    if (ret != NULL)
        free(ret);
    return (NULL);
}

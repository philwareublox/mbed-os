/* mbed Microcontroller Library
 * Copyright (c) 2016 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "drivers/FileHandle.h"
#include "events/EventQueue.h"
#if defined(FEATURE_COMMON_PAL)
#include "mbed_trace.h"
#define TRACE_GROUP "LPPP"
#else
#define tr_debug(...) (void(0)) //dummies if feature common pal is not added
#define tr_info(...)  (void(0)) //dummies if feature common pal is not added
#define tr_error(...) (void(0)) //dummies if feature common pal is not added
#endif //defined(FEATURE_COMMON_PAL)
#include "rtos/Thread.h"
#include "lwip/tcpip.h"
#include "lwip/tcp.h"
#include "lwip/ip.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
extern "C" { // "pppos.h" is missing extern C
#include "netif/ppp/pppapi.h"
}

#include "lwip_stack.h"

static void (*notify_ppp_link_status_cb)(int) = 0;

namespace mbed {

using rtos::Thread;
using events::EventQueue;

#if LWIP_PPP_API

static EventQueue *event_queue;
static Thread *event_thread;
static volatile bool event_queued;

// Just one interface for now
static ppp_pcb *my_ppp_pcb;

static EventQueue *prepare_event_queue()
{
    if (event_queue) {
        return event_queue;
    }

    // Should be trying to get a global shared event queue here!
    // Shouldn't have to be making a private thread!

    // Only need to queue 1 event. new blows on failure.
    event_queue = new EventQueue(2 * EVENTS_EVENT_SIZE, NULL);
    event_thread = new Thread(osPriorityNormal, 700);

    if (event_thread->start(callback(event_queue, &EventQueue::dispatch_forever)) != osOK) {
        delete event_thread;
        delete event_queue;
        return NULL;
    }

    return event_queue;
}

static u32_t ppp_output(ppp_pcb *pcb, u8_t *data, u32_t len, void *ctx)
{
    FileHandle *stream = static_cast<FileHandle *>(ctx);
    PollFH fhs;
    fhs.fh = stream;
    fhs.events = MBED_POLLOUT;

    // LWIP expects us to block on write
    // File handle will be in non-blocking mode, because of read events.
    // Therefore must use poll to achieve the necessary block for writing.

    uint32_t written = 0;
    while (written < len) {
        // Block forever until we're selected - don't care about reason we wake;
        // return from write should tell us what's up.
        mbed_poll(&fhs, 1, -1);
        // This write will be non-blocking, but blocking would be fine.
        ssize_t ret = stream->write(data, len);
        if (ret < 0) {
            break;
        }
        written += ret;
    }

    tr_debug("> %ld\n", (long) written);

    return written;
}

static void ppp_link_status(ppp_pcb *pcb, int err_code, void *ctx)
{
    switch(err_code) {
        case PPPERR_NONE: {
            tr_info("status_cb: Connected");
#if PPP_IPV4_SUPPORT
            tr_debug("   our_ipaddr  = %s", ipaddr_ntoa(&ppp_netif(pcb)->ip_addr));
            tr_debug("   his_ipaddr  = %s", ipaddr_ntoa(&ppp_netif(pcb)->gw));
            tr_debug("   netmask     = %s", ipaddr_ntoa(&ppp_netif(pcb)->netmask));
#if LWIP_DNS
            const ip_addr_t *ns;
            ns = dns_getserver(0);
            if (ns) {
                tr_debug("   dns1        = %s", ipaddr_ntoa(ns));
            }
            ns = dns_getserver(1);
            if (ns) {
                tr_debug("   dns2        = %s", ipaddr_ntoa(ns));
            }
#endif /* LWIP_DNS */
#endif /* PPP_IPV4_SUPPORT */
#if PPP_IPV6_SUPPORT
            tr_debug("   our6_ipaddr = %s", ip6addr_ntoa(netif_ip6_addr(ppp_netif(pcb), 0)));
#endif /* PPP_IPV6_SUPPORT */
            break;
        }
        case PPPERR_PARAM: {
            tr_info("status_cb: Invalid parameter");
            break;
        }
        case PPPERR_OPEN: {
            tr_info("status_cb: Unable to open PPP session");
            break;
        }
        case PPPERR_DEVICE: {
            tr_info("status_cb: Invalid I/O device for PPP");
            break;
        }
        case PPPERR_ALLOC: {
            tr_info("status_cb: Unable to allocate resources");
            break;
        }
        case PPPERR_USER: {
            tr_info("status_cb: User interrupt");
            break;
        }
        case PPPERR_CONNECT: {
            tr_info("status_cb: Connection lost");
            break;
        }
        case PPPERR_AUTHFAIL: {
            tr_info("status_cb: Failed authentication challenge");
            break;
        }
        case PPPERR_PROTOCOL: {
            tr_info("status_cb: Failed to meet protocol");
            break;
        }
        case PPPERR_PEERDEAD: {
            tr_info("status_cb: Connection timeout");
            break;
        }
        case PPPERR_IDLETIMEOUT: {
            tr_info("status_cb: Idle Timeout");
            break;
        }
        case PPPERR_CONNECTTIME: {
            tr_info("status_cb: Max connect time reached");
            break;
        }
        case PPPERR_LOOPBACK: {
            tr_info("status_cb: Loopback detected");
            break;
        }
        default: {
            tr_info("status_cb: Unknown error code %d", err_code);
            break;
        }
    }

    notify_ppp_link_status_cb(err_code);

#if 0
    /*
     * This should be in the switch case, this is put outside of the switch
     * case for example readability.
     */

    if (err_code == PPPERR_NONE) {
        return;
    }

    /* ppp_close() was previously called, don't reconnect */
    if (err_code == PPPERR_USER) {
        /* ppp_free(); -- can be called here */
        return;
    }

    /*
     * Try to reconnect in 30 seconds, if you need a modem chatscript you have
     * to do a much better signaling here ;-)
     */
    ppp_connect(pcb, 30);
    /* OR ppp_listen(pcb); */

#endif
}

static void flush(FileHandle *stream)
{
    char buffer[8];
    for (;;) {
        ssize_t ret = stream->read(buffer, sizeof buffer);
        if (ret <= 0) {
            break;
        }
    }
}

static void ppp_input(FileHandle *stream)
{
    // Allow new events from now, avoiding potential races around the read
    event_queued = false;

    // Infinite loop, but we assume that we can read faster than the
    // serial, so we will fairly rapidly hit WOULDBLOCK.
    for (;;) {
#if PPP_INPROC_IRQ_SAFE
        u8_t buffer[16];
        ssize_t len = stream->read(buffer, sizeof buffer);
        if (len <= 0) {
            // error - (XXX should do something if not WOULDBLOCK)
            break;
        }
        pppos_input(my_ppp_pcb, buffer, len);
#else
        // Code borrowed from internals of pppos_input_tcpip(), to avoid
        // need for an extra buffer.
        struct pbuf *p = pbuf_alloc(PBUF_RAW, PBUF_POOL_BUFSIZE, PBUF_POOL);
        if (!p) {
            // Out of memory, so leave routine for now.
            // Flush to make sure we will be woken again
            flush(stream);
            break;
        }

        ssize_t len = stream->read(p->payload, p->len);
        if (len <= 0) {
            // error - (XXX should do something if not WOULDBLOCK)
            pbuf_free(p);
            break;
        }

        //printf("< %ld\n", (long) len);

        p->len = p->tot_len = len;

        err_t err = tcpip_inpkt(p, ppp_netif(my_ppp_pcb), pppos_input_sys);
        if (err != ERR_OK) {
            pbuf_free(p);
            // If it doesn't accept it, I don't care - keep going
        }
#endif
    }
}

static void stream_cb(FileHandle *stream, short events) {
    if (!event_queued) {
        event_queued = true;
        if (event_queue->call(callback(ppp_input, stream)) == 0) {
            event_queued = false;
        }
    }
}

err_t ppp_lwip_if_init(struct netif *netif, FileHandle *stream)
{
    if (!prepare_event_queue()) {
        return ERR_MEM;
    }

    if (!my_ppp_pcb) {
        my_ppp_pcb = pppos_create(netif,
                               ppp_output, ppp_link_status, stream);
    }

    if (!my_ppp_pcb) {
        return ERR_IF;
    }

#if LWIP_IPV6_AUTOCONFIG
    /* IPv6 address autoconfiguration not enabled by default */
    netif->ip6_autoconfig_enabled = 1;
#endif /* LWIP_IPV6_AUTOCONFIG */

#if LWIP_IPV4
    ppp_set_usepeerdns(my_ppp_pcb, true);
#endif

    ppp_set_default(my_ppp_pcb);

    stream->attach(callback(stream_cb, stream));
    stream->set_blocking(false);

    return ppp_connect(my_ppp_pcb, 0);
}

static struct netif my_ppp_netif;

nsapi_error_t mbed_ppp_init(FileHandle *stream, void (*link_status)(int))
{
    notify_ppp_link_status_cb = link_status;
    mbed_lwip_init();
    ppp_lwip_if_init(&my_ppp_netif, stream);

    return NSAPI_ERROR_OK;
}

NetworkStack *mbed_ppp_get_stack()
{
    return nsapi_create_stack(&lwip_stack);
}

#endif /* LWIP_PPP_API */

} // namespace mbed

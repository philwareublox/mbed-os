/* Copyright (c) 2017 ublox Limited
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

#include "UbloxCellularInterfaceGenericAtData.h"
#include "APN_db.h"
#if defined(FEATURE_COMMON_PAL)
#include "mbed_trace.h"
#define TRACE_GROUP "UCAD"
#else
#define tr_debug(...) (void(0)) // dummies if feature common pal is not added
#define tr_info(...)  (void(0)) // dummies if feature common pal is not added
#define tr_error(...) (void(0)) // dummies if feature common pal is not added
#endif

/**********************************************************************
 * MACROS
 **********************************************************************/

/** The maximum number of bytes to write to a socket at a time.
 */
#define MAX_WRITE_SIZE 1024

/** The maximum number of bytes that can be read from a socket at a time.
 */
#define MAX_READ_SIZE 1024

/**********************************************************************
 * STATIC VARIABLES
 **********************************************************************/

// A callback function that will be called if the connection goes down.
static void (*callback_fptr)(nsapi_error_t);

/**********************************************************************
 * PRIVATE METHODS
 **********************************************************************/

// Find or create a socket from the list.
UbloxCellularInterfaceGenericAtData::SockCtrl * UbloxCellularInterfaceGenericAtData::findSocket(int modemHandle)
{
    UbloxCellularInterfaceGenericAtData::SockCtrl *socket = NULL;

    for (unsigned int x = 0; (socket == NULL) && (x < sizeof(_sockets) / sizeof(_sockets[0])); x++) {
        if (_sockets[x].modemHandle == modemHandle) {
            socket = &(_sockets[x]);
        }
    }

    return socket;
}

// Clear out the storage for a socket
void UbloxCellularInterfaceGenericAtData::clearSocket(UbloxCellularInterfaceGenericAtData::SockCtrl * socket)
{
    if (socket != NULL) {
        socket->tcpConnected        = false;
        socket->modemHandle         = SOCKET_UNUSED;
        _timeout                    = TIMEOUT_BLOCKING;
        socket->pending             = 0;
    }
}

// Convert nsapi_security_t to the modem security numbers
int UbloxCellularInterfaceGenericAtData::nsapiSecurityToModemSecurity(nsapi_security_t nsapiSecurity)
{
    int modemSecurity = 3;

    switch (nsapiSecurity)
    {
        case NSAPI_SECURITY_NONE:
            modemSecurity = 0;
            break;
        case NSAPI_SECURITY_PAP:
            modemSecurity = 1;
            break;
        case NSAPI_SECURITY_CHAP:
            modemSecurity = 2;
            break;
        case NSAPI_SECURITY_UNKNOWN:
            modemSecurity = 3;
            break;
        default:
            modemSecurity = 3;
            break;
    }

    return modemSecurity;
}


// Callback for Socket Read URC.
void UbloxCellularInterfaceGenericAtData::UUSORD_URC()
{
    int a;
    int b;
    SockCtrl *socket;

    // +UUSORD: <socket>,<length>
    if (_at->recv(": %d,%d\n", &a, &b)) {
        socket = findSocket(a);
        tr_debug("Socket 0x%08x: modem handle %d has %d bytes pending",
                 (unsigned int) socket, a, b);
        if (socket != NULL) {
            socket->pending += b;
        }
    }
}

// Callback for Socket Read From URC.
void UbloxCellularInterfaceGenericAtData::UUSORF_URC()
{
    int a;
    int b;
    SockCtrl *socket;

    // +UUSORF: <socket>,<length>
    if (_at->recv(": %d,%d\n", &a, &b)) {
        socket = findSocket(a);
        tr_debug("Socket 0x%08x: modem handle %d has %d bytes pending",
                 (unsigned int) socket, a, b);
        if (socket != NULL) {
            socket->pending += b;
        }
    }
}

// Callback for Socket Close URC.
void UbloxCellularInterfaceGenericAtData::UUSOCL_URC()
{
    int a;
    SockCtrl *socket;

    // +UUSOCL: <socket>
    if (_at->recv(": %d\n", &a)) {
        socket = findSocket(a);
        tr_debug("Socket 0x%08x: handle %d closed by remote host",
                 (unsigned int) socket, a);
        clearSocket(socket);
    }
}

// Callback for UUPSDD.
void UbloxCellularInterfaceGenericAtData::UUPSDD_URC()
{
    int a;
    SockCtrl *socket;

    // +UUPSDD: <socket>
    if (_at->recv(": %d\n", &a)) {
        socket = findSocket(a);
        tr_debug("Socket 0x%08x: handle %d connection lost",
                 (unsigned int) socket, a);
        clearSocket(socket);
        if (callback_fptr != NULL) {
            callback_fptr(NSAPI_ERROR_CONNECTION_LOST);
        }
    }
}

/**********************************************************************
 * PROTECTED METHODS: GENERAL
 **********************************************************************/

// Get the next set of credentials, based on IMSI.
void UbloxCellularInterfaceGenericAtData::get_next_credentials(const char * config)
{
    if (config) {
        _apn    = _APN_GET(config);
        _uname  = _APN_GET(config);
        _pwd    = _APN_GET(config);
    }

    _apn    = _apn     ?  _apn    : "";
    _uname  = _uname   ?  _uname  : "";
    _pwd    = _pwd     ?  _pwd    : "";
}

// Active a connection profile on board the modem.
// Note: the AT interface should be locked before this is called.
bool UbloxCellularInterfaceGenericAtData::activateProfile(const char* apn,
                                                          const char* username,
                                                          const char* password,
                                                          nsapi_security_t auth)
{
    bool activated = false;
    bool success = false;
    SocketAddress address;

    // Set up the APN
    if (*apn) {
        success = _at->send("AT+UPSD=" PROFILE ",1,\"%s\"", apn) && _at->recv("OK");
    }
    if (success && *username) {
        success = _at->send("AT+UPSD=" PROFILE ",2,\"%s\"", username) && _at->recv("OK");
    }
    if (success && *password) {
        success = _at->send("AT+UPSD=" PROFILE ",3,\"%s\"", password) && _at->recv("OK");
    }

    if (success) {
        // Set up dynamic IP address assignment.
        success = _at->send("AT+UPSD=" PROFILE ",7,\"0.0.0.0\"") && _at->recv("OK");
        // Set up the authentication protocol
        // 0 = none
        // 1 = PAP (Password Authentication Protocol)
        // 2 = CHAP (Challenge Handshake Authentication Protocol)
        for (int protocol = nsapiSecurityToModemSecurity(NSAPI_SECURITY_NONE);
             success && (protocol <= nsapiSecurityToModemSecurity(NSAPI_SECURITY_CHAP)); protocol++) {
            if ((_auth == nsapiSecurityToModemSecurity(NSAPI_SECURITY_UNKNOWN)) || (_auth == protocol)) {
                if (_at->send("AT+UPSD=" PROFILE ",6,%d", protocol) && _at->recv("OK")) {
                    // Activate, waiting 180 seconds for the connection to be made
                    if (_at->send("AT+UPSDA=" PROFILE ",3") && _at->recv("OK")) {
                        _at->set_timeout(1000);
                        for (int retries = 0; !activated && (retries < 180); retries++) {
                            if (get_ip_address() != NULL) {
                                activated = true;
                            } else {
                                wait_ms(1000);
                            }
                        }
                        _at->set_timeout(AT_PARSER_TIMEOUT);
                    }
                }
            }
        }
    }

    return activated;
}

// Activate a profile by reusing an external PDP context.
// Note: the AT interface should be locked before this is called.
bool UbloxCellularInterfaceGenericAtData::activateProfileReuseExternal(void)
{
    bool success = false;
    int cid = -1;
    char ip[NSAPI_IP_SIZE];
    SocketAddress address;
    int t;

    //+CGDCONT: <cid>,"IP","<apn name>","<ip adr>",0,0,0,0,0,0
    if (_at->send("AT+CGDCONT?")) {
        if (_at->recv("+CGDCONT: %d,\"IP\",\"%*[^\"]\",\"%" stringify(NSAPI_IP_SIZE) "[^\"]\",%*d,%*d,%*d,%*d,%*d,%*d",
                      &t, ip) &&
            _at->recv("OK")) {
            // Check if the IP address is valid
            if (address.set_ip_address(ip)) {
                cid = t;
            }
        }
    }

    // If a context has been found, use it
    if ((cid != -1) && (_at->send("AT+UPSD=" PROFILE ",100,%d", cid) && _at->recv("OK"))) {
        // Activate, waiting 30 seconds for the connection to be made
        _at->set_timeout(30000);
        success = _at->send("AT+UPSDA=" PROFILE ",3") && _at->recv("OK");
        _at->set_timeout(AT_PARSER_TIMEOUT);
    }

    return success;
}

// Activate a profile by context ID.
// Note: the AT interface should be locked before this is called.
bool UbloxCellularInterfaceGenericAtData::activateProfileByCid(int cid,
                                                               const char* apn,
                                                               const char* username,
                                                               const char* password,
                                                               nsapi_security_t auth)
{
    bool success = false;

    if (_at->send("AT+CGDCONT=%d,\"IP\",\"%s\"", cid, apn) && _at->recv("OK") &&
        _at->send("AT+UAUTHREQ=%d,%d,\"%s\",\"%s\"", cid, nsapiSecurityToModemSecurity(auth),
                  username, password) && _at->recv("OK") &&
        _at->send("AT+UPSD=" PROFILE ",100,%d", cid) && _at->recv("OK")) {

        // Wait 30 seconds for the connection to be made
        _at->set_timeout(30000);
        // Activate the protocol
        success = _at->send("AT+UPSDA=" PROFILE ",3") && _at->recv("OK");
        _at->set_timeout(AT_PARSER_TIMEOUT);
    }

    return success;
}

// Connect the on board IP stack of the modem.
bool UbloxCellularInterfaceGenericAtData::connectModemStack()
{
    bool success = false;
    int active = 0;
    const char * config = NULL;
    LOCK();

    // Check the profile
    if (_at->send("AT+UPSND=" PROFILE ",8") && _at->recv("+UPSND: %*d,%*d,%d\n", &active) &&
        _at->recv("OK")) {
        if (active == 1) {
            // Disconnect the profile already if it is connected
            if (_at->send("AT+UPSDA=" PROFILE ",4") && _at->recv("OK")) {
                active = 0;
            }
        }
    }

    // Use the profile
    if (active == 0) {
        // If the caller hasn't entered an APN, try to find it
        if (_apn == NULL) {
            config = apnconfig(_dev_info->imsi);
        }

        // Attempt to connect
        do {
            // Set up APN and IP protocol for PDP context
            get_next_credentials(config);
            _auth = (*_uname && *_pwd) ? _auth : NSAPI_SECURITY_NONE;
            if ((_dev_info->dev != DEV_TOBY_L2) && (_dev_info->dev != DEV_MPCI_L2)) {
                success = activateProfile(_apn, _uname, _pwd, _auth);
            } else {
                success = activateProfileReuseExternal();
                if (success) {
                    tr_debug("Reusing external context");
                } else {
                    success = activateProfileByCid(1, _apn, _uname, _pwd, _auth);
                }
            }
        } while (!success && config && *config);
    }

    UNLOCK();
    return success;
}

// Disconnect the on board IP stack of the modem.
bool UbloxCellularInterfaceGenericAtData::disconnectModemStack()
{
    bool success = false;
    LOCK();

    if (get_ip_address() != NULL) {
        if (_at->send("AT+UPSDA=" PROFILE ",4") && _at->recv("OK")) {
            success = true;
            if (callback_fptr != NULL) {
                callback_fptr(NSAPI_ERROR_CONNECTION_LOST);
            }
        }
    }

    UNLOCK();
    return success;
}

/**********************************************************************
 * PROTECTED METHODS: NETWORK INTERFACE and SOCKETS
 **********************************************************************/

// Gain access to us.
NetworkStack *UbloxCellularInterfaceGenericAtData::get_stack()
{
    return this;
}

// Create a socket.
nsapi_error_t UbloxCellularInterfaceGenericAtData::socket_open(nsapi_socket_t *handle,
                                                               nsapi_protocol_t proto)
{
    nsapi_error_t nsapiError = NSAPI_ERROR_DEVICE_ERROR;
    bool success = false;
    int modemHandle;
    SockCtrl *socket;
    LOCK();

    // Find a free socket
    socket = findSocket();
    tr_debug("socket_open(%d)", proto);

    if (socket != NULL) {
        if (proto == NSAPI_UDP) {
            success = _at->send("AT+USOCR=17");
        } else if (proto == NSAPI_TCP) {
            success = _at->send("AT+USOCR=6");
        } else  {
            nsapiError = NSAPI_ERROR_UNSUPPORTED;
        }

        if (success) {
            nsapiError = NSAPI_ERROR_NO_SOCKET;
            if (_at->recv("+USOCR: %d\n", &modemHandle) && (modemHandle != SOCKET_UNUSED) &&
                _at->recv("OK")) {
                tr_debug("Socket 0x%8x: handle %d was created", (unsigned int) socket, modemHandle);
                clearSocket(socket);
                socket->modemHandle         = modemHandle;
                *handle = (nsapi_socket_t) socket;
                nsapiError = NSAPI_ERROR_OK;
            }
        }
    } else {
        nsapiError = NSAPI_ERROR_NO_MEMORY;
    }

    UNLOCK();
    return nsapiError;
}

// Close a socket.
nsapi_error_t UbloxCellularInterfaceGenericAtData::socket_close(nsapi_socket_t handle)
{
    nsapi_error_t nsapiError = NSAPI_ERROR_DEVICE_ERROR;
    SockCtrl *socket = (SockCtrl *) handle;

    tr_debug("socket_close(0x%08x)", (unsigned int) handle);

    if (_at->send("AT+USOCL=%d", socket->modemHandle) &&
        _at->recv("OK")) {
        clearSocket(socket);
        nsapiError = NSAPI_ERROR_OK;
    }

    return nsapiError;
}

// Bind a local port to a socket.
nsapi_error_t UbloxCellularInterfaceGenericAtData::socket_bind(nsapi_socket_t handle,
                                                               const SocketAddress &address)
{
    nsapi_error_t nsapiError = NSAPI_ERROR_NO_SOCKET;
    int proto;
    int modemHandle;
    SockCtrl savedSocket;
    SockCtrl *socket = (SockCtrl *) handle;

    tr_debug("socket_bind(0x%08x, :%d)", (unsigned int) handle, address.get_port());

    // Query the socket type
    if (_at->send("AT+USOCTL=%d,0", socket->modemHandle) &&
        _at->recv("+USOCTL: %*d,0,%d\n", &proto) &&
        _at->recv("OK")) {
        savedSocket = *socket;
        nsapiError = NSAPI_ERROR_DEVICE_ERROR;
        // Now close the socket and re-open it with the binding given
        if (_at->send("AT+USOCL=%d", socket->modemHandle) &&
            _at->recv("OK")) {
            clearSocket(socket);
            nsapiError = NSAPI_ERROR_CONNECTION_LOST;
            if (_at->send("AT+USOCR=%d,%d", proto, address.get_port()) &&
                _at->recv("+USOCR: %d\n", &modemHandle) && (modemHandle != SOCKET_UNUSED) &&
                _at->recv("OK")) {
                *socket = savedSocket;
                nsapiError = NSAPI_ERROR_OK;
            }
        }
    }

    return nsapiError;
}

// Connect to a socket
nsapi_error_t UbloxCellularInterfaceGenericAtData::socket_connect(nsapi_socket_t handle,
                                                                  const SocketAddress &address)
{
    nsapi_error_t nsapiError = NSAPI_ERROR_DEVICE_ERROR;
    SockCtrl *socket = (SockCtrl *) handle;
    LOCK();

    tr_debug("socket_connect(0x%08x, %s(:%d))", (unsigned int) handle,
             address.get_ip_address(), address.get_port());
    if (!socket->tcpConnected) {
        if (_at->send("AT+USOCO=%d,\"%s\",%d", socket->modemHandle,
                      address.get_ip_address(), address.get_port()) &&
            _at->recv("OK")) {
            socket->tcpConnected = true;
            nsapiError = NSAPI_ERROR_OK;
        }
    }

    UNLOCK();
    return nsapiError;
}

// Send to a socket.
nsapi_size_or_error_t UbloxCellularInterfaceGenericAtData::socket_send(nsapi_socket_t handle,
                                                                       const void *data,
                                                                       nsapi_size_t size)
{
    nsapi_size_or_error_t nsapiErrorSize = NSAPI_ERROR_DEVICE_ERROR;
    bool success = true;
    const char *buf = (const char *) data;
    nsapi_size_t blk = MAX_WRITE_SIZE;
    nsapi_size_t cnt = size;
    SockCtrl *socket = (SockCtrl *) handle;

    tr_debug("socket_send(0x%08x, 0x%08x, %d)", (unsigned int) handle, (unsigned int) data, size);

    while ((cnt > 0) && success) {
        if (cnt < blk) {
            blk = cnt;
        }
        LOCK();

        if (socket->tcpConnected) {
            if (_at->send("AT+USOWR=%d,%d", socket->modemHandle, blk) && _at->recv("@")) {
                wait_ms(50);
                if ((_at->write(buf, blk) >= (int) blk) &&
                     _at->recv("OK")) {
                } else {
                    success = false;
                }
            } else {
                success = false;
            }
        } else {
            nsapiErrorSize = NSAPI_ERROR_NO_CONNECTION;
            success = false;
        }

        UNLOCK();
        buf += blk;
        cnt -= blk;
    }

    if (success) {
        nsapiErrorSize = size - cnt;
    }

    return nsapiErrorSize;
}

// Send to an IP address.
nsapi_size_or_error_t UbloxCellularInterfaceGenericAtData::socket_sendto(nsapi_socket_t handle,
                                                                         const SocketAddress &address,
                                                                         const void *data,
                                                                         nsapi_size_t size)
{
    nsapi_size_or_error_t nsapiErrorSize = NSAPI_ERROR_DEVICE_ERROR;
    bool success = true;
    const char *buf = (const char *) data;
    SockCtrl *socket = (SockCtrl *) handle;

    tr_debug("socket_sendto(0x%8x, %s(:%d), 0x%08x, %d)", (unsigned int) handle,
             address.get_ip_address(), address.get_port(), (unsigned int) data, size);
    LOCK();

    if (_at->send("AT+USOST=%d,\"%s\",%d,%d", socket->modemHandle,
                   address.get_ip_address(), address.get_port(), size) &&
        _at->recv("@")) {
        wait_ms(50);
        success = (_at->write(buf, size) >= (int) size) && _at->recv("OK");
    }

    UNLOCK();

    if (success) {
        nsapiErrorSize = size;
    }

    return nsapiErrorSize;
}

// Receive from a socket.
nsapi_size_or_error_t UbloxCellularInterfaceGenericAtData::socket_recv(nsapi_socket_t handle,
                                                                       void *data,
                                                                       nsapi_size_t size)
{
    nsapi_size_or_error_t nsapiErrorSize = NSAPI_ERROR_DEVICE_ERROR;
    bool success = true;
    char *buf = (char *) data;
    nsapi_size_t readBlk = MAX_READ_SIZE;
    nsapi_size_t cnt = 0;
    char * tmpBuf = NULL;
    unsigned int sz, sk, readSize;
    Timer timer;
    SockCtrl *socket = (SockCtrl *) handle;

    tr_debug("socket_recv(0x%08x, 0x%08x, %d)", (unsigned int) handle, (unsigned int) data, size);

    _at->set_timeout(1000);
    timer.start();
    tmpBuf = (char *) malloc(MAX_READ_SIZE + 2); // +2 for leading and trailing quotes

    if (tmpBuf != NULL) {
        while (success && (size > 0)) {
            if (size < readBlk) {
                readBlk = size;
            }
            LOCK();

            if (socket->tcpConnected) {
                if (socket->pending <= readBlk) {
                    readBlk = socket->pending;
                }
                if (readBlk > 0) {
                    if (_at->send("AT+USORD=%d,%d", socket->modemHandle, readBlk) &&
                        _at->recv("+USORD: %d,%d,", &sk, &sz)) {
                        MBED_ASSERT (sz <= MAX_READ_SIZE);
                        readSize = _at->read(tmpBuf, sz + 2);
                        if ((readSize > 0) &&
                            (*(tmpBuf + readSize - sz - 2) == '\"' && *(tmpBuf  + readSize - 1) == '\"')) {
                            if (sz > size) {
                                sz = size;
                            }
                            memcpy(buf, (tmpBuf + readSize - sz - 1), sz);
                            socket->pending -= readBlk;
                            size -= sz;
                            cnt += sz;
                            buf += sz;
                        }
                        // Wait for the "OK" before continuing
                        _at->recv("OK");
                    }
                } else if (!TIMEOUT(timer, _timeout)) {
                    // Wait for URCs
                    _at->recv(UNNATURAL_STRING);
                } else {
                    // Timeout with nothing received
                    nsapiErrorSize = NSAPI_ERROR_WOULD_BLOCK;
                    success = false;
                }
            } else {
                nsapiErrorSize = NSAPI_ERROR_NO_CONNECTION;
                success = false;
            }
        }

        UNLOCK();
        free(tmpBuf);
    } else {
        success = false;
        nsapiErrorSize = NSAPI_ERROR_NO_MEMORY;
    }
    timer.stop();
    _at->set_timeout(AT_PARSER_TIMEOUT);

    if (success) {
        nsapiErrorSize = cnt;
        if ((cnt == 0) && (_timeout != TIMEOUT_BLOCKING)) {
            nsapiErrorSize = NSAPI_ERROR_WOULD_BLOCK;
        }
    }
    tr_debug("socket_recv: %d \"%*.*s\"", cnt, cnt, cnt, buf - cnt);

    return nsapiErrorSize;
}

// Receive a packet over a UDP socket.
nsapi_size_or_error_t UbloxCellularInterfaceGenericAtData::socket_recvfrom(nsapi_socket_t handle,
                                                                           SocketAddress *address,
                                                                           void *data,
                                                                           nsapi_size_t size)
{
    nsapi_size_or_error_t nsapiErrorSize = NSAPI_ERROR_DEVICE_ERROR;
    bool success = true;
    char *buf = (char *) data;
    nsapi_size_t readBlk;
    nsapi_size_t cnt = 0;
    char ipAddress[NSAPI_IP_SIZE];
    int port;
    char * tmpBuf = NULL;
    unsigned int sz, readSize;
    Timer timer;
    SockCtrl *socket = (SockCtrl *) handle;

    tr_debug("socket_recvfrom(0x%08x, %s(:%d), 0x%08x, %d)", (unsigned int) handle,
             address->get_ip_address(), address->get_port(), (unsigned int) data, size);

    _at->set_timeout(1000);
    timer.start();

    while (success && (size > 0)) {
        LOCK();

        readBlk = socket->pending;
        if (readBlk > 0) {
            memset (ipAddress, 0, sizeof (ipAddress)); // Ensure terminator
            if (_at->send("AT+USORF=%d,%d", socket->modemHandle, readBlk) &&
                _at->recv("+USORF: %*d,\"%" stringify(NSAPI_IP_SIZE) "[^\"]\",%d,%d,",
                          ipAddress, &port, &sz)) {
                tmpBuf = (char *) malloc(sz + 2); // +2 for leading and trailing quotes
                if (tmpBuf != NULL) {
                    tr_debug("...reading %d bytes from handle %d...", sz, socket->modemHandle);
                    readSize = _at->read(tmpBuf, sz + 2);
                    tr_debug("tmpBuf: %d |%*.*s|", readSize, readSize, readSize, tmpBuf);
                    if ((*tmpBuf == '\"') && *(tmpBuf + sz + 1) == '\"') {
                        if (sz > size) {
                            sz = size;
                        }
                        tr_debug("...copying %d bytes into buffer...", sz);
                        memcpy(buf, tmpBuf + 1, sz);
                        socket->pending -= readBlk;
                        address->set_ip_address(ipAddress);
                        address->set_port(port);
                        cnt += sz;
                        buf += sz;
                        size = 0; // UDP packets must arrive all at once, so this means DONE.
                    }
                    free(tmpBuf);
                } else {
                    success = false;
                    nsapiErrorSize = NSAPI_ERROR_NO_MEMORY;
                }
                // Wait for the "OK" before continuing
                _at->recv("OK");
            } else {
              // Should never fail to read when there is pending data
                success = false;
            }
        } else if (!TIMEOUT(timer, _timeout)) {
            // Wait for URCs
            _at->recv(UNNATURAL_STRING);
        } else {
            // Timeout with nothing received
            nsapiErrorSize = NSAPI_ERROR_WOULD_BLOCK;
            success = false;
        }

        UNLOCK();
    }
    timer.stop();
    _at->set_timeout(AT_PARSER_TIMEOUT);

    if (success) {
        nsapiErrorSize = cnt;
    }
    tr_debug("socket_recvfrom: %d \"%*.*s\"", cnt, cnt, cnt, buf - cnt);

    return nsapiErrorSize;
}

// Unsupported TCP server functions.
nsapi_error_t UbloxCellularInterfaceGenericAtData::socket_listen(nsapi_socket_t handle,
                                                                 int backlog)
{
    return NSAPI_ERROR_UNSUPPORTED;
}
nsapi_error_t UbloxCellularInterfaceGenericAtData::socket_accept(nsapi_socket_t server,
                                                                 nsapi_socket_t *handle,
                                                                 SocketAddress *address)
{
    return NSAPI_ERROR_UNSUPPORTED;
}
void UbloxCellularInterfaceGenericAtData::socket_attach(nsapi_socket_t handle,
                                                        void (*callback)(void *),
                                                        void *data)
{
}

// Unsupported option functions.
nsapi_error_t UbloxCellularInterfaceGenericAtData::setsockopt(nsapi_socket_t handle,
                                                              int level, int optname,
                                                              const void *optval,
                                                              unsigned optlen)
{
    return NSAPI_ERROR_UNSUPPORTED;
}
nsapi_error_t UbloxCellularInterfaceGenericAtData::getsockopt(nsapi_socket_t handle,
                                                              int level, int optname,
                                                              void *optval,
                                                              unsigned *optlen)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

/**********************************************************************
 * PUBLIC METHODS
 **********************************************************************/

// Constructor.
UbloxCellularInterfaceGenericAtData::UbloxCellularInterfaceGenericAtData(bool debug_on,
                                                                         PinName tx,
                                                                         PinName rx,
                                                                         int baud):
                                     UbloxCellularGenericBase(debug_on, tx, rx, baud)
{
    _sim_pin_check_change_pending = false;
    _sim_pin_check_change_pending_enabled_value = false;
    _sim_pin_change_pending = false;
    _sim_pin_change_pending_new_pin_value = NULL;
    _apn = NULL;
    _uname = NULL;
    _pwd = NULL;

    // Initialise sockets storage
    memset(_sockets, 0, sizeof(_sockets));
    for (unsigned int socket = 0; socket < sizeof(_sockets) / sizeof(_sockets[0]); socket++) {
        _sockets[socket].modemHandle = SOCKET_UNUSED;
    }

    // The authentication to use
    _auth = NSAPI_SECURITY_UNKNOWN;

    // Nullify the temporary IP address storage
    _ip = NULL;

    // URC handlers for sockets
    _at->oob("+UUSORD", callback(this, &UbloxCellularInterfaceGenericAtData::UUSORD_URC));
    _at->oob("+UUSORF", callback(this, &UbloxCellularInterfaceGenericAtData::UUSORF_URC));
    _at->oob("+UUSOCL", callback(this, &UbloxCellularInterfaceGenericAtData::UUSOCL_URC));
    _at->oob("+UUPSDD", callback(this, &UbloxCellularInterfaceGenericAtData::UUPSDD_URC));
}

// Destructor.
UbloxCellularInterfaceGenericAtData::~UbloxCellularInterfaceGenericAtData()
{
    // Free _ip if it was ever allocated
    free(_ip);
}

// Set the authentication scheme.
void UbloxCellularInterfaceGenericAtData::set_authentication(nsapi_security_t auth)
{
    _auth = auth;
}

// Set APN, user name and password.
void  UbloxCellularInterfaceGenericAtData::set_credentials(const char *apn,
                                                           const char *uname,
                                                           const char *pwd)
{
    _apn = apn;
    _uname = uname;
    _pwd = pwd;
}

// Set PIN.
void UbloxCellularInterfaceGenericAtData::set_sim_pin(const char *pin) {
    set_pin(pin);
}

// Get the IP address of a host.
nsapi_error_t UbloxCellularInterfaceGenericAtData::gethostbyname(const char *host,
                                                                 SocketAddress *address,
                                                                 nsapi_version_t version)
{
    nsapi_error_t nsapiError = NSAPI_ERROR_DEVICE_ERROR;
    char ipAddress[NSAPI_IP_SIZE];

    if (address->set_ip_address(host)) {
        nsapiError = NSAPI_ERROR_OK;
    } else {
        LOCK();
        memset (ipAddress, 0, sizeof (ipAddress)); // Ensure terminator
        if (_at->send("AT+UDNSRN=0,\"%s\"", host) &&
            _at->recv("+UDNSRN: \"%" stringify(NSAPI_IP_SIZE) "[^\"]\"", ipAddress) &&
            _at->recv("OK")) {
            if (address->set_ip_address(ipAddress)) {
                nsapiError = NSAPI_ERROR_OK;
            }
        }
        UNLOCK();
    }

    return nsapiError;
}

// Make a cellular connection
nsapi_error_t UbloxCellularInterfaceGenericAtData::connect(const char *sim_pin,
                                                           const char *apn,
                                                           const char *uname,
                                                           const char *pwd)
{
    nsapi_error_t nsapi_error;

    if (sim_pin != NULL) {
        _pin = sim_pin;
    }

    if (apn != NULL) {
        _apn = apn;
    }

    if ((uname != NULL) && (pwd != NULL)) {
        _uname = uname;
        _pwd = pwd;
    } else {
        _uname = NULL;
        _pwd = NULL;
    }

    nsapi_error = connect();

    return nsapi_error;
}

// Make a cellular connection using the IP stack on board the cellular modem
nsapi_error_t UbloxCellularInterfaceGenericAtData::connect()
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
    bool registered = false;

    // Set up modem and then register with the network
    if (init()) {
        nsapi_error = NSAPI_ERROR_NO_CONNECTION;
        // Perform any pending SIM actions
        if (_sim_pin_check_change_pending) {
            if (!check_pin(_sim_pin_check_change_pending_enabled_value)) {
                nsapi_error = NSAPI_ERROR_AUTH_FAILURE;
            }
            _sim_pin_check_change_pending = false;
        }
        if (_sim_pin_change_pending) {
            if (!change_pin(_sim_pin_change_pending_new_pin_value)) {
                nsapi_error = NSAPI_ERROR_AUTH_FAILURE;
            }
            _sim_pin_change_pending = false;
        }

        if (nsapi_error == NSAPI_ERROR_NO_CONNECTION) {
            for (int retries = 0; !registered && (retries < 3); retries++) {
                if (nwk_registration(_dev_info->dev)) {
                    registered = true;;
                }
            }
        }
    }

    // Attempt to establish a connection
    if (registered && connectModemStack()) {
        nsapi_error = NSAPI_ERROR_OK;
    }

    return nsapi_error;
}

// User initiated disconnect.
nsapi_error_t UbloxCellularInterfaceGenericAtData::disconnect()
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_DEVICE_ERROR;

    if (disconnectModemStack() && nwk_deregistration()) {
        nsapi_error = NSAPI_ERROR_OK;
    }

    return nsapi_error;
}

// Enable or disable SIM PIN check lock.
nsapi_error_t UbloxCellularInterfaceGenericAtData::check_sim_pin(bool check,
                                                                 bool immediate,
                                                                 const char *sim_pin)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_AUTH_FAILURE;

    if (sim_pin != NULL) {
        _pin = sim_pin;
    }

    if (immediate) {
        if (init()) {
            if (check_pin(check)) {
                nsapi_error = NSAPI_ERROR_OK;
            }
        } else {
            nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
        }
    } else {
        nsapi_error = NSAPI_ERROR_OK;
        _sim_pin_check_change_pending = true;
        _sim_pin_check_change_pending_enabled_value = check;
    }

    return nsapi_error;
}

// Change the PIN code for the SIM card.
nsapi_error_t UbloxCellularInterfaceGenericAtData::change_sim_pin(const char *new_pin,
                                                                  bool immediate,
                                                                  const char *old_pin)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_AUTH_FAILURE;

    if (old_pin != NULL) {
        _pin = old_pin;
    }

    if (immediate) {
        if (init()) {
            if (change_pin(new_pin)) {
                nsapi_error = NSAPI_ERROR_OK;
            }
        } else {
            nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
        }
    } else {
        nsapi_error = NSAPI_ERROR_OK;
        _sim_pin_change_pending = true;
        _sim_pin_change_pending_new_pin_value = new_pin;
    }

    return nsapi_error;
}

// Determine if the connection is up.
bool UbloxCellularInterfaceGenericAtData::is_connected()
{
    return get_ip_address() != NULL;
}

// Get the IP address of the on-board modem IP stack.
const char * UbloxCellularInterfaceGenericAtData::get_ip_address()
{
    SocketAddress address;
    LOCK();

    if (_ip == NULL) {
        // Temporary storage for an IP address string with terminator
        _ip = (char *) malloc(NSAPI_IP_SIZE);
    }

    if (_ip != NULL) {
        memset(_ip, 0, NSAPI_IP_SIZE); // Ensure a terminator
        // +UPSND=<profile_id>,<param_tag>[,<dynamic_param_val>]
        // If we get back a quoted "w.x.y.z" then we have an IP address,
        // otherwise we don't.
        if (!_at->send("AT+UPSND=" PROFILE ",0") ||
            !_at->recv("+UPSND: " PROFILE ",0,\"%" stringify(NSAPI_IP_SIZE) "[^\"]\"", _ip) ||
            !_at->recv("OK") ||
            !address.set_ip_address(_ip) || // Return NULL if the address is not a valid one
            !address) { // Return null if the address is zero
            free (_ip);
            _ip = NULL;
        }
    }

    UNLOCK();
    return _ip;
}

// Get the local network mask.
const char *UbloxCellularInterfaceGenericAtData::get_netmask()
{
    // Not implemented.
    return NULL;
}

// Get the local gateways.
const char *UbloxCellularInterfaceGenericAtData::get_gateway()
{
    return get_ip_address();
}

// Determine if there's anything to read.
nsapi_size_or_error_t UbloxCellularInterfaceGenericAtData::readable(nsapi_socket_t handle)
{
    nsapi_size_or_error_t nsapiSizeError = NSAPI_ERROR_DEVICE_ERROR;
    SockCtrl *socket = (SockCtrl *) handle;

    tr_debug("socket_readable(0x%08x)", (unsigned int) handle);
    if (socket->tcpConnected) {
        // Allow time to receive unsolicited commands
        wait_ms(8000);
        if (socket->tcpConnected) {
            nsapiSizeError = socket->pending;
            nsapiSizeError = NSAPI_ERROR_OK;
        }
    } else {
        nsapiSizeError = NSAPI_ERROR_NO_CONNECTION;
    }

    return nsapiSizeError;
}

// Callback in case the connection is lost.
void UbloxCellularInterfaceGenericAtData::connection_status_cb(void (*fptr)(nsapi_error_t))
{
    callback_fptr = fptr;
}

// End of file

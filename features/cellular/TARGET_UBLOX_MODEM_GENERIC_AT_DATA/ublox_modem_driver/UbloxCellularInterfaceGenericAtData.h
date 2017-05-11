/* Copyright (c) 2017 ARM Limited
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

#ifndef _UBLOX_CELLULAR_INTERFACE_GENERIC_AT_DATA_
#define _UBLOX_CELLULAR_INTERFACE_GENERIC_AT_DATA_

#include "NetworkStack.h"
#include "TARGET_UBLOX_MODEM_GENERIC/ublox_modem_driver/UbloxCellularGenericBase.h"

/** UbloxCellularInterfaceGenericAtData class.
 *
 *  This uses the IP stack that is on-board the
 *  cellular modem instead of the LWIP stack
 *  on the mbed MCU.
 *
 *  There are two advantages to using this mechanism:
 *
 *  1.  Since the modem interface remains in AT mode
 *      throughout, it is possible to continue using
 *      any AT commands (e.g. send SMS, use the module's
 *      file system, etc.) while the connection is up.
 *
 *  2.  The UbloxCellularGenericDataAtExt class can
 *      be used to provide a very simply HTTP interface
 *      using the modem's on-board HTTP client.
 *
 *  3.  LWIP is not required (and hence RAM is saved).
 *
 *  The disadvantage is that some additional parsing
 *  (at the AT interface) has to go on in order to exchange
 *  IP packets, so this is less efficient under heavy loads.
 */

// Forward declaration
class NetworkStack;

class UbloxCellularInterfaceGenericAtData : public NetworkStack, public CellularInterface, virtual public UbloxCellularGenericBase {

public:
     UbloxCellularInterfaceGenericAtData(bool debug_on = false,
                                         PinName tx = MDMTXD,
                                         PinName rx = MDMRXD,
                                         int baud = MBED_CONF_UBLOX_MODEM_GENERIC_BAUD_RATE);
    ~UbloxCellularInterfaceGenericAtData();

    /**
     * The profile to use.
     */
    #define PROFILE "0"

    /** Translates a hostname to an IP address with specific version.
     *
     *  The hostname may be either a domain name or an IP address. If the
     *  hostname is an IP address, no network transactions will be performed.
     *
     *  If no stack-specific DNS resolution is provided, the hostname
     *  will be resolve using a UDP socket on the stack.
     *
     *  @param host     Hostname to resolve.
     *  @param address  Destination for the host SocketAddress.
     *  @param version  IP version of address to resolve, NSAPI_UNSPEC indicates
     *                  version is chosen by the stack (defaults to NSAPI_UNSPEC).
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t gethostbyname(const char *host,
                                        SocketAddress *address,
                                        nsapi_version_t version = NSAPI_UNSPEC);

    /** Set the authentication scheme.
     *
     *  @param auth      the authentication scheme, chose from
     *                   NSAPI_SECURITY_NONE, NSAPI_SECURITY_PAP,
     *                   NSAPI_SECURITY_CHAP or NSAPI_SECURITY_UNKNOWN;
     *                   use NSAPI_SECURITY_UNKNOWN to try all of none,
     *                   PAP and CHAP.
     */
    virtual void set_authentication(nsapi_security_t auth);

    /** Set the Cellular network credentials.
     *
     *  Please check documentation of connect() for default behaviour of APN settings.
     *
     *  @param apn      Access point name.
     *  @param uname    optionally, username.
     *  @param pwd      optionally, password.
     */
    virtual void set_credentials(const char *apn, const char *uname = 0,
                                 const char *pwd = 0);

    /** Set the PIN code for the SIM card.
     *
     *  @param sim_pin      PIN for the SIM card.
     */
    virtual void set_SIM_pin(const char *sim_pin);

    /** Connect to the cellular network and start the interface.
     *
     *  Attempts to connect to a Cellular network.  Note: if init() has
     *  not been called beforehand, connect() will call it first.
     *
     *  @param sim_pin     PIN for the SIM card.
     *  @param apn         optionally, access point name.
     *  @param uname       optionally, Username.
     *  @param pwd         optionally, password.
     *  @return            NSAPI_ERROR_OK on success, or negative error code on failure.
     */
    virtual nsapi_error_t connect(const char *sim_pin, const char *apn = 0,
                                  const char *uname = 0, const char *pwd = 0);

    /** Attempt to connect to the Cellular network.
     *
     *  Brings up the network interface. Connects to the Cellular Radio
     *  network and then brings up IP stack on the cellular modem to be used
     *  indirectly via AT commands, rather than LWIP.  Note: if init() has
     *  not been called beforehand, connect() will call it first.
     *
     *  For APN setup, default behaviour is to use 'internet' as APN string and assuming no authentication
     *  is required, i.e., username and password are not set. Optionally, a database lookup can be requested
     *  by turning on the APN database lookup feature. The APN database is by no means exhaustive. It contains
     *  a short list of some public APNs with publicly available usernames and passwords (if required) in
     *  some particular countries only. Lookup is done using IMSI (International mobile subscriber identifier).
     *
     *  The preferred method is to setup APN using 'set_credentials()' API.
     *
     *  If you find that the AT interface returns "CONNECT" but shortly afterwards drops the connection
     *  then 99% of the time this will be because the APN is incorrect.
     *
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t connect();

    /** Attempt to disconnect from the network.
     *
     *  Brings down the network interface.
     *  Does not bring down the Radio network.
     *
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t disconnect();

    /** Adds or removes a SIM facility lock.
     *
     * Can be used to enable or disable SIM PIN check at device startup.
     *
     * @param check        can be set to true if the SIM PIN check is supposed
     *                     to be enabled and vice versa.
     * @param immediate    if true, change the SIM PIN now, else set a flag
     *                     and make the change only when connect() is called.
     *                     If this is true and init() has not been called previously,
     *                     it will be called first.
     * @param sim_pin      the current SIM PIN, must be a const.  If this is not
     *                     provided, the SIM PIN must have previously been set by a
     *                     call to set_SIM_pin().
     * @return             0 on success, negative error code on failure.
     */
    nsapi_error_t check_sim_pin(bool check, bool immediate = false,
                                const char *sim_pin = NULL);

    /** Change the PIN for the SIM card.
     *
     * Provide the new PIN for your SIM card with this API.  It is ONLY possible to
     * change the SIM PIN when SIM PIN checking is ENABLED.
     *
     * @param new_pin     new PIN to be used in string format, must be a const.
     * @param immediate   if true, change the SIM PIN now, else set a flag
     *                    and make the change only when connect() is called.
     *                    If this is true and init() has not been called previously,
     *                    it will be called first.
     * @param old_pin     old PIN, must be a const.  If this is not provided, the SIM PIN
     *                    must have previously been set by a call to set_SIM_pin().
     * @return            0 on success, negative error code on failure.
     */
    nsapi_error_t change_sim_pin(const char *new_pin, bool immediate = false,
                                 const char *old_pin = NULL);

    /** Check if the connection is currently established or not.
     *
     * @return true/false   If the cellular module have successfully acquired a carrier and is
     *                      connected to an external packet data network using PPP, isConnected()
     *                      API returns true and false otherwise.
     */
    virtual bool isConnected();

    /** Get the local IP address
     *
     *  @return         Null-terminated representation of the local IP address
     *                  or null if no IP address has been received.
     */
    virtual const char *get_ip_address();

    /** Get the local network mask.
     *
     *  @return         Null-terminated representation of the local network mask
     *                  or null if no network mask has been received.
     */
    virtual const char *get_netmask();

    /** Get the local gateways.
     *
     *  @return         Null-terminated representation of the local gateway
     *                  or null if no network mask has been received.
     */
    virtual const char *get_gateway();

    /** Set blocking or non-blocking mode of the socket.
     *
     *  set_blocking(false) is equivalent to set_timeout(-1).
     *  set_blocking(true) is equivalent to set_timeout(0).
     *
     *  @param blocking true for blocking mode, false for non-blocking mode.
     *  @return         0 on success, negative error code on failure
     */
    virtual nsapi_error_t set_blocking(nsapi_socket_t handle, bool blocking);

    /** Set timeout on blocking socket operations.
     *
     *  set_timeout(-1) is equivalent to set_blocking(true).
     *
     *  @param timeout  Timeout in milliseconds
     *  @return         0 on success, negative error code on failure
     */
    virtual nsapi_error_t set_timeout(nsapi_socket_t handle, int timeout);

    /** Get the number of bytes pending for reading for this socket.
     *
     * @param handle    The socket handle.
     * @return          The number of bytes pending on success, negative error
     *                  code on failure.
     */
    virtual nsapi_size_or_error_t readable(nsapi_socket_t handle);

    /** Call back in case connection is lost.
     *
     * @param fptr     the function to call.
     */
    void connection_status_cb(void (*fptr)(nsapi_error_t));

protected:

    /** Infinite timeout.
     */
    #define TIMEOUT_BLOCKING -1

    /** Socket "unused" value
     */
    #define SOCKET_UNUSED - 1

    /** Maximum number of bytes that can be written to a socket.
     */
    #define MAX_WRITE_SIZE 1024

    /** Maximum number of bytes that can be read from a socket.
     */
    #define MAX_READ_SIZE 128

    /** Management structure for sockets.
     */
    typedef struct {
        int modemHandle;
        int timeoutMilliseconds;
        volatile bool connected;
        volatile nsapi_size_t pending;
    } SockCtrl;

    /** Sockets storage.
     */
    SockCtrl _sockets[12];

    /** Storage for a single IP address.
     */
    char *_ip;

    /** The APN to use.
     */
    const char *_apn;

    /** The user name to use.
     */
    const char *_uname;

    /** The password to use.
     */
    const char *_pwd;

    /** The type of authentication to use.
     */
    nsapi_security_t _auth;

    /** Get the next set of credentials from the database.
     */
    virtual void get_next_credentials(const char * config);

    /** Activate one of the on-board modem's connection profiles.
     *
     * @param apn      the apn to use.
     * @param username the username to use.
     * @param password the password to use.
     * @param auth     the authentication method to use
     *                 (NSAPI_SECURITY_NONE, NSAPI_SECURITY_PAP,
     *                 NSAPI_SECURITY_CHAP or NSAPI_SECURITY_UNKNOWN).
     * @return true if successful, otherwise false.
     */
    virtual bool activateProfile(const char* apn, const char* username,
                                 const char* password, nsapi_security_t auth);

    /** Activate a profile using the existing external connection.
     *
     * @return true if successful, otherwise false.
     */
    virtual bool activateProfileReuseExternal(void);

    /** Activate a profile based on connection ID.
     *
     * @param cid       the connection ID.
     * @param apn       the apn to use.
     * @param username  the username to use.
     * @param password  the password to use.
     * @param auth      the authentication method to use.
     * @return true if successful, otherwise false.
     */
    virtual bool activateProfileByCid(int cid, const char* apn, const char* username,
                                      const char* password, nsapi_security_t auth);

    /** Connect the on board IP stack of the modem.
     *
     * @return true if successful, otherwise false.
     */
    virtual bool connectModemStack();

    /** Disconnect the on board IP stack of the modem.
     *
     * @return true if successful, otherwise false.
     */
    virtual bool disconnectModemStack();

    /** Provide access to the NetworkStack object
     *
     *  @return The underlying NetworkStack object
     */
    virtual NetworkStack *get_stack();

     /** Open a socket.
     *
     *  Creates a network socket and stores it in the specified handle.
     *  The handle must be passed to following calls on the socket.
     *
     *  @param handle   Destination for the handle to a newly created socket.
     *  @param proto    Protocol of socket to open, NSAPI_TCP or NSAPI_UDP.
     *  @param port     Optional port to bind to.
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t socket_open(nsapi_socket_t *handle, nsapi_protocol_t proto);

    /** Close a socket.
     *
     *  Closes any open connection and deallocates any memory associated
     *  with the socket.
     *
     *  @param handle   Socket handle.
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t socket_close(nsapi_socket_t handle);

    /** Bind a specific address to a socket.
     *
     *  Binding a socket specifies the address and port on which to receive
     *  data. If the IP address is zeroed, only the port is bound.
     *
     *  @param handle   Socket handle
     *  @param address  Local address to bind
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t socket_bind(nsapi_socket_t handle, const SocketAddress &address);

    /** Connects TCP socket to a remote host.
     *
     *  Initiates a connection to a remote server specified by the
     *  indicated address.
     *
     *  @param handle   Socket handle
     *  @param address  The SocketAddress of the remote host
     *  @return         0 on success, negative error code on failure
     */
    virtual nsapi_error_t socket_connect(nsapi_socket_t handle, const SocketAddress &address);

    /** Send data over a TCP socket.
     *
     *  The socket must be connected to a remote host. Returns the number of
     *  bytes sent from the buffer.
     *
     *  @param handle   Socket handle.
     *  @param data     Buffer of data to send to the host.
     *  @param size     Size of the buffer in bytes.
     *  @return         Number of sent bytes on success, negative error
     *                  code on failure.
     */
    virtual nsapi_size_or_error_t socket_send(nsapi_socket_t handle,
                                              const void *data, nsapi_size_t size);

    /** Send a packet over a UDP socket.
     *
     *  Sends data to the specified address. Returns the number of bytes
     *  sent from the buffer.
     *
     *  @param handle   Socket handle.
     *  @param address  The SocketAddress of the remote host.
     *  @param data     Buffer of data to send to the host.
     *  @param size     Size of the buffer in bytes.
     *  @return         Number of sent bytes on success, negative error
     *                  code on failure.
     */
    virtual nsapi_size_or_error_t socket_sendto(nsapi_socket_t handle, const SocketAddress &address,
                                                const void *data, nsapi_size_t size);

    /** Receive data over a TCP socket.
     *
     *  The socket must be connected to a remote host. Returns the number of
     *  bytes received into the buffer.
     *
     *  @param handle   Socket handle.
     *  @param data     Destination buffer for data received from the host.
     *  @param size     Size of the buffer in bytes.
     *  @return         Number of received bytes on success, negative error
     *                  code on failure.
     */
    virtual nsapi_size_or_error_t socket_recv(nsapi_socket_t handle,
                                              void *data, nsapi_size_t size);

    /** Receive a packet over a UDP socket.
     *
     *  Receives data and stores the source address in address if address
     *  is not NULL. Returns the number of bytes received into the buffer.
     *
     *  @param handle   Socket handle.
     *  @param address  Destination for the source address or NULL.
     *  @param data     Destination buffer for data received from the host.
     *  @param size     Size of the buffer in bytes.
     *  @return         Number of received bytes on success, negative error
     *                  code on failure.
     */
    virtual nsapi_size_or_error_t socket_recvfrom(nsapi_socket_t handle, SocketAddress *address,
                                                  void *data, nsapi_size_t size);

    /** Listen for connections on a TCP socket
     *
     *  Marks the socket as a passive socket that can be used to accept
     *  incoming connections.
     *
     *  @param backlog  Number of pending connections that can be queued
     *                  simultaneously, defaults to 1
     *  @return         0 on success, negative error code on failure
     */
    virtual nsapi_error_t socket_listen(nsapi_socket_t handle, int backlog);

    /** Accepts a connection on a TCP socket
     *
     *  The server socket must be bound and set to listen for connections.
     *  On a new connection, creates a network socket and stores it in the
     *  specified handle. The handle must be passed to following calls on
     *  the socket.
     *
     *  A stack may have a finite number of sockets, in this case
     *  NSAPI_ERROR_NO_SOCKET is returned if no socket is available.
     *
     *  This call is non-blocking. If accept would block,
     *  NSAPI_ERROR_WOULD_BLOCK is returned immediately.
     *
     *  @param server   Socket handle to server to accept from
     *  @param handle   Destination for a handle to the newly created socket
     *  @param address  Destination for the remote address or NULL
     *  @return         0 on success, negative error code on failure
     */
    virtual nsapi_error_t socket_accept(nsapi_socket_t server,
                                        nsapi_socket_t *handle, SocketAddress *address = 0);

    /** Register a callback on state change of the socket
     *
     *  The specified callback will be called on state changes such as when
     *  the socket can recv/send/accept successfully and on when an error
     *  occurs. The callback may also be called spuriously without reason.
     *
     *  The callback may be called in an interrupt context and should not
     *  perform expensive operations such as recv/send calls.
     *
     *  @param handle   Socket handle
     *  @param callback Function to call on state change
     *  @param data     Argument to pass to callback
     */
    virtual void socket_attach(nsapi_socket_t handle, void (*callback)(void *), void *data);

    /*  Set stack-specific socket options
     *
     *  The setsockopt allow an application to pass stack-specific hints
     *  to the underlying stack. For unsupported options,
     *  NSAPI_ERROR_UNSUPPORTED is returned and the socket is unmodified.
     *
     *  @param handle   Socket handle
     *  @param level    Stack-specific protocol level
     *  @param optname  Stack-specific option identifier
     *  @param optval   Option value
     *  @param optlen   Length of the option value
     *  @return         0 on success, negative error code on failure
     */
    virtual nsapi_error_t setsockopt(nsapi_socket_t handle, int level,
                                     int optname, const void *optval, unsigned optlen);

    /*  Get stack-specific socket options
     *
     *  The getstackopt allow an application to retrieve stack-specific hints
     *  from the underlying stack. For unsupported options,
     *  NSAPI_ERROR_UNSUPPORTED is returned and optval is unmodified.
     *
     *  @param handle   Socket handle
     *  @param level    Stack-specific protocol level
     *  @param optname  Stack-specific option identifier
     *  @param optval   Destination for option value
     *  @param optlen   Length of the option value
     *  @return         0 on success, negative error code on failure
     */
    virtual nsapi_error_t getsockopt(nsapi_socket_t handle, int level,
                                     int optname, void *optval, unsigned *optlen);

private:

    #define IS_PROFILE(p) (((p) >= 0) && (((unsigned int) p) < (sizeof(_httpProfiles)/sizeof(_httpProfiles[0]))) \
                           && (_httpProfiles[p].modemHandle != HTTP_PROF_UNUSED))
    #define TIMEOUT(t, ms)  ((ms != TIMEOUT_BLOCKING) && (ms < t.read_ms()))
    #define stringify(a) str(a)
    #define str(a) #a

    bool _sim_pin_check_change_pending;
    bool _sim_pin_check_change_pending_enabled_value;
    bool _sim_pin_change_pending;
    const char *_sim_pin_change_pending_new_pin_value;
    SockCtrl * findSocket(int modemHandle = SOCKET_UNUSED);
    int nsapiSecurityToModemSecurity(nsapi_security_t nsapiSecurity);
    void UUSORD_URC();
    void UUSORF_URC();
    void UUSOCL_URC();
};

#endif // _UBLOX_CELLULAR_INTERFACE_GENERIC_AT_DATA_

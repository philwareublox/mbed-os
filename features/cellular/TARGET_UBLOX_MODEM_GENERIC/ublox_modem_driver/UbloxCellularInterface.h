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

#ifndef _UBLOX_CELLULAR_INTERFACE_
#define _UBLOX_CELLULAR_INTERFACE_

#include "mbed.h"
#include "nsapi.h"
#include "rtos.h"
#include "FileHandle.h"
#include "InterruptIn.h"
#include "ATParser.h"

/**********************************************************************
 * MACROS
 **********************************************************************/

/**
 *  Helper to make sure that lock unlock pair is always balanced
 */
#define LOCK()         { lock()

/**
 *  Helper to make sure that lock unlock pair is always balanced
 */
#define UNLOCK()       } unlock()

/**********************************************************************
 * TYPES
 **********************************************************************/

/**
 * Supported u-blox modem variants
 */
typedef enum {
    DEV_TYPE_NONE=0,
    DEV_SARA_G35,
    DEV_LISA_U2,
    DEV_LISA_U2_03S,
    DEV_LISA_C2,
    DEV_SARA_U2,
    DEV_LEON_G2,
    DEV_TOBY_L2,
    DEV_MPCI_L2
} device_type;

/**
 * Network registration status
 * UBX-13001820 - AT Commands Example Application Note (Section 4.1.4.5)
 */
typedef enum {
   GSM=0,
   COMPACT_GSM=1,
   UTRAN=2,
   EDGE=3,
   HSDPA=4,
   HSUPA=5,
   HSDPA_HSUPA=6,
   LTE=7
} radio_access_nwk_type;

/**
 * Circuit Switched network registration status (CREG Usage)
 * UBX-13001820 - AT Commands Example Application Note (Section 7.10.3)
 */
typedef enum {
    CSD_NOT_REGISTERED_NOT_SEARCHING=0,
    CSD_REGISTERED=1,
    CSD_NOT_REGISTERED_SEARCHING=2,
    CSD_REGISTRATION_DENIED=3,
    CSD_UNKNOWN_COVERAGE=4,
    CSD_REGISTERED_ROAMING=5,
    CSD_SMS_ONLY=6,
    CSD_SMS_ONLY_ROAMING=7,
    CSD_CSFB_NOT_PREFERRED=9
} nwk_registration_status_csd;

/**
 * Packet Switched network registration status (CGREG Usage)
 * UBX-13001820 - AT Commands Example Application Note (Section 18.27.3)
 */
typedef enum {
    PSD_NOT_REGISTERED_NOT_SEARCHING=0,
    PSD_REGISTERED=1,
    PSD_NOT_REGISTERED_SEARCHING=2,
    PSD_REGISTRATION_DENIED=3,
    PSD_UNKNOWN_COVERAGE=4,
    PSD_REGISTERED_ROAMING=5,
    PSD_EMERGENCY_SERVICES_ONLY=8
} nwk_registration_status_psd;

/**
 * EPS network registration status (CEREG Usage)
 * UBX-13001820 - AT Commands Example Application Note (Section 18.36.3)
 */
typedef enum {
    EPS_NOT_REGISTERED_NOT_SEARCHING=0,
    EPS_REGISTERED=1,
    EPS_NOT_REGISTERED_SEARCHING=2,
    EPS_REGISTRATION_DENIED=3,
    EPS_UNKNOWN_COVERAGE=4,
    EPS_REGISTERED_ROAMING=5,
    EPS_EMERGENCY_SERVICES_ONLY=8
} nwk_registration_status_eps;

typedef struct {
    device_type dev;
    char ccid[20+1];    //!< Integrated Circuit Card ID
    char imsi[15+1];    //!< International Mobile Station Identity
    char imei[15+1];    //!< International Mobile Equipment Identity
    char meid[18+1];    //!< Mobile Equipment IDentifier
    int flags;
    bool ppp_connection_up;
    radio_access_nwk_type rat;
    nwk_registration_status_csd reg_status_csd;
    nwk_registration_status_psd reg_status_psd;
    nwk_registration_status_eps reg_status_eps;
} device_info;

/**********************************************************************
 * CLASSES
 **********************************************************************/

/* Forward declaration */
class NetworkStack;

/** UbloxCellularInterface class.
 *
 *  This interface serves as the controller/driver for the u-blox
 *  C030 board.
 */
class UbloxCellularInterface : public CellularInterface {

public:
    UbloxCellularInterface(bool debugOn = false, PinName tx = MDMTXD, PinName rx = MDMRXD, int baud = MBED_CONF_UBLOX_MODEM_GENERIC_BAUD_RATE, bool use_USB = false);
    ~UbloxCellularInterface();
    /** Initialise the modem, ready for use.
     *
     *  @param sim_pin     PIN for the SIM card.
     *  @return            NSAPI_ERROR_OK on success, or negative error code on failure.
     */
    virtual nsapi_error_t init(const char *sim_pin = 0);

    /** Put the modem into its lowest power state.
     */
    virtual void deinit();

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
    virtual void  set_SIM_pin(const char *sim_pin);

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
     *  network and then brings up the underlying network stack to be used
     *  by the cellular modem over PPP interface.  Note: if init() has
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
     *  Brings down the network interface. Shuts down the PPP interface
     *  of the underlying network stack. Does not bring down the Radio network.
     *
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t disconnect();

    /** Adds or removes a SIM facility lock.
     *
     * Can be used to enable or disable SIM PIN check at device startup.
     *
     * @param pin_check_disabled  can be set to true if the SIM PIN check is supposed
     *                            to be disabled and vice versa.
     * @param immediate           if true, change the SIM PIN now, else set a flag
     *                            and make the change only when connect() is called.
     *                            If this is true and init() has not been called previously,
     *                            it will be called first.
     * @param sim_pin             the current SIM PIN, must be a const.  If this is not
     *                            provided, the SIM PIN must have previously been set by a
     *                            call to set_SIM_pin().
     * @return                    0 on success, negative error code on failure.
     */
    nsapi_error_t add_remove_sim_pin_check(bool pin_check_disabled, bool immediate = false,
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

    /** Call back in case connection is lost.
     *
     * @param fptr     the function to call.
     */
    void connection_lost_notification_cb(void (*fptr)(nsapi_error_t));

private:
    FileHandle *_fh;
    bool _useUSB;
    const char *_pin;
    const char *_apn;
    const char *_uname;
    const char *_pwd;
    bool _debug_trace_on;
    bool _modem_initialised;
    bool _sim_pin_check_enabled;
    bool _sim_pin_check_change_pending;
    bool _sim_pin_check_change_pending_disabled_value;
    bool _sim_pin_change_pending;
    const char *_sim_pin_change_pending_new_pin_value;
    void setup_at_parser();
    void shutdown_at_parser();
    bool set_device_identity(device_type *dev);
    bool device_init(device_type dev);
    nsapi_error_t initialise_sim_card();
    nsapi_error_t setup_context_and_credentials();
    bool nwk_registration(device_type dev);
    bool nwk_registration_status(device_type dev);
    bool power_up_modem();
    void power_down_modem();
    bool is_registered_csd();
    bool is_registered_psd();
    bool is_registered_eps();
    void set_nwk_reg_status_csd(unsigned int status);
    void set_nwk_reg_status_psd(unsigned int status);
    void set_nwk_reg_status_eps(unsigned int status);
    void set_RAT(unsigned int AcTStatus);
    bool get_CCID();
    bool get_IMSI();
    bool get_IMEI();
    bool get_MEID();
    bool set_CMGF();
    bool set_CNMI();
    bool set_ATD();
    void parser_abort();
    nsapi_error_t do_add_remove_sim_pin_check(bool pin_check_disabled);
    nsapi_error_t do_change_sim_pin(const char *new_pin);
    void CMTI_URC();
    void CMT_URC();

protected:

    /**
     * Point to the instance of the AT parser in use.
     */
    ATParser *_at;

    /**
     * The mutex resource.
     */
    Mutex _mtx;

    /**
     * General info about the modem as a device.
     */
    device_info *_dev_info;

    /**
     * Lock a mutex when accessing the modem.
     */
    virtual void lock(void)     { _mtx.lock(); }

    /**
     * Unlock the modem when done accessing it.
     */
    virtual void unlock(void)   { _mtx.unlock(); }

    /**
     * Provide access to the underlying stack.
     *
     * @return The underlying network stack.
     */
    virtual NetworkStack *get_stack();
};

#endif //_UBLOX_CELLULAR_INTERFACE_

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

#ifndef REFERENCE_CELLULAR_INTERFACE_
#define REFERENCE_CELLULAR_INTERFACE_

#include "mbed.h"
#include "nsapi.h"
#include "rtos.h"
#include "FileHandle.h"
#include "InterruptIn.h"
#include "ATParser.h"
#include "PinNames.h"

// Forward declaration
class NetworkStack;

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

typedef struct {
    char ccid[20+1];    //!< Integrated Circuit Card ID
    char imsi[15+1];    //!< International Mobile Station Identity
    char imei[15+1];    //!< International Mobile Equipment Identity
    char meid[18+1];    //!< Mobile Equipment IDentifier
    int flags;
    bool ppp_connection_up;
    radio_access_nwk_type rat;
    nwk_registration_status_csd reg_status_csd;
    nwk_registration_status_psd reg_status_psd;
} device_info;

/** ReferenceCellularInterface class
 *
 *  This interface serves as the controller/driver for the Cellular
 *  modems (tested with UBLOX_C027 and MTS_DRAGONFLY_F411RE).
 */
class ReferenceCellularInterface : public CellularInterface {

public:
    ReferenceCellularInterface(bool debugOn = false, PinName tx = MDMTXD, PinName rx = MDMRXD, int baud = MBED_CONF_REF_CELL_DRV_BAUD_RATE);
    ~ReferenceCellularInterface();

    /** Set the Cellular network credentials
     *
     *  Please check documentation of connect() for default behaviour of APN settings.
     *
     *  @param apn      Access point name
     *  @param uname    optionally, Username
     *  @param pwd      optionally, password
     */
    virtual void set_credentials(const char *apn, const char *uname = 0,
                                                  const char *pwd = 0);

    /** Set the pin code for SIM card
     *
     *  @param sim_pin      PIN for the SIM card
     */
    virtual void  set_SIM_pin(const char *sim_pin);

    /** Start the interface
     *
     *  Attempts to connect to a Cellular network.
     *
     *  @param sim_pin     PIN for the SIM card
     *  @param apn         optionally, access point name
     *  @param uname       optionally, Username
     *  @param pwd         optionally, password
     *  @return            NSAPI_ERROR_OK on success, or negative error code on failure
     */
    virtual nsapi_error_t connect(const char *sim_pin, const char *apn = 0,
                                  const char *uname = 0, const char *pwd = 0);


    /** Attempt to connect to the Cellular network
     *
     *  Brings up the network interface. Connects to the Cellular Radio
     *  network and then brings up the underlying network stack to be used
     *  by the cellular modem over PPP interface.
     *
     *  If the SIM requires a PIN, and it is not set/invalid, NSAPI_ERROR_AUTH_ERROR is returned.
     *  For APN setup, default behaviour is to use 'internet' as APN string and assuming no authentication
     *  is required, i.e., username and password are no set. Optionally, a database lookup can be requested
     *  by turning on the APN database lookup feature. In order to do so, add 'MBED_REF_CELL_DRV_APN_LOOKUP'
     *  in your mbed_app.json. APN database is by no means exhaustive. It contains a short list of some public
     *  APNs with publicly available usernames and passwords (if required) in some particular countries only.
     *  Lookup is done using IMSI (International mobile subscriber identifier).
     *  Please note that even if 'MBED_REF_CELL_DRV_APN_LOOKUP' config option is set, 'set_credentials()' api still
     *  gets the precedence. If the aforementioned API is not used and the config option is set but no match is found in
     *  the lookup table then the driver tries to resort to default APN settings.
     *
     *  Preferred method is to setup APN using 'set_credentials()' API.

     *  @return         0 on success, negative error code on failure
     */
    virtual nsapi_error_t connect();

    /** Attempt to disconnect from the network
     *
     *  Brings down the network interface. Shuts down the PPP interface
     *  of the underlying network stack. Does not bring down the Radio network
     *
     *  @return         0 on success, negative error code on failure
     */
    virtual nsapi_error_t disconnect();

    /** Adds or removes a SIM facility lock
     *
     * Can be used to enable or disable SIM pin check at device startup.
     * This API sets up flags for the driver which would either enable or disable
     * SIM pin checking depending upon the user provided argument while establishing
     * connection. It doesn't do anything immediately other than setting up flags.
     *
     * @param check      can be set to true if the SIM pin check is supposed to be enabled
     *                   and vice versa.
     */
    void check_sim_pin(bool check);

    /** Change the pin for the SIM card
     *
     * Provide the new pin for your SIM card with this API. Old pin code will be assumed to
     * be set using set_SIM_pin() API. This API have no immediate effect. While establishing
     * connection, driver will change the SIM pin for the next boot.
     *
     * @param new_pin     new pin to be used in string format
     */
    void change_sim_pin(const char *new_pin);

    /** Check if the connection is currently established or not
     *
     * @return true/false   If the cellular module have successfully acquired a carrier and is
     *                      connected to an external packet data network using PPP, isConnected()
     *                      API returns true and false otherwise.
     */
    virtual bool isConnected();

    /** Get the local IP address
     *
     *  @return         Null-terminated representation of the local IP address
     *                  or null if no IP address has been recieved
     */
    virtual const char *get_ip_address();

    /** Get the local network mask
     *
     *  @return         Null-terminated representation of the local network mask
     *                  or null if no network mask has been recieved
     */
    virtual const char *get_netmask();

    /** Get the local gateways
     *
     *  @return         Null-terminated representation of the local gateway
     *                  or null if no network mask has been recieved
     */
    virtual const char *get_gateway();

    /** Get notified if the connection gets lost
     *
     *  @fptr         user defined callback
     */
    void connection_status_cb(void (*fptr)(nsapi_error_t));

private:
    FileHandle *_fh;
    ATParser *_at;
    InterruptIn *_dcd;
    const char *_new_pin;
    const char *_pin;
    const char *_apn;
    const char *_uname;
    const char *_pwd;
    bool _debug_trace_on;
    void setup_at_parser();
    void shutdown_at_parser();
    nsapi_error_t initialize_sim_card();
    nsapi_error_t setup_context_and_credentials();
    bool nwk_registration();
    bool nwk_registration_status_csd();
    bool nwk_registration_status_psd();
    bool PowerUpModem();
    void PowerDownModem();

protected:
    /** Provide access to the underlying stack
     *
     *  @return The underlying network stack
     */
    virtual NetworkStack *get_stack();

};

#endif //REFERENCE_CELLULAR_INTERFACE_

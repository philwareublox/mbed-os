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

#ifndef _UBLOX_CELLULAR_GENERIC_BASE_
#define _UBLOX_CELLULAR_GENERIC_BASE_

#include "mbed.h"
#include "ATParser.h"
#include "FileHandle.h"

/**********************************************************************
 * CLASSES
 **********************************************************************/

/** UbloxCellularGenericBase class.
 *
 *  This class provides all the base support for generic u-blox modems
 *  on C030 and C027 boards: module identification, power-up, network
 *  registration, etc.
 */
class UbloxCellularGenericBase {

public:
    UbloxCellularGenericBase(bool debug_on = false, PinName tx = MDMTXD,
                             PinName rx = MDMRXD,
                             int baud = MBED_CONF_UBLOX_CELL_GEN_DRV_BAUD_RATE);
    ~UbloxCellularGenericBase();

    /** Initialise the modem, ready for use.
     *
     *  @param pin     PIN for the SIM card.
     *  @return        true if successful, otherwise false.
     */
    bool init(const char *pin = 0);

    /** Put the modem into its lowest power state.
     */
    void deinit();

    /** Set the PIN code for the SIM card.
     *
     *  @param pin PIN for the SIM card.
     */
    void set_pin(const char *pin);

    /** Action a request to change the SIM pin checking.
     *
     * @param check true if SIM PIN checking is to be enabled,
     *              otherwise false.
     * @return      true if successful, otherwise false.
     */
    bool check_pin(bool check);

    /** Action a request to change the SIM pin.
     *
     * @param new_pin the new PIN to use.
     * @return        true if successful, otherwise false.
     */
    bool change_pin(const char *new_pin);

protected:

    #define OUTPUT_ENTER_KEY  "\r"

    #if MBED_CONF_UBLOX_CELL_GEN_DRV_AT_PARSER_BUFFER_SIZE
    #define AT_PARSER_BUFFER_SIZE   MBED_CONF_UBLOX_CELL_GEN_DRV_AT_PARSER_BUFFER_SIZE
    #else
    #define AT_PARSER_BUFFER_SIZE   256
    #endif

    #if MBED_CONF_UBLOX_CELL_GEN_DRV_AT_PARSER_TIMEOUT
    #define AT_PARSER_TIMEOUT       MBED_CONF_UBLOX_CELL_GEN_DRV_AT_PARSER_TIMEOUT
    #else
    #define AT_PARSER_TIMEOUT       8*1000 // Milliseconds
    #endif

    /** A string that would not normally be sent by the modem on the AT interface
     */
    #define UNNATURAL_STRING "\x01"

    /** Supported u-blox modem variants
     */
    typedef enum {
        DEV_TYPE_NONE = 0,
        DEV_SARA_G35,
        DEV_LISA_U2,
        DEV_LISA_U2_03S,
        DEV_SARA_U2,
        DEV_LEON_G2,
        DEV_TOBY_L2,
        DEV_MPCI_L2
    } device_type;

    /** Network registration status
     * UBX-13001820 - AT Commands Example Application Note (Section 4.1.4.5)
     */
    typedef enum {
       GSM = 0,
       COMPACT_GSM = 1,
       UTRAN = 2,
       EDGE = 3,
       HSDPA = 4,
       HSUPA = 5,
       HSDPA_HSUPA = 6,
       LTE = 7
    } radio_access_nwk_type;

    /** Circuit Switched network registration status (CREG Usage)
     * UBX-13001820 - AT Commands Example Application Note (Section 7.10.3)
     */
    typedef enum {
        CSD_NOT_REGISTERED_NOT_SEARCHING = 0,
        CSD_REGISTERED = 1,
        CSD_NOT_REGISTERED_SEARCHING = 2,
        CSD_REGISTRATION_DENIED = 3,
        CSD_UNKNOWN_COVERAGE = 4,
        CSD_REGISTERED_ROAMING = 5,
        CSD_SMS_ONLY = 6,
        CSD_SMS_ONLY_ROAMING = 7,
        CSD_CSFB_NOT_PREFERRED = 9
    } nwk_registration_status_csd;

    /** Packet Switched network registration status (CGREG Usage)
     * UBX-13001820 - AT Commands Example Application Note (Section 18.27.3)
     */
    typedef enum {
        PSD_NOT_REGISTERED_NOT_SEARCHING = 0,
        PSD_REGISTERED = 1,
        PSD_NOT_REGISTERED_SEARCHING = 2,
        PSD_REGISTRATION_DENIED = 3,
        PSD_UNKNOWN_COVERAGE = 4,
        PSD_REGISTERED_ROAMING = 5,
        PSD_EMERGENCY_SERVICES_ONLY = 8
    } nwk_registration_status_psd;

    /** EPS network registration status (CEREG Usage)
     * UBX-13001820 - AT Commands Example Application Note (Section 18.36.3)
     */
    typedef enum {
        EPS_NOT_REGISTERED_NOT_SEARCHING = 0,
        EPS_REGISTERED = 1,
        EPS_NOT_REGISTERED_SEARCHING = 2,
        EPS_REGISTRATION_DENIED = 3,
        EPS_UNKNOWN_COVERAGE = 4,
        EPS_REGISTERED_ROAMING = 5,
        EPS_EMERGENCY_SERVICES_ONLY = 8
    } nwk_registration_status_eps;

    /** Info about the modem.
     */
    typedef struct {
        device_type dev;
        char iccid[20+1];   //!< Integrated Circuit Card ID
        char imsi[15+1];    //!< International Mobile Station Identity
        char imei[15+1];    //!< International Mobile Equipment Identity
        char meid[18+1];    //!< Mobile Equipment IDentifier
        volatile radio_access_nwk_type rat;
        volatile nwk_registration_status_csd reg_status_csd;
        volatile nwk_registration_status_psd reg_status_psd;
        volatile nwk_registration_status_eps reg_status_eps;
    } device_info;

    /** IMPORTANT: the variables below are available to
     * classes that inherit this in order to keep things
     * simple. However, ONLY this class should free
     * any of the pointers, or there will be havoc.
     */

    /** Point to the instance of the AT parser in use.
     */
    ATParser *_at;

    /** File handle used by the AT parser.
     */
    FileHandle *_fh;

    /** The mutex resource.
     */
    Mutex _mtx;

    /** General info about the modem as a device.
     */
    device_info *_dev_info;

    /** The SIM PIN to use.
     */
    const char *_pin;

    /** Set to true to spit out debug traces.
     */
    bool _debug_trace_on;

    /** True if the modem is ready register to the network,
     * otherwise false.
     */
    bool _modem_initialised;

    /** True it the SIM requires a PIN, otherwise false.
     */
    bool _sim_pin_check_enabled;

    /** Power up the modem.
     *
     * @return true if successful, otherwise false.
     */
    bool power_up_modem();

    /** Power down the modem.
     */
    void power_down_modem();

    /** Perform registration with the network.
     *
     * @param dev   the device type.
     * @return true if successful, otherwise false.
     */
    bool nwk_registration(device_type dev);

    /** Test network registration status.
     *
     * @param dev   the device type.
     * @return true if registered, otherwise false.
     */
    bool nwk_registration_status(device_type dev);

    /** Lock a mutex when accessing the modem.
     */
    void lock(void)     { _mtx.lock(); }

    /** Helper to make sure that lock unlock pair is always balanced
     */
    #define LOCK()         { lock()

    /** Unlock the modem when done accessing it.
     */
    void unlock(void)   { _mtx.unlock(); }

    /** Helper to make sure that lock unlock pair is always balanced
     */
    #define UNLOCK()       } unlock()

    /** Set the device identity in _dev_info
     * based on the ATI string returned by
     * the module.
     *
     * @return true if dev is a known value,
     *         otherwise false.
     */
    bool set_device_identity(device_type *dev);

    /** Perform any modem initialisation that is
     * specialised by device type.
     *
     * @return true if successful, otherwise false.
     */
    bool device_init(device_type dev);

    /** Set up the SIM.
     *
     * @return true if successful, otherwiss false.
     */
    bool initialise_sim_card();

    /** True if the modem is registered for circuit
     * switched data, otherwise false.
     */
    bool is_registered_csd();

    /** True if the modem is registered for packet
     * switched data, otherwise false.
     */
    bool is_registered_psd();

    /** True if the modem is registered for enhanced
     * packet switched data (i.e. LTE and beyond),
     * otherwise false.
     */
    bool is_registered_eps();

private:

    void set_nwk_reg_status_csd(int status);
    void set_nwk_reg_status_psd(int status);
    void set_nwk_reg_status_eps(int status);
    void set_rat(int AcTStatus);
    bool get_iccid();
    bool get_imsi();
    bool get_imei();
    bool get_meid();
    void parser_abort_cb();
    void CMX_ERROR_URC();
    void CREG_URC();
    void CGREG_URC();
    void CEREG_URC();
    void UMWI_URC();
};

#endif //_UBLOX_CELLULAR_GENERIC_BASE_

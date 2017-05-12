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

#include "modem_api.h"
#include "UbloxCellularGenericBase.h"
#include "BufferedSerial.h"
#include "APN_db.h"
#if defined(FEATURE_COMMON_PAL)
#include "mbed_trace.h"
#define TRACE_GROUP "UCID"
#else
#define tr_debug(...) (void(0)) // dummies if feature common pal is not added
#define tr_info(...)  (void(0)) // dummies if feature common pal is not added
#define tr_error(...) (void(0)) // dummies if feature common pal is not added
#endif

/**********************************************************************
 * MACROS
 **********************************************************************/

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

/**********************************************************************
 * STATIC VARIABLES
 **********************************************************************/

// Modem object for the device
static modem_t mdm_object;

/**********************************************************************
 * PRIVATE METHODS
 **********************************************************************/

void UbloxCellularGenericBase::set_nwk_reg_status_csd(unsigned int status)
{
    switch (status) {
        case CSD_NOT_REGISTERED_NOT_SEARCHING:
        case CSD_NOT_REGISTERED_SEARCHING:
            tr_debug("Not registered for circuit switched service");
            break;
        case CSD_REGISTERED:
        case CSD_REGISTERED_ROAMING:
            tr_debug("Registered for circuit switched service");
            break;
        case CSD_REGISTRATION_DENIED:
            tr_debug("Circuit switched service denied");
            break;
        case CSD_UNKNOWN_COVERAGE:
            tr_debug("Out of circuit switched service coverage");
            break;
        case CSD_SMS_ONLY:
            tr_debug("SMS service only");
            break;
        case CSD_SMS_ONLY_ROAMING:
            tr_debug("SMS service only");
            break;
        case CSD_CSFB_NOT_PREFERRED:
            tr_debug("Registered for circuit switched service with CSFB not preferred");
            break;
        default:
            tr_debug("Unknown circuit switched service registration status. %u", status);
            break;
    }

    _dev_info->reg_status_csd = static_cast<nwk_registration_status_csd>(status);
}

void UbloxCellularGenericBase::set_nwk_reg_status_psd(unsigned int status)
{
    switch (status) {
        case PSD_NOT_REGISTERED_NOT_SEARCHING:
        case PSD_NOT_REGISTERED_SEARCHING:
            tr_debug("Not registered for packet switched service");
            break;
        case PSD_REGISTERED:
        case PSD_REGISTERED_ROAMING:
            tr_debug("Registered for packet switched service");
            break;
        case PSD_REGISTRATION_DENIED:
            tr_debug("Packet switched service denied");
            break;
        case PSD_UNKNOWN_COVERAGE:
            tr_debug("Out of packet switched service coverage");
            break;
        case PSD_EMERGENCY_SERVICES_ONLY:
            tr_debug("Limited access for packet switched service. Emergency use only.");
            break;
        default:
            tr_debug("Unknown packet switched service registration status. %u", status);
            break;
    }

    _dev_info->reg_status_psd = static_cast<nwk_registration_status_psd>(status);
}

void UbloxCellularGenericBase::set_nwk_reg_status_eps(unsigned int status)
{
    switch (status) {
        case EPS_NOT_REGISTERED_NOT_SEARCHING:
        case EPS_NOT_REGISTERED_SEARCHING:
            tr_debug("Not registered for EPS service");
            break;
        case EPS_REGISTERED:
        case EPS_REGISTERED_ROAMING:
            tr_debug("Registered for EPS service");
            break;
        case EPS_REGISTRATION_DENIED:
            tr_debug("EPS service denied");
            break;
        case EPS_UNKNOWN_COVERAGE:
            tr_debug("Out of EPS service coverage");
            break;
        case EPS_EMERGENCY_SERVICES_ONLY:
            tr_debug("Limited access for EPS service. Emergency use only.");
            break;
        default:
            tr_debug("Unknown EPS service registration status. %u", status);
            break;
    }

    _dev_info->reg_status_eps = static_cast<nwk_registration_status_eps>(status);
}

void UbloxCellularGenericBase::set_rat(unsigned int AcTStatus)
{
    switch (AcTStatus) {
        case GSM:
        case COMPACT_GSM:
            tr_debug("Connected to RAT. GSM");
            break;
        case UTRAN:
            tr_debug("Connected to RAT. UTRAN");
            break;
        case EDGE:
            tr_debug("Connected to RAT. EDGE");
            break;
        case HSDPA:
            tr_debug("Connected to RAT. HSDPA");
            break;
        case HSUPA:
            tr_debug("Connected to RAT. HSPA");
            break;
        case HSDPA_HSUPA:
            tr_debug("Connected to RAT. HDPA/HSPA");
            break;
        case LTE:
            tr_debug("Connected to RAT. LTE");
            break;
        default:
            tr_debug("Unknown RAT. %u", AcTStatus);
            break;
    }

    _dev_info->rat = static_cast<radio_access_nwk_type>(AcTStatus);
}

bool UbloxCellularGenericBase::get_iccid()
{
    bool success;
    LOCK();

    // Returns the ICCID (Integrated Circuit Card ID) of the SIM-card.
    // ICCID is a serial number identifying the SIM.
    // AT Command Manual UBX-13002752, section 4.12
    success = _at->send("AT+CCID") && _at->recv("+CCID: %20[^\n]\nOK\n", _dev_info->iccid);
    tr_debug("DevInfo: CCID=%s", _dev_info->iccid);

    UNLOCK();
    return success;
}

bool UbloxCellularGenericBase::get_imsi()
{
    bool success;
    LOCK();

    // International mobile subscriber identification
    // AT Command Manual UBX-13002752, section 4.11
    success = _at->send("AT+CIMI") && _at->recv("%15[^\n]\nOK\n", _dev_info->imsi);
    tr_debug("DevInfo: IMSI=%s", _dev_info->imsi);

    UNLOCK();
    return success;
}

bool UbloxCellularGenericBase::get_imei()
{
    bool success;
    LOCK();

    // International mobile equipment identifier
    // AT Command Manual UBX-13002752, section 4.7
    success = _at->send("AT+CGSN") && _at->recv("%15[^\n]\nOK\n", _dev_info->imei);
    tr_debug("DevInfo: IMEI=%s", _dev_info->imei);

    UNLOCK();
    return success;
}

bool UbloxCellularGenericBase::get_meid()
{
    bool success;
    LOCK();

    // Mobile equipment identifier
    // AT Command Manual UBX-13002752, section 4.8
    success = _at->send("AT+GSN") && _at->recv("%18[^\n]\nOK\n", _dev_info->meid);
    tr_debug("DevInfo: MEID=%s", _dev_info->meid);

    UNLOCK();
    return success;
}

void UbloxCellularGenericBase::parser_abort_cb()
{
    _at->abort();
}

/**********************************************************************
 * PROTECTED METHODS
 **********************************************************************/

// Power up the modem.
// Enables the GPIO lines to the modem and then wriggles the power line in short pulses.
bool UbloxCellularGenericBase::power_up_modem()
{
    bool success = false;
    LOCK();

    /* Initialize GPIO lines */
    tr_debug("Powering up modem...");
    modem_init(&mdm_object);
    /* Give modem a little time to settle down */
    wait_ms(250);

    for (int retry_count = 0; !success && (retry_count < 20); retry_count++) {
        modem_power_up(&mdm_object);
        wait_ms(500);
        // Modem tends to spit out noise during power up - don't confuse the parser
        _at->flush();
        _at->set_timeout(1000);

        if (_at->send("AT") && _at->recv("OK")) {
            success = true;
        }
    }

    _at->set_timeout(8000);

    if (success) {
        success = _at->send("ATE0;" // Turn off modem echoing
                            "+CMEE=2;" // Turn on verbose responses
                            "&K0" //turn off RTC/CTS handshaking
                            "+IPR=%d;" // Set baud rate
                            "&C1;"  // Set DCD circuit(109), changes in accordance with the carrier detect status
                            "&D0", MBED_CONF_UBLOX_CELL_GEN_DRV_BAUD_RATE) && // Set DTR circuit, we ignore the state change of DTR
                  _at->recv("OK");
    }

    if (!success) {
        tr_error("Preliminary modem setup failed.");
    }

    UNLOCK();
    return success;
}

// Power down modem via AT interface.
void UbloxCellularGenericBase::power_down_modem()
{
    LOCK();

    // If possible, do a soft power-off first
    if (_at) {
        _at->send("AT+CPWROFF") && _at->recv("OK");
    }

    // Now do a hard power-off
    modem_power_down(&mdm_object);
    modem_deinit(&mdm_object);

    UNLOCK();
}

// Perform registration.
bool UbloxCellularGenericBase::nwk_registration(device_type dev)
{
    bool registered = false;
    LOCK();

    // Enable the packet switched and network registration unsolicited result codes
    if (_at->send("AT+CGREG=0;+CREG=0") && _at->recv("OK")) {
        // For Operator selection, we should have a longer timeout.
        // According to Ublox AT Manual UBX-13002752, it could be 3 minutes
        for (int waitSeconds = 0; !registered && (waitSeconds < 180); waitSeconds++) {
            if (nwk_registration_status(dev)) {
                registered = is_registered_psd() || is_registered_csd() || is_registered_eps();
            }
            wait_ms (1000);
        }
    }

    UNLOCK();
    return registered;
}

// Check for both circuit switched and packet switched registration status,
// returning true if we are able to update the status variables.
bool UbloxCellularGenericBase::nwk_registration_status(device_type dev)
{
    int successCount = 0;
    char str[35];
    unsigned int status;
    LOCK();

    // URC's have been disabled by choice, so we are expecting
    // +CREG/+CGREG/+CEREG: <n>,<stat> where n will always be zero and stat is
    // the registration status for circuit switched, packet switched and EPS
    // service respectively.
    if (_at->send("AT+CREG?") && _at->recv("+CREG: %34[^\n]\n", str)) {
        if (sscanf(str, "%*u,%u", &status) >= 1) {
            set_nwk_reg_status_csd(status);
            successCount++;
        }
    }

    if (_at->send("AT+CGREG?") && _at->recv("+CGREG: %34[^\n]\n", str)) {
        if (sscanf(str, "%*u,%u", &status) >= 1) {
            set_nwk_reg_status_psd(status);
            successCount++;
        }
    }

    if ((dev == DEV_TOBY_L2) ||  (dev == DEV_MPCI_L2)) {
        if (_at->send("AT+CEREG?") && _at->recv("+CEREG: %34[^\n]\n", str)) {
            if (sscanf(str, "%*u,%u", &status) >= 1) {
                set_nwk_reg_status_eps(status);
                successCount++;
            }
        }
    } else {
        successCount++;
    }

    /* Determine the  type from AT+COPS, if possible */
    if ((is_registered_csd() || is_registered_psd() || is_registered_eps()) &&
       _at->send("AT+COPS?") && _at->recv("+COPS: %34[^\n]\n", str)) {
        if (sscanf(str, "%*u,%*u,\"%*[^\"]\",%u", &status) >= 1) {
            set_rat(status);
        }
    }

    UNLOCK();
    return (successCount >= 3);
}

// Get the device ID.
bool UbloxCellularGenericBase::set_device_identity(device_type *dev)
{
    char buf[20];
    bool success;
    LOCK();

    success = _at->send("ATI") && _at->recv("%19[^\n]\nOK\n", buf);

    if (success) {
        if (strstr(buf, "SARA-G35"))
            *dev = DEV_SARA_G35;
        else if (strstr(buf, "LISA-U200-03S"))
            *dev = DEV_LISA_U2_03S;
        else if (strstr(buf, "LISA-U2"))
            *dev = DEV_LISA_U2;
        else if (strstr(buf, "SARA-U2"))
            *dev = DEV_SARA_U2;
        else if (strstr(buf, "LEON-G2"))
            *dev = DEV_LEON_G2;
        else if (strstr(buf, "TOBY-L2"))
            *dev = DEV_TOBY_L2;
        else if (strstr(buf, "MPCI-L2"))
            *dev = DEV_MPCI_L2;
    }

    UNLOCK();
    return success;
}

// Send initialisation AT commands that are specific to the device.
bool UbloxCellularGenericBase::device_init(device_type dev)
{
    bool success = false;
    LOCK();

    if ((dev == DEV_LISA_U2) || (dev == DEV_LEON_G2) || (dev == DEV_TOBY_L2)) {
        success = _at->send("AT+UGPIOC=20,2") && _at->recv("OK");
    } else if ((dev == DEV_SARA_U2) || (dev == DEV_SARA_G35)) {
        success = _at->send("AT+UGPIOC=16,2") && _at->recv("OK");
    } else {
        success = true;
    }

    UNLOCK();
    return success;
}

// Get the SIM card going.
bool UbloxCellularGenericBase::initialise_sim_card()
{
    bool success = false;
    int retry_count = 0;
    bool done = false;
    LOCK();

    /* SIM initialisation may take a significant amount, so an error is
     * kind of expected. We should retry 10 times until we succeed or timeout. */
    for (retry_count = 0; !done && (retry_count < 10); retry_count++) {
        char pinstr[16];

        if (_at->send("AT+CPIN?") && _at->recv("+CPIN: %15[^\n]\nOK\n", pinstr)) {
            done = true;
            if (strcmp(pinstr, "SIM PIN") == 0) {
                _sim_pin_check_enabled = true;
                if (_at->send("AT+CPIN=\"%s\"", _pin) && _at->recv("OK")) {
                    tr_debug("PIN correct");
                    success = true;
                }
            } else if (strcmp(pinstr, "READY") == 0) {
                _sim_pin_check_enabled = false;
                tr_debug("No PIN required");
                success = true;
            } else {
                tr_debug("Unexpected response from SIM: \"%s\"", pinstr);
            }
        }

        /* wait for a second before retry */
        wait_ms(1000);
    }

    if (!done) {
        tr_error("SIM not ready.");
    }

    UNLOCK();
    return success;
}

bool UbloxCellularGenericBase::is_registered_csd()
{
  return (_dev_info->reg_status_csd == CSD_REGISTERED) ||
          (_dev_info->reg_status_csd == CSD_REGISTERED_ROAMING) ||
          (_dev_info->reg_status_csd == CSD_CSFB_NOT_PREFERRED);
}

bool UbloxCellularGenericBase::is_registered_psd()
{
    return (_dev_info->reg_status_psd == PSD_REGISTERED) ||
            (_dev_info->reg_status_psd == PSD_REGISTERED_ROAMING);
}

bool UbloxCellularGenericBase::is_registered_eps()
{
    return (_dev_info->reg_status_eps == EPS_REGISTERED) ||
            (_dev_info->reg_status_eps == EPS_REGISTERED_ROAMING);
}

/**********************************************************************
 * PUBLIC METHODS
 **********************************************************************/

// Constructor.
UbloxCellularGenericBase::UbloxCellularGenericBase(bool debug_on, PinName tx,
                                                   PinName rx, int baud)
{
    _pin = NULL;
    _at = NULL;
    _fh = NULL;
    _debug_trace_on = false;
    _modem_initialised = false;
    _sim_pin_check_enabled = false;
    _debug_trace_on = debug_on;

    // Set up File Handle for buffered serial comms with cellular module
    // (which will be used by the AT parser)
    _fh = new BufferedSerial(tx, rx, baud);

    _dev_info = new device_info;
    _dev_info->dev = DEV_TYPE_NONE;
    _dev_info->reg_status_csd = CSD_NOT_REGISTERED_NOT_SEARCHING;
    _dev_info->reg_status_psd = PSD_NOT_REGISTERED_NOT_SEARCHING;
    _dev_info->reg_status_eps = EPS_NOT_REGISTERED_NOT_SEARCHING;

    // Set up the AT parser
    _at = new ATParser(_fh, OUTPUT_ENTER_KEY, AT_PARSER_BUFFER_SIZE,
                       AT_PARSER_TIMEOUT,
                       _debug_trace_on ? true : false);

    // Error cases, out of band handling
    _at->oob("ERROR", callback(this, &UbloxCellularGenericBase::parser_abort_cb));
    _at->oob("+CME ERROR", callback(this, &UbloxCellularGenericBase::parser_abort_cb));
    _at->oob("+CMS ERROR", callback(this, &UbloxCellularGenericBase::parser_abort_cb));
}

// Destructor.
UbloxCellularGenericBase::~UbloxCellularGenericBase()
{
    deinit();
    delete _dev_info;
    delete _fh;
}

// Initialise the modem.
bool UbloxCellularGenericBase::init(const char *pin)
{
    LOCK();

    if (!_modem_initialised) {
        if (power_up_modem()) {
            if (pin != NULL) {
                _pin = pin;
            }
            if (initialise_sim_card()) {
                if (set_device_identity(&_dev_info->dev) && // Set up device identity
                    device_init(_dev_info->dev) && // Initialise this device
                    get_iccid() && // Get integrated circuit ID of the SIM
                    get_imsi() && // Get international mobile subscriber information
                    get_imei() && // Get international mobile equipment identifier
                    get_meid()) { // Probably the same as the IMEI

                    // The modem is initialised.
                    _modem_initialised = true;
                }
            }
        }
    }

    UNLOCK();
    return _modem_initialised;
}

// Put the modem into its lowest power state.
void UbloxCellularGenericBase::deinit()
{
    power_down_modem();

    delete _at;
    _at = NULL;

    _modem_initialised = false;
}

// Set the PIN.
void UbloxCellularGenericBase::set_pin(const char *pin) {
    _pin = pin;
}

// Enable or disable SIM pin check lock.
bool UbloxCellularGenericBase::check_pin(bool check)
{
    bool success = false;;
    LOCK();

    if (_pin != NULL) {
        if (_sim_pin_check_enabled && !check) {
            // Disable the SIM lock
            if (_at->send("AT+CLCK=\"SC\",0,\"%s\"", _pin) && _at->recv("OK")) {
                _sim_pin_check_enabled = false;
                success = true;
            }
        } else if (!_sim_pin_check_enabled && check) {
            // Enable the SIM lock
            if (_at->send("AT+CLCK=\"SC\",1,\"%s\"", _pin) && _at->recv("OK")) {
                _sim_pin_check_enabled = true;
                success = true;
            }
        } else {
            success = true;
        }
    }

    UNLOCK();
    return success;
}

// Change the pin code for the SIM card.
bool UbloxCellularGenericBase::change_pin(const char *pin)
{
    bool success = false;;
    LOCK();

    // Change the SIM pin
    if ((pin != NULL) && (_pin != NULL)) {
        if (_at->send("AT+CPWD=\"SC\",\"%s\",\"%s\"", _pin, pin) && _at->recv("OK")) {
            _pin = pin;
            success = true;
        }
    }

    UNLOCK();
    return success;
}

// End of File

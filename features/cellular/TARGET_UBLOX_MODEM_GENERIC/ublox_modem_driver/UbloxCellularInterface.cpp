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

#include "ublox_low_level_api.h"
#include "UbloxCellularInterface.h"
#include "nsapi_ppp.h"
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

#define AT_PARSER_BUFFER_SIZE           256
#define AT_PARSER_TIMEOUT_MILLISECONDS  8 * 1000

/**********************************************************************
 * STATIC VARIABLES
 **********************************************************************/

// A callback function that will be called if the PPP connection goes down.
void (*callback_fptr)(nsapi_error_t);

// The ppp_connection_up() callback, which goes out via the PPP stack and has
// to be a static function, needs access to the member variable
// _dev_info->ppp_connection_up, so we need to keep a pointer to it here.
volatile bool * ppp_connection_up_ptr = NULL;

/**********************************************************************
 * STATIC CALLBACKS
 **********************************************************************/

static void ppp_connection_down_cb(nsapi_error_t err)
{
    if (ppp_connection_up_ptr != NULL) {
        *ppp_connection_up_ptr = false;
    }

    if (callback_fptr != NULL) {
        callback_fptr(err);
    }
}

/**********************************************************************
 * PRIVATE METHODS: GENERAL
 **********************************************************************/

// Start an instance of the AT parser.
void UbloxCellularInterface::setup_at_parser()
{
    if (_at == NULL) {
        _at = new ATParser(*_fh, AT_PARSER_BUFFER_SIZE, AT_PARSER_TIMEOUT_MILLISECONDS,
                             _debug_trace_on ? true : false);

        // Error cases, out of band handling
        _at->oob("ERROR", this, &UbloxCellularInterface::parser_abort);
        _at->oob("+CME ERROR", this, &UbloxCellularInterface::parser_abort);
        _at->oob("+CMS ERROR", this, &UbloxCellularInterface::parser_abort);

        // URCs, handled out of band
        _at->oob("+CMT", this, &UbloxCellularInterface::CMT_URC);
        _at->oob("+CMTI", this, &UbloxCellularInterface::CMTI_URC);
    }
}

// Free the AT parser.
void UbloxCellularInterface::shutdown_at_parser()
{
    delete _at;
    _at = NULL;
}

// Get the device ID.
bool UbloxCellularInterface::set_device_identity(device_type *dev)
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
        else if (strstr(buf, "LISA-C2"))
            *dev = DEV_LISA_C2;
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
bool UbloxCellularInterface::device_init(device_type dev)
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
nsapi_error_t UbloxCellularInterface::initialise_sim_card()
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_AUTH_FAILURE;
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
                    nsapi_error = NSAPI_ERROR_OK;
                }
            } else if (strcmp(pinstr, "READY") == 0) {
                _sim_pin_check_enabled = false;
                tr_debug("No PIN required");
                nsapi_error = NSAPI_ERROR_OK;
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
    return nsapi_error;
}

// Send the credentials to the modem.
nsapi_error_t UbloxCellularInterface::setup_context_and_credentials()
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_PARAMETER;
    const char *auth = _uname && _pwd ? "CHAP:" : "";
    LOCK();

    if (_apn) {
        // TODO: IPV6
        if (_at->send("AT+CGDCONT=1,\"%s\",\"%s%s\"", "IP", auth, _apn)
                   && _at->recv("OK")) {
            nsapi_error = NSAPI_ERROR_OK;
        }
    }

    UNLOCK();
    return nsapi_error;

}

// Perform registration.
bool UbloxCellularInterface::nwk_registration(device_type dev)
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
bool UbloxCellularInterface::nwk_registration_status(device_type dev)
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

    /* Determine the network type from AT+COPS, if possible */
    if ((is_registered_csd() || is_registered_psd() || is_registered_eps()) &&
       _at->send("AT+COPS?") && _at->recv("+COPS: %34[^\n]\n", str)) {
        if (sscanf(str, "%*u,%*u,\"%*[^\"]\",%u", &status) >= 1) {
            set_RAT(status);
        }
    }

    UNLOCK();
    return (successCount >= 3);
}

// Power up the modem.
// Enables the GPIO lines to the modem and then wriggles the power line in short pulses.
bool UbloxCellularInterface::power_up_modem()
{
    bool success = false;
    DigitalOut pwrOn(MDMPWRON, 1);
    DigitalIn cts(MDMCTS);

    LOCK();

    // Initialise GPIO lines
    tr_debug("Powering up modem...");
    ublox_mdm_power_on(_useUSB);
    wait_ms(500);

    // The power-on call takes the module out of reset, here we toggle the power-on
    // PIN to do the actual waking up, see Sara-U2_DataSheet_(UBX-13005287).pdf section
    // 4.2.6.
    for (int retry_count = 0; !success && (retry_count < 20); retry_count++) {
        pwrOn = 0;
        wait_us(50);
        pwrOn = 1;
        wait_ms(10);

        while (cts) {
            wait_ms(500);
        }

        // Modem tends to spit out noise during power up - don't confuse the parser
        _at->flush();
        _at->setTimeout(1000);

        if (_at->send("AT") && _at->recv("OK")) {
            success = true;
        }
    }

    _at->setTimeout(8000);

    if (success) {
        // For more details regarding DCD and DTR circuitry, please refer to SARA-U2 System integration manual
        // and Ublox AT commands manual
        success = _at->send("ATE0;" // Turn off modem echoing
                            "+CMEE=2;" // Turn on verbose responses
                            "+IPR=%d;" // Set baud rate
                            "&C1;"  // Set DCD circuit(109), changes in accordance with the carrier detect status
                            "&D0", MBED_CONF_UBLOX_MODEM_GENERIC_BAUD_RATE) && // Set DTR circuit, we ignore the state change of DTR
                  _at->recv("OK");
    }

    if (!success) {
        tr_error("Preliminary modem setup failed.");
    }

    UNLOCK();
    return success;
}

// Power down modem via AT interface.
void UbloxCellularInterface::power_down_modem()
{
    LOCK();

    // If possible, do a soft power-off first
    if (_at) {
        _at->send("AT+CPWROFF") && _at->recv("OK");
    }

    // Now do a hard power-off
    ublox_mdm_power_off();

    UNLOCK();
}

bool UbloxCellularInterface::is_registered_csd()
{
  return (_dev_info->reg_status_csd == CSD_REGISTERED) ||
          (_dev_info->reg_status_csd == CSD_REGISTERED_ROAMING) ||
          (_dev_info->reg_status_csd == CSD_CSFB_NOT_PREFERRED);
}

bool UbloxCellularInterface::is_registered_psd()
{
    return (_dev_info->reg_status_psd == PSD_REGISTERED) ||
            (_dev_info->reg_status_psd == PSD_REGISTERED_ROAMING);
}

bool UbloxCellularInterface::is_registered_eps()
{
    return (_dev_info->reg_status_eps == EPS_REGISTERED) ||
            (_dev_info->reg_status_eps == EPS_REGISTERED_ROAMING);
}

void UbloxCellularInterface::set_nwk_reg_status_csd(unsigned int status)
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

void UbloxCellularInterface::set_nwk_reg_status_psd(unsigned int status)
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

void UbloxCellularInterface::set_nwk_reg_status_eps(unsigned int status)
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

void UbloxCellularInterface::set_RAT(unsigned int AcTStatus)
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

bool UbloxCellularInterface::get_CCID()
{
    bool success;
    LOCK();

    // Returns the ICCID (Integrated Circuit Card ID) of the SIM-card.
    // ICCID is a serial number identifying the SIM.
    // AT Command Manual UBX-13002752, section 4.12
    success = _at->send("AT+CCID") && _at->recv("+CCID: %20[^\n]\nOK\n", _dev_info->ccid);
    tr_debug("DevInfo: CCID=%s", _dev_info->ccid);

    UNLOCK();
    return success;
}

bool UbloxCellularInterface::get_IMSI()
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

bool UbloxCellularInterface::get_IMEI()
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

bool UbloxCellularInterface::get_MEID()
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

bool UbloxCellularInterface::set_CMGF()
{
    bool success;
    LOCK();

    // Preferred message format
    // AT Command Manual UBX-13002752, section 11.4
    // set AT+CMGF=[mode] , 0 PDU mode, 1 text mode
    success = _at->send("AT+CMGF=1") && _at->recv("OK");

    UNLOCK();
    return success;
}

bool UbloxCellularInterface::set_CNMI()
{
    bool success;
    LOCK();

    // New SMS indication configuration
    // AT Command Manual UBX-13002752, section 11.8
    // set AT+CMTI=[mode, index] , 0 PDU mode, 1 text mode
    // Multiple URCs for SMS, i.e., CMT, CMTI, UCMT, CBMI, CDSI as DTE could be following any of these SMS formats
    success = _at->send("AT+CNMI=2,1") && _at->recv("OK");

    UNLOCK();
    return success;
}

// Enter data mode.
bool UbloxCellularInterface::set_ATD()
{
    bool success;
    LOCK();

    success = _at->send("ATD*99***1#") && _at->recv("CONNECT");

    UNLOCK();
    return success;
}

// Enable or disable SIM pin check lock.
// NOTE: the AT interface must be locked before this is called
nsapi_error_t UbloxCellularInterface::do_add_remove_sim_pin_check(bool pin_check_disabled)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_AUTH_FAILURE;

    if (_pin != NULL) {
        if (_sim_pin_check_enabled && pin_check_disabled) {
            // Disable the SIM lock
            if (_at->send("AT+CLCK=\"SC\",0,\"%s\"", _pin) && _at->recv("OK")) {
                _sim_pin_check_enabled = false;
                nsapi_error = NSAPI_ERROR_OK;
            }
        } else if (!_sim_pin_check_enabled && !pin_check_disabled) {
            // Enable the SIM lock
            if (_at->send("AT+CLCK=\"SC\",1,\"%s\"", _pin) && _at->recv("OK")) {
                _sim_pin_check_enabled = true;
                nsapi_error = NSAPI_ERROR_OK;
            }
        } else {
            nsapi_error = NSAPI_ERROR_OK;
        }
    }

    return nsapi_error;
}

// Change the pin code for the SIM card.
// NOTE: the AT interface must be locked before this is called
nsapi_error_t UbloxCellularInterface::do_change_sim_pin(const char *new_pin)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_AUTH_FAILURE;

    // Change the SIM pin
    if ((new_pin != NULL) && (_pin != NULL)) {
        if (_at->send("AT+CPWD=\"SC\",\"%s\",\"%s\"", _pin, new_pin) && _at->recv("OK")) {
            _pin = new_pin;
            nsapi_error = NSAPI_ERROR_OK;
        }
    }

    return nsapi_error;
}

/**********************************************************************
 * PRIVATE METHODS: CALLBACKS
 **********************************************************************/

void UbloxCellularInterface::parser_abort()
{
    _at->abort();
}

void UbloxCellularInterface::CMTI_URC()
{
    // our CMGF = 1, i.e., text mode. So we expect response in this format:
    //+CMTI: <mem>,<index>,
    //AT Command Manual UBX-13002752, section 11.8.2
    _at->recv(": %*u,%*u");
    tr_info("New SMS received");
}

void UbloxCellularInterface::CMT_URC()
{
    // our CMGF = 1, i.e., text mode. So we expect response in this format:
    //+CMT: <oa>,[<alpha>],<scts>[,<tooa>,
    //<fo>,<pid>,<dcs>,<sca>,<tosca>,
    //<length>]<CR><LF><data>
    // By default detailed SMS header CSDH=0 , so we are not expecting  [,<tooa>,
    //<fo>,<pid>,<dcs>,<sca>,<tosca>
    //AT Command Manual UBX-13002752, section 11.8.2
    char sms[50];
    char service_timestamp[15];

    _at->recv(": %49[^\"]\",,%14[^\"]\"\n", sms, service_timestamp);

    tr_info("SMS:%s, %s", service_timestamp, sms);
}

/**********************************************************************
 * PROTECTED METHODS
 **********************************************************************/

// Gain access to the underlying network stack.
NetworkStack *UbloxCellularInterface::get_stack()
{
    return nsapi_ppp_get_stack();
}

/**********************************************************************
 * PUBLIC METHODS
 **********************************************************************/

// Constructor.
UbloxCellularInterface::UbloxCellularInterface(bool debugOn, PinName tx, PinName rx, int baud, bool use_USB)
{
    _pin = NULL;
    _at = NULL;
    _apn = "internet";
    _uname = NULL;
    _pwd = NULL;
    _useUSB = use_USB;
    _debug_trace_on = false;
    _modem_initialised = false;
    _sim_pin_check_enabled = false;
    _sim_pin_check_change_pending = false;
    _sim_pin_check_change_pending_disabled_value = false;
    _sim_pin_change_pending = false;
    _sim_pin_change_pending_new_pin_value = NULL;

    // USB is not currently supported
    MBED_ASSERT(!use_USB);

     // Set up File Handle
    _fh = new BufferedSerial(tx, rx, baud);

    if (debugOn) {
         _debug_trace_on = true;
     }

    _dev_info = new device_info;
    _dev_info->dev = DEV_TYPE_NONE;
    _dev_info->reg_status_csd = CSD_NOT_REGISTERED_NOT_SEARCHING;
    _dev_info->reg_status_psd = PSD_NOT_REGISTERED_NOT_SEARCHING;
    _dev_info->reg_status_eps = EPS_NOT_REGISTERED_NOT_SEARCHING;
    _dev_info->ppp_connection_up = false;

    ppp_connection_up_ptr = &(_dev_info->ppp_connection_up);
}

// Destructor.
UbloxCellularInterface::~UbloxCellularInterface()
{
    deinit();

    delete _fh;
    delete _dev_info;
    ppp_connection_up_ptr = NULL;
}

// Set the callback to be called in case the connection is lost.
void UbloxCellularInterface::connection_lost_notification_cb(void (*fptr)(nsapi_error_t))
{
    callback_fptr = fptr;
}

// Initialise the modem
nsapi_error_t UbloxCellularInterface::init(const char *sim_pin)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_OK;
    const char * apn_config;
    LOCK();

    if (!_modem_initialised) {
        // Assume we won't make it...
        nsapi_error = NSAPI_ERROR_DEVICE_ERROR;

        // Setup AT parser
        setup_at_parser();

        // If we are using serial interface, we want to make sure that DCD line is
        // not connected as long as we are using ATParser.
        // As soon as we get into data mode, we would like to attach an interrupt line
        // to DCD line.
        // Here, we detach the line
        if (!_useUSB) {
            BufferedSerial *serial = static_cast<BufferedSerial *>(_fh);
            serial->set_data_carrier_detect(NC);
        }

        if (power_up_modem()) {
            if (sim_pin != NULL) {
                _pin = sim_pin;
            }
            nsapi_error = initialise_sim_card();
            if (nsapi_error == NSAPI_ERROR_OK) {
                if (set_device_identity(&_dev_info->dev) && // Set up device identity
                    device_init(_dev_info->dev) && // Initialise this device
                    get_CCID()  && // Get integrated circuit ID of the SIM
                    get_IMSI()  && // Get international mobile subscriber information
                    get_IMEI()  && // Get international mobile equipment identifier
                    get_MEID()  && // Probably the same as the IMEI
                    set_CMGF()  && // Set message format for SMS
                    set_CNMI()) { // Set new SMS indication

                    // The modem is initialised.  The following checks my still fail,
                    // of course, but they are all of a "fatal" nature and so we wouldn't
                    // want to retry them anyway
                    _modem_initialised = true;

                    // If the caller hasn't entered an APN, try to find it
                    if (_apn == NULL) {
                        apn_config = apnconfig(_dev_info->imsi);
                        if (apn_config) {
                            _apn    = _APN_GET(apn_config);
                            _uname  = _APN_GET(apn_config);
                            _pwd    = _APN_GET(apn_config);
                        }

                        _apn    = _apn     ?  _apn    : NULL;
                        _uname  = _uname   ?  _uname  : NULL;
                        _pwd    = _pwd     ?  _pwd    : NULL;
                    }

                    // Set up APN and IP protocol for external PDP context
                    nsapi_error = setup_context_and_credentials();
                } else {
                    nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
                }
            }
        }
    }

    UNLOCK();
    return nsapi_error;
}

// Put the modem into its lowest power state
void UbloxCellularInterface::deinit()
{
    if (_dev_info->ppp_connection_up) {
        disconnect();
    }

    power_down_modem();
    shutdown_at_parser();

    _modem_initialised = false;
}

// Set APN, username and password
void  UbloxCellularInterface::set_credentials(const char *apn, const char *uname,
                                              const char *pwd)
{
    _apn = apn;
    _uname = uname;
    _pwd = pwd;
}

void UbloxCellularInterface::set_SIM_pin(const char *pin) {
    /* overwrite the default PIN by user provided PIN */
    _pin = pin;
}

// Make a cellular connection
nsapi_error_t UbloxCellularInterface::connect(const char *sim_pin, const char *apn, const char *uname, const char *pwd)
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

// Make a cellular connection
nsapi_error_t UbloxCellularInterface::connect()
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_IS_CONNECTED;

    if (!_dev_info->ppp_connection_up) {

        // Set up modem and then register with the network
        nsapi_error = init();
        if (nsapi_error == NSAPI_ERROR_OK) {

            // Perform any pending SIM actions
            if (_sim_pin_check_change_pending) {
                nsapi_error = do_add_remove_sim_pin_check(_sim_pin_check_change_pending_disabled_value);
                _sim_pin_check_change_pending = false;
            }
            if (_sim_pin_change_pending) {
                nsapi_error = do_change_sim_pin(_sim_pin_change_pending_new_pin_value);
                _sim_pin_change_pending = false;
            }

            if (nsapi_error == NSAPI_ERROR_OK) {
                nsapi_error = NSAPI_ERROR_NO_CONNECTION;
                for (int retries = 0; (nsapi_error == NSAPI_ERROR_NO_CONNECTION) && (retries < 3); retries++) {
                    if (nwk_registration(_dev_info->dev)) {
                        nsapi_error = NSAPI_ERROR_OK;
                    }
                }
            }
        }

        if (nsapi_error == NSAPI_ERROR_OK) {
            // Attempt to enter data mode
            if (set_ATD()) {
                // Here we would like to attach an interrupt line to the DCD PIN
                // We cast back the serial interface from the file handle and set
                // the DCD line.
                if(!_useUSB) {
                    BufferedSerial *serial = static_cast<BufferedSerial *>(_fh);
                    serial->set_data_carrier_detect(MDMDCD);
                }

                // Initialise PPP
                // mbed_ppp_init() is a blocking call, it will block until
                // connected, or timeout after 30 seconds
                for (int retries = 0; !_dev_info->ppp_connection_up && (retries < 3); retries++) {
                    nsapi_error = nsapi_ppp_connect(_fh, ppp_connection_down_cb, _uname, _pwd);
                    _dev_info->ppp_connection_up = (nsapi_error == NSAPI_ERROR_OK);
                }
            } else {
                nsapi_error = NSAPI_ERROR_NO_CONNECTION;
            }
        }

        // If we were unable to connect, power down the modem
        if (!_dev_info->ppp_connection_up) {
            power_down_modem();
        }
    }

    return nsapi_error;
}

// User initiated disconnect.
nsapi_error_t UbloxCellularInterface::disconnect()
{
    nsapi_error_t nsapi_error = nsapi_ppp_disconnect(_fh);

    // Get the "NO CARRIER" response out of the modem
    // so as not to confuse subsequent AT commands
    if (_at) {
        _at->send("AT") && _at->recv("NO CARRIER");
    }

    return nsapi_error;
}

// Enable or disable SIM PIN check lock.
nsapi_error_t UbloxCellularInterface::add_remove_sim_pin_check(bool pin_check_disabled, bool immediate, const char *sim_pin)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_AUTH_FAILURE;
    LOCK();

    if (sim_pin != NULL) {
        _pin = sim_pin;
    }

    if (immediate) {
        nsapi_error = init();
        if (nsapi_error == NSAPI_ERROR_OK) {
            nsapi_error = do_add_remove_sim_pin_check(pin_check_disabled);
        }
    } else {
        nsapi_error = NSAPI_ERROR_OK;
        _sim_pin_check_change_pending = true;
        _sim_pin_check_change_pending_disabled_value = pin_check_disabled;
    }

    UNLOCK();
    return nsapi_error;
}

// Change the PIN code for the SIM card.
nsapi_error_t UbloxCellularInterface::change_sim_pin(const char *new_pin, bool immediate, const char *old_pin)
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_AUTH_FAILURE;
    LOCK();

    if (old_pin != NULL) {
        _pin = old_pin;
    }

    if (immediate) {
        nsapi_error = init();
        if (nsapi_error == NSAPI_ERROR_OK) {
            nsapi_error = do_change_sim_pin(new_pin);
        }
    } else {
        nsapi_error = NSAPI_ERROR_OK;
        _sim_pin_change_pending = true;
        _sim_pin_change_pending_new_pin_value = new_pin;
    }

    UNLOCK();
    return nsapi_error;
}

// Determine if PPP is up.
bool UbloxCellularInterface::isConnected()
{
    return _dev_info->ppp_connection_up;
}

// Get our IP address.
const char *UbloxCellularInterface::get_ip_address()
{
    return nsapi_ppp_get_ip_addr(_fh);
}

// Get the local network mask.
const char *UbloxCellularInterface::get_netmask()
{
    return nsapi_ppp_get_netmask(_fh);
}

// Get the local gateways.
const char *UbloxCellularInterface::get_gateway()
{
    return nsapi_ppp_get_ip_addr(_fh);
}

// End of File

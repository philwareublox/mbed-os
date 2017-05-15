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
#include "UbloxCellularInterfaceGeneric.h"
#include "nsapi_ppp.h"
#include "BufferedSerial.h"
#include "APN_db.h"
#if defined(FEATURE_COMMON_PAL)
#include "mbed_trace.h"
#define TRACE_GROUP "UCGD"
#else
#define tr_debug(...) (void(0)) // dummies if feature common pal is not added
#define tr_info(...)  (void(0)) // dummies if feature common pal is not added
#define tr_error(...) (void(0)) // dummies if feature common pal is not added
#endif

/**********************************************************************
 * STATIC VARIABLES
 **********************************************************************/

// A callback function that will be called if the PPP connection goes down.
static void (*callback_fptr)(nsapi_error_t);

// The ppp_connection_up() callback, which goes out via the PPP stack and has
// to be a static function, needs access to the member variable
// _dev_info->ppp_connection_up, so we need to keep a pointer to it here.
static volatile bool * ppp_connection_up_ptr = NULL;

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
 * PRIVATE METHODS
 **********************************************************************/

// Send the credentials to the modem.
nsapi_error_t UbloxCellularInterfaceGeneric::setup_context_and_credentials()
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

// Enter data mode.
bool UbloxCellularInterfaceGeneric::set_atd()
{
    bool success;
    LOCK();

    success = _at->send("ATD*99***1#") && _at->recv("CONNECT");

    UNLOCK();
    return success;
}

/**********************************************************************
 * PROTECTED METHODS
 **********************************************************************/

// Gain access to the underlying network stack.
NetworkStack *UbloxCellularInterfaceGeneric::get_stack()
{
    return nsapi_ppp_get_stack();
}

// Get the next set of credentials, based on IMSI.
void UbloxCellularInterfaceGeneric::get_next_credentials(const char * config)
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

/**********************************************************************
 * PUBLIC METHODS
 **********************************************************************/

// Constructor.
UbloxCellularInterfaceGeneric::UbloxCellularInterfaceGeneric(bool debug_on,
                                                             PinName tx,
                                                             PinName rx,
                                                             int baud):
                               UbloxCellularGenericBase(debug_on, tx, rx, baud)
{
    _apn = "internet";
    _uname = NULL;
    _pwd = NULL;
    _sim_pin_check_change_pending = false;
    _sim_pin_check_change_pending_enabled_value = false;
    _sim_pin_change_pending = false;
    _sim_pin_change_pending_new_pin_value = NULL;
    _ppp_connection_up = false;

    ppp_connection_up_ptr = &(_ppp_connection_up);
}

// Destructor.
UbloxCellularInterfaceGeneric::~UbloxCellularInterfaceGeneric()
{
    if (_ppp_connection_up) {
        disconnect();
    }
    deinit();

    ppp_connection_up_ptr = NULL;
}

// Set APN, user name and password.
void  UbloxCellularInterfaceGeneric::set_credentials(const char *apn,
                                                     const char *uname,
                                                     const char *pwd)
{
    _apn = apn;
    _uname = uname;
    _pwd = pwd;
}

// Set PIN.
void UbloxCellularInterfaceGeneric::set_sim_pin(const char *pin) {
    set_pin(pin);
}

// Make a cellular connection.
nsapi_error_t UbloxCellularInterfaceGeneric::connect(const char *sim_pin,
                                                     const char *apn,
                                                     const char *uname,
                                                     const char *pwd)
{
    nsapi_error_t nsapi_error;

    if (sim_pin != NULL) {
        set_pin(sim_pin);
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
nsapi_error_t UbloxCellularInterfaceGeneric::connect()
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_IS_CONNECTED;
    const char * config = NULL;

    if (!_ppp_connection_up) {

        // Set up modem and then register with the network
        if (init()) {
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

            if (nsapi_error != NSAPI_ERROR_AUTH_FAILURE) {
                nsapi_error = NSAPI_ERROR_NO_CONNECTION;
                for (int retries = 0; (nsapi_error == NSAPI_ERROR_NO_CONNECTION) && (retries < 3); retries++) {
                    if (nwk_registration(_dev_info->dev)) {
                        nsapi_error = NSAPI_ERROR_OK;
                    }
                }
            }
        } else {
            nsapi_error = NSAPI_ERROR_DEVICE_ERROR;
        }

        if (nsapi_error == NSAPI_ERROR_OK) {
            // If the caller hasn't entered an APN, try to find it
            if (_apn == NULL) {
                config = apnconfig(_dev_info->imsi);
            }

            // Attempt to connect
            do {
                // Set up APN and IP protocol for external PDP context
                get_next_credentials(config);
                nsapi_error = setup_context_and_credentials();

                // Attempt to enter data mode
                if ((nsapi_error == NSAPI_ERROR_OK) && set_atd()) {
                    // Initialise PPP
                    // mbed_ppp_init() is a blocking call, it will block until
                    // connected, or timeout after 30 seconds
                    nsapi_error = nsapi_ppp_connect(_fh, ppp_connection_down_cb, _uname, _pwd);
                    _ppp_connection_up = (nsapi_error == NSAPI_ERROR_OK);
                }
            } while (!_ppp_connection_up && config && *config);
        }

        if (!_ppp_connection_up) {
            tr_error("Failed to connect, check your APN/username/password");
        }
    }

    return nsapi_error;
}

// User initiated disconnect.
nsapi_error_t UbloxCellularInterfaceGeneric::disconnect()
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_DEVICE_ERROR;

    if (nsapi_ppp_disconnect(_fh) == NSAPI_ERROR_OK) {
        // Get the "NO CARRIER" response out of the modem
        // so as not to confuse subsequent AT commands
        _at->send("AT") && _at->recv("NO CARRIER");

        if (nwk_deregistration()) {
            nsapi_error = NSAPI_ERROR_OK;
        }
    }

    return nsapi_error;
}

// Enable or disable SIM PIN check lock.
nsapi_error_t UbloxCellularInterfaceGeneric::check_sim_pin(bool check,
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
nsapi_error_t UbloxCellularInterfaceGeneric::change_sim_pin(const char *new_pin,
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

// Determine if PPP is up.
bool UbloxCellularInterfaceGeneric::is_connected()
{
    return _ppp_connection_up;
}

// Get our IP address.
const char *UbloxCellularInterfaceGeneric::get_ip_address()
{
    return nsapi_ppp_get_ip_addr(_fh);
}

// Get the local network mask.
const char *UbloxCellularInterfaceGeneric::get_netmask()
{
    return nsapi_ppp_get_netmask(_fh);
}

// Get the local gateways.
const char *UbloxCellularInterfaceGeneric::get_gateway()
{
    return nsapi_ppp_get_ip_addr(_fh);
}

// Set the callback to be called in case the connection is lost.
void UbloxCellularInterfaceGeneric::connection_status_cb(void (*fptr)(nsapi_error_t))
{
    callback_fptr = fptr;
}

// End of File

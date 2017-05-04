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

#include "UbloxCellularInterfaceExt.h"
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

/**
 * Test if an HTTP profile is OK to use.
 */
#define IS_PROFILE(p) (((p) >= 0) && (((unsigned int) p) < (sizeof(_httpProfiles)/sizeof(_httpProfiles[0]))) \
                       && (_httpProfiles[p].handle != HTTP_PROF_ERROR))

/**
 * Check for timeout.
 */
#define TIMEOUT(t, ms)  ((ms != TIMEOUT_BLOCKING) && (ms < t.read_ms()))

/**********************************************************************
 * PROTECTED METHODS: HTTP
 **********************************************************************/

// Callback for HTTP result code handling.
void UbloxCellularInterfaceExt::UUHTTPCR_URC()
{
    int a, b, c;

    // +UHTTPCR: <profile_id>,<op_code>,<param_val>
    if (_at->recv(": %d,%d,%d", &a, &b, &c)) {
        _httpProfiles[a].cmd = b;          // Command
        _httpProfiles[a].result = c;       // Result
    }

    tr_debug("%s for profile %d: result code is %d", getHTTPcmd(b), a, c);
}

// Find a given profile.  NOTE: LOCK() before calling.
int UbloxCellularInterfaceExt::findProfile(int handle)
{
    for (unsigned int profile = 0; profile < (sizeof(_httpProfiles)/sizeof(_httpProfiles[0])); profile++) {
        if (_httpProfiles[profile].handle == handle) {
            return profile;
        }
    }

    return HTTP_PROF_ERROR;
}

/**********************************************************************
 * PUBLIC METHODS: GENERIC
 **********************************************************************/

// Constructor.
UbloxCellularInterfaceExt::UbloxCellularInterfaceExt(bool debugOn, PinName tx, PinName rx, int baud):
                           UbloxCellularInterface(debugOn, tx, rx, baud)
{
    // Zero Cell Locate stuff
    _locRcvPos = 0;
    _locExpPos = 0;

    // Zero HTTP profiles
    memset(_httpProfiles, 0, sizeof(_httpProfiles));
    for (unsigned int profile = 0; profile < sizeof(_httpProfiles) / sizeof(_httpProfiles[0]); profile++) {
        _httpProfiles[profile].handle = HTTP_PROF_ERROR;
    }

    // URC handler for HTTP
    _at->oob("+UUHTTPCR", callback(this, &UbloxCellularInterfaceExt::UUHTTPCR_URC));
}

// Destructor.
UbloxCellularInterfaceExt::~UbloxCellularInterfaceExt()
{
    // TODO
}

// Get the IP address of a host.
UbloxCellularInterfaceExt::IP UbloxCellularInterfaceExt::gethostbyname(const char *host)
{
    IP ip = NOIP;
    int a, b, c, d;

    if (sscanf(host, IPSTR, &a, &b, &c, &d) == 4) {
        ip = IPADR(a, b, c, d);
    } else {
        LOCK();
        if (_at->send("AT+UDNSRN=0,\"%s\"", host) && _at->recv("+UDNSRN: \"" IPSTR "\"", &a, &b, &c, &d)) {
            ip = IPADR(a, b, c, d);
        }
        UNLOCK();
    }

    return ip;
}

/**********************************************************************
 * PUBLIC METHODS: HTTP
 **********************************************************************/

// Find a free profile.
int UbloxCellularInterfaceExt::httpFindProfile()
{
    int profile = HTTP_PROF_ERROR;
    LOCK();

    // Find a free HTTP profile
    profile = findProfile();
    tr_debug("httpFindProfile: profile is %d", profile);

    if (profile != HTTP_PROF_ERROR) {
        _httpProfiles[profile].handle     = 1;
        _httpProfiles[profile].timeout_ms = TIMEOUT_BLOCKING;
        _httpProfiles[profile].pending    = false;
        _httpProfiles[profile].cmd        = -1;
        _httpProfiles[profile].result     = -1;
    }

    UNLOCK();
    return profile;
}

// Set the blocking/timeout state of a profile.
bool UbloxCellularInterfaceExt::httpSetBlocking(int profile, int timeout_ms)
{
    bool ok = false;
    LOCK();

    tr_debug("httpSetBlocking(%d, %d)", profile, timeout_ms);
    if (IS_PROFILE(profile)) {
        _httpProfiles[profile].timeout_ms = timeout_ms;
        ok = true;
    }

    UNLOCK();
    return ok;
}

// Set the profile so that it's suitable for commands.
bool UbloxCellularInterfaceExt::httpSetProfileForCmdMng(int profile)
{
    bool ok = false;
    LOCK();

    tr_debug("httpSetProfileForCmdMng(%d)", profile);
    if (IS_PROFILE(profile)) {
        _httpProfiles[profile].pending = true;
        _httpProfiles[profile].result = -1;
        ok = true;
    }

    UNLOCK();
    return ok;
}

// Free a profile.
bool UbloxCellularInterfaceExt::httpFreeProfile(int profile)
{
    LOCK();

    if (IS_PROFILE(profile)) {
        tr_debug("httpFreeProfile(%d)", profile);
        _httpProfiles[profile].handle     = HTTP_PROF_ERROR;
        _httpProfiles[profile].timeout_ms = TIMEOUT_BLOCKING;
        _httpProfiles[profile].pending    = false;
        _httpProfiles[profile].cmd        = -1;
        _httpProfiles[profile].result     = -1;
    }

    UNLOCK();
    return true;
}

// Set a profile back to defaults.
bool UbloxCellularInterfaceExt::httpResetProfile(int httpProfile)
{
    bool ok = false;
    LOCK();

    tr_debug("httpResetProfile(%d)", httpProfile);
    ok = _at->send("AT+UHTTP=%d", httpProfile) && _at->recv("OK");

    UNLOCK();
    return ok;
}

// Set HTTP parameters.
bool UbloxCellularInterfaceExt::httpSetPar(int httpProfile, HttpOpCode httpOpCode, const char * httpInPar)
{
    bool ok = false;
    IP ip = NOIP;
    int httpInParNum = 0;
    LOCK();

    tr_debug("httpSetPar(%d,%d,\"%s\")", httpProfile, httpOpCode, httpInPar);
    switch(httpOpCode) {
        case HTTP_IP_ADDRESS:   // 0
            ip = gethostbyname(httpInPar);
            if (ip != NOIP) {
                ok = _at->send("AT+UHTTP=%d,%d,\"" IPSTR "\"", httpProfile, httpOpCode, IPNUM(ip)) && _at->recv("OK");
            }
            break;
        case HTTP_SERVER_NAME:  // 1
        case HTTP_USER_NAME:    // 2
        case HTTP_PASSWORD:     // 3
            ok = _at->send("AT+UHTTP=%d,%d,\"%s\"", httpProfile, httpOpCode, httpInPar) && _at->recv("OK");
            break;

        case HTTP_AUTH_TYPE:    // 4
        case HTTP_SERVER_PORT:  // 5
            httpInParNum = atoi(httpInPar);
            ok = _at->send("AT+UHTTP=%d,%d,%d", httpProfile, httpOpCode, httpInParNum) && _at->recv("OK");
            break;

        case HTTP_SECURE:       // 6
            if (_dev_info->dev != DEV_LISA_C2) {
                httpInParNum = atoi(httpInPar);
                ok = _at->send("AT+UHTTP=%d,%d,%d", httpProfile, httpOpCode, httpInParNum) && _at->recv("OK");
            } else {
                tr_debug("httpSetPar: HTTP secure option not supported by module");
            }
            break;

        default:
            tr_debug("httpSetPar: unknown httpOpCode %d", httpOpCode);
            break;
    }

    UNLOCK();
    return ok;
}

// Perform an HTTP command.
bool UbloxCellularInterfaceExt::httpCommand(int httpProfile, HttpCmd httpCmdCode, const char *httpPath, const char *httpOut,
                                            const char *httpIn, int httpContentType, const char *httpCustomPar, char *buf, int len)
{
    bool ok = false;
    LOCK();

    tr_debug("%s", getHTTPcmd(httpCmdCode));
    switch (httpCmdCode) {
        case HTTP_HEAD:
            ok = _at->send("AT+UHTTPC=%d,%d,\"%s\",\"%s\"", httpProfile, HTTP_HEAD, httpPath, httpOut) &&
                 _at->recv("OK");
            break;
        case HTTP_GET:
            ok = _at->send("AT+UHTTPC=%d,%d,\"%s\",\"%s\"", httpProfile, HTTP_GET, httpPath, httpOut) &&
                 _at->recv("OK");
            break;
        case HTTP_DELETE:
            ok = _at->send("AT+UHTTPC=%d,%d,\"%s\",\"%s\"", httpProfile, HTTP_DELETE, httpPath, httpOut) &&
                 _at->recv("OK");
            break;
        case HTTP_PUT:
            // In this case the parameter httpIn is a filename
            ok = _at->send("AT+UHTTPC=%d,%d,\"%s\",\"%s\",\"%s\"", httpProfile, HTTP_PUT, httpPath, httpOut, httpIn) &&
                 _at->recv("OK");
            break;
        case HTTP_POST_FILE:
            // In this case the parameter httpIn is a filename
            if (_dev_info->dev != DEV_LISA_C2) {
                if (httpContentType != 6) {
                    ok = _at->send("AT+UHTTPC=%d,%d,\"%s\",\"%s\",\"%s\",%d",
                                   httpProfile, HTTP_POST_FILE, httpPath, httpOut, httpIn, httpContentType) &&
                         _at->recv("OK");
                } else {
                    ok = _at->send("AT+UHTTPC=%d,%d,\"%s\",\"%s\",\"%s\",%d,%s",
                                   httpProfile, HTTP_POST_FILE, httpPath, httpOut, httpIn, httpContentType, httpCustomPar) &&
                         _at->recv("OK");
                }
            } else {
                if ((httpContentType != 5) && (httpContentType != 6) && (httpCustomPar == NULL)) {
                    // Parameter values consistent with the AT commands specs of LISA-C200
                    // (in particular httpCustomPar has to be not defined)
                    ok = _at->send("AT+UHTTPC=%d,%d,\"%s\",\"%s\",\"%s\",%d",
                                   httpProfile, HTTP_POST_FILE, httpPath, httpOut, httpIn, httpContentType) &&
                         _at->recv("OK");
                } else {
                    tr_debug("httpCommand: command not supported by module");
                }
            }
            break;
        case HTTP_POST_DATA:
            // In this case the parameter httpIn is a string containing data
            if (_dev_info->dev != DEV_LISA_C2) {
                if (httpContentType != 6) {
                    ok = _at->send("AT+UHTTPC=%d,%d,\"%s\",\"%s\",\"%s\",%d",
                                   httpProfile, HTTP_POST_DATA, httpPath, httpOut, httpIn, httpContentType) &&
                         _at->recv("OK");
           } else {
                    ok = _at->send("AT+UHTTPC=%d,%d,\"%s\",\"%s\",\"%s\",%d,%s",
                                   httpProfile, HTTP_POST_DATA, httpPath, httpOut, httpIn, httpContentType, httpCustomPar) &&
                         _at->recv("OK");
                }
            } else {
                if ((httpContentType != 5) && (httpContentType != 6) && (httpCustomPar == NULL)) {
                    // Parameters values consistent with the AT commands specs of LISA-C200
                    // (in particular httpCustomPar has to be not defined)
                    ok = _at->send("AT+UHTTPC=%d,%d,\"%s\",\"%s\",\"%s\",%d",
                                   httpProfile, HTTP_POST_DATA, httpPath, httpOut, httpIn, httpContentType) &&
                         _at->recv("OK");
                } else {
                    tr_debug("httpCommand: command not supported by module");
                }
            }
            break;
        default:
            tr_debug("HTTP command not recognised");
    }

    if (ok) {
        Timer timer;

        timer.start();
        httpSetProfileForCmdMng(httpProfile);

        // Waiting for unsolicited result codes
        while (_httpProfiles[httpProfile].pending) {
            ok = false;  // Reset variable
            if (_httpProfiles[httpProfile].result != -1) {
                // Received unsolicited: starting its analysis
                _httpProfiles[httpProfile].pending = false;
                if (_httpProfiles[httpProfile].result == 1) {
                    // HTTP command successfully executed
                    if (_dev_info->dev != DEV_LISA_C2) {
                        tr_debug("httpCommand: reading files with a dimension also greater than MAX_SIZE bytes");
                        if (readFileNew(httpOut, buf, len) >= 0) {
                            ok = true;
                        }
                    } else {
                        tr_debug("httpCommand: reading files with a dimension less than MAX_SIZE bytes, otherwise error");
                        if (readFile(httpOut, buf, len) >= 0) {
                            ok = true;
                        }
                    }
                } else {
                    // HTTP command not successfully executed
                    ok = false;
                }
            } else if (!TIMEOUT(timer, _httpProfiles[httpProfile].timeout_ms)) {
                // Wait for URCs
                wait_ms (1000);
            } else  {
                // Not received unsolicited and expired timer
                tr_debug("httpCommand: URC not received in time");
                ok = false;
            }

            if (!ok) {
                tr_debug("%s: ERROR", getHTTPcmd(httpCmdCode));
                // No more while loops
                _httpProfiles[httpProfile].pending = false;
            }
        }
    }

    UNLOCK();
    return ok;
}

// Return a string representing an AT command.
const char * UbloxCellularInterfaceExt::getHTTPcmd(int httpCmdCode)
{
    switch (httpCmdCode) {
        case HTTP_HEAD:
            return "HTTP HEAD command";
        case HTTP_GET:
            return "HTTP GET command";
        case HTTP_DELETE:
            return "HTTP DELETE command";
        case HTTP_PUT:
            return "HTTP PUT command";
        case HTTP_POST_FILE:
            return "HTTP POST file command";
        case HTTP_POST_DATA:
            return "HTTP POST data command";
        default:
            return "HTTP command not recognised";
    }
}

// End of file

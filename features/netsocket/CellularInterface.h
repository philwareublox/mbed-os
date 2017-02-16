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

#ifndef CELLULAR_INTERFACE_H
#define CELLULAR_INTERFACE_H

#include "netsocket/NetworkInterface.h"

/** CellularInterface class
 *
 *  Common interface that is shared between Cellular interfaces
 */
class CellularInterface: public NetworkInterface {
    /** CellularInterface lifetime
     */
    //virtual ~CellularInterface() {};

    /** Set the Cellular network credentials
     *
     *  Please check documentation of connect() for default behaviour of APN settings.
     *
     *  @param apn      Access point name
     *  @param uname    optionally, Username
     *  @param pwd      optionally, password
     */
    virtual void set_credentials(const char *apn,
                                          const char *uname = 0,
                                          const char *pwd = 0) = 0;

    /** Set the pin code for SIM card
     *
     *  @param sim_pin      PIN for the SIM card
     */
    virtual void set_SIM_pin(const char *sim_pin) = 0;

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
                                  const char *uname = 0,
                                  const char *pwd = 0) = 0;

    /** Start the interface
     *
     *  Attempts to connect to a Cellular network.
     *  If the SIM requires a PIN, and it is not set/invalid, NSAPI_ERROR_AUTH_ERROR is returned.
     *
     *  @return            NSAPI_ERROR_OK on success, or negative error code on failure
     */
    virtual nsapi_error_t connect() = 0;

    /** Stop the interface
        *
        *  @return         0 on success, or error code on failure
        */
    virtual nsapi_error_t disconnect() = 0;
};

#endif

/** @}*/

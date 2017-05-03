/* mbed Microcontroller Library
 * Copyright (c) 2017 ARM Limited
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
#include "ublox_low_level_api.h"
#include "gpio_api.h"
#include "platform/mbed_wait_api.h"
#include "PinNames.h"

#ifdef DEVICE_MODEM
static void press_power_button(int time_ms)
{
    gpio_t gpio;

    gpio_init_out_ex(&gpio, MDMPWRON, 0);
    wait_ms(time_ms);
    gpio_write(&gpio, 1);
}

void modem_init()
{
    //currently USB is not supported, so pass 0 to disable USB
    //This call does everything except actually pressing the power button
    ublox_mdm_powerOn(0);
}

void modem_deinit()
{
    ublox_mdm_powerOff();
}
void modem_power_up()
{
    /* keep the power line low for 150 milisecond */
    press_power_button(150);
    /* give modem a little time to respond */
    wait_ms(100);
}

void modem_power_down()
{
    /* keep the power line low for 1 second */
    press_power_button(1000);
}
#endif //DEVICE_MODEM

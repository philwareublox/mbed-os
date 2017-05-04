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
#include "gpio_api.h"
#include "platform/mbed_wait_api.h"
#include "PinNames.h"

#ifdef DEVICE_MODEM
// Note microseconds not milliseconds
static void press_power_button(int time_us)
{
    gpio_t gpio;

    gpio_init_out_ex(&gpio, MDMPWRON, 0);
    wait_us(time_us);
    gpio_write(&gpio, 1);
}

void modem_init(modem_t *obj)
{
    gpio_t gpio;

    // Take us out of reset
    gpio_init_out_ex(&gpio, MDMRST,    1);
    obj->state = POWER_READY;
}

void modem_deinit(modem_t *obj)
{
    gpio_t gpio;

    // Back into reset
    gpio_init_out_ex(&gpio, MDMRST, 0);
    obj->state = LOWEST_POWER_STATE;
}
void modem_power_up(modem_t *obj)
{
    /* keep the power line low for 50 microseconds */
    press_power_button(50);
    /* give modem a little time to respond */
    wait_ms(100);
    obj->state = POWERED_ON;
}

void modem_power_down(modem_t *obj)
{
    /* keep the power line low for 1 second */
    press_power_button(1000000);
    obj->state = POWERED_OFF;
}
#endif //DEVICE_MODEM

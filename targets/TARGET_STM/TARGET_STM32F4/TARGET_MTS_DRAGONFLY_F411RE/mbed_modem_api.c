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
static void press_power_button(int time_ms)
{
    gpio_t gpio;

    gpio_init_out_ex(&gpio, MDMPWRON, 1);
    gpio_write(&gpio, 0);
    wait_ms(time_ms);
    gpio_write(&gpio, 1);
}

void modem_init(modem_t *obj)
{
   //does nothing at the moment, TODO: MultiTech to add hardware initialization stuff if needed
    obj->state = POWER_READY;
}

void modem_deinit(modem_t *obj)
{
    //does nothing at the moment, TODO: MultiTech to add hardware de-initialization stuff if needed
    obj->state = LOWEST_POWER_STATE;
}
void modem_power_up(modem_t *obj)
{
    /* keep the power line low for 200 milisecond */
    press_power_button(200);
    /* give modem a little time to respond */
    wait_ms(100);
    obj->state = POWERED_ON;
}

void modem_power_down(modem_t *obj)
{
    gpio_t gpio;

    gpio_init_out_ex(&gpio, MDMPWRON, 0);
    /* keep the power line low for more than 10 seconds.
     * If 3G_ON_OFF pin is kept low for more than a second, a controlled disconnect and shutdown takes
     * place, Due to the network disconnect, shut-off can take up to 30 seconds. However, we wait for 10
     * seconds only   */
    wait_ms(10*1000);
    obj->state = POWERED_OFF;
}
#endif //DEVICE_MODEM

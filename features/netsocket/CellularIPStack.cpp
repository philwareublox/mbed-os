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

#include "CellularIPStack.h"
#include "rtos.h"

CellularIPStack * CellularIPStack::_cellular_stack;

CellularIPStack * CellularIPStack::get_stack()
{
    //nanostack_lock();

    if (NULL == _cellular_stack) {
        _cellular_stack = new CellularIPStack();
    }

 //   nanostack_unlock();

    return _cellular_stack;
}





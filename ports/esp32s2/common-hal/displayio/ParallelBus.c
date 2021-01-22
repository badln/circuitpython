/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Lucian Copeland for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "shared-bindings/displayio/ParallelBus.h"

#include <stdint.h>

#include "common-hal/microcontroller/Pin.h"
#include "py/runtime.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "shared-bindings/microcontroller/__init__.h"

/*
* Current pin limitations:
* data0 pin must be byte aligned and use pin numbers < 32 (data0 options: 0, 8, 16 or 24)
* write pin must be pin number < 32.
*
* Future extensions:
* 1. Allow data0 pin numbers >= 32.
* 2. Allow write pin numbers >= 32.
*/

void common_hal_displayio_parallelbus_construct(displayio_parallelbus_obj_t* self,
    const mcu_pin_obj_t* data0, const mcu_pin_obj_t* command, const mcu_pin_obj_t* chip_select,
    const mcu_pin_obj_t* write, const mcu_pin_obj_t* read, const mcu_pin_obj_t* reset) {


    uint8_t data_pin = data0->number;
    if ( (data_pin % 8 != 0) && (data_pin >= 32) ) {
        mp_raise_ValueError(translate("Data 0 pin must be byte aligned and < 32"));
    }

    for (uint8_t i = 0; i < 8; i++) {
        if (!pin_number_is_free(data_pin + i)) {
            mp_raise_ValueError_varg(translate("Bus pin %d is already in use"), i);
        }
    }

    if (write->number >= 32) {
    	mp_raise_ValueError(translate("Write pin must be < 32"));
    }

    gpio_dev_t *g = &GPIO; /* this is the GPIO registers, see "extern gpio_dev_t GPIO" from file:gpio_struct.h */

    /* Setup the pins as "Simple GPIO outputs" see section 19.3.3 of the ESP32-S2 Reference Manual */
    /* Enable pins with "enable_w1ts" */

    for (uint8_t i = 0; i < 8; i++) {
    	g->enable_w1ts = (0x1 << (data_pin + i));
    	g->func_out_sel_cfg[data_pin + i].val= 256; /* setup output pin for simple GPIO Output, (0x100 = 256) */

    }

    /* I think there is a limitation of the ESP32-S2 that does not allow single-byte writes into
    the GPIO registers.  See section 10.3.3 regarding "non-aligned writes" into the registers.
    If a method for writing single-byte writes is uncovered, this code can be modified to provide
    single-byte access into the output register */

	self->bus = (uint32_t*) &g->out; //pointer to GPIO output register (for pins 0-31)

    /* SNIP - common setup of command, chip select, write and read pins, same as from SAMD and NRF ports */
    self->command.base.type = &digitalio_digitalinout_type;
    common_hal_digitalio_digitalinout_construct(&self->command, command);
    common_hal_digitalio_digitalinout_switch_to_output(&self->command, true, DRIVE_MODE_PUSH_PULL);

    self->chip_select.base.type = &digitalio_digitalinout_type;
    common_hal_digitalio_digitalinout_construct(&self->chip_select, chip_select);
    common_hal_digitalio_digitalinout_switch_to_output(&self->chip_select, true, DRIVE_MODE_PUSH_PULL);

    self->write.base.type = &digitalio_digitalinout_type;
    common_hal_digitalio_digitalinout_construct(&self->write, write);
    common_hal_digitalio_digitalinout_switch_to_output(&self->write, true, DRIVE_MODE_PUSH_PULL);

    self->read.base.type = &digitalio_digitalinout_type;
    common_hal_digitalio_digitalinout_construct(&self->read, read);
    common_hal_digitalio_digitalinout_switch_to_output(&self->read, true, DRIVE_MODE_PUSH_PULL);

    self->data0_pin = data_pin;
    self->write_group = &GPIO;
    /* Should modify the .h structure definitions if want to allow a write pin >= 32.
       If so, consider putting separate "clear_write" and "set_write" pointers into the .h in place of "write_group"
       to select between out_w1tc/out1_w1tc (clear) and out_w1ts/out1_w1ts (set) registers.  */

    self->write_mask = 1 << (write->number % 32); /* the write pin triggers the LCD to latch the data */
    /* Note:  As currently written for the ESP32-S2 port, the write pin must be a pin number less than 32
       This could be updated to accommodate 32 and higher by using the different construction of the
       address for writing to output pins >= 32, see related note above for 'self->write_group' */

    /* SNIP - common setup of the reset pin, same as from SAMD and NRF ports */
    self->reset.base.type = &mp_type_NoneType;
    if (reset != NULL) {
        self->reset.base.type = &digitalio_digitalinout_type;
        common_hal_digitalio_digitalinout_construct(&self->reset, reset);
        common_hal_digitalio_digitalinout_switch_to_output(&self->reset, true, DRIVE_MODE_PUSH_PULL);
        never_reset_pin_number(reset->number);
        common_hal_displayio_parallelbus_reset(self);
    }

    never_reset_pin_number(command->number);
    never_reset_pin_number(chip_select->number);
    never_reset_pin_number(write->number);
    never_reset_pin_number(read->number);
    for (uint8_t i = 0; i < 8; i++) {
        never_reset_pin_number(data_pin + i);
    }

}

void common_hal_displayio_parallelbus_deinit(displayio_parallelbus_obj_t* self) {
	/* SNIP - same as from SAMD and NRF ports */
    for (uint8_t i = 0; i < 8; i++) {
        reset_pin_number(self->data0_pin + i);
    }

    reset_pin_number(self->command.pin->number);
    reset_pin_number(self->chip_select.pin->number);
    reset_pin_number(self->write.pin->number);
    reset_pin_number(self->read.pin->number);
    reset_pin_number(self->reset.pin->number);
}

bool common_hal_displayio_parallelbus_reset(mp_obj_t obj) {
	/* SNIP - same as from SAMD and NRF ports */
	displayio_parallelbus_obj_t* self = MP_OBJ_TO_PTR(obj);
    if (self->reset.base.type == &mp_type_NoneType) {
        return false;
    }

    common_hal_digitalio_digitalinout_set_value(&self->reset, false);
    common_hal_mcu_delay_us(4);
    common_hal_digitalio_digitalinout_set_value(&self->reset, true);
    return true;

}

bool common_hal_displayio_parallelbus_bus_free(mp_obj_t obj) {
	/* SNIP - same as from SAMD and NRF ports */
    return true;
}

bool common_hal_displayio_parallelbus_begin_transaction(mp_obj_t obj) {
	/* SNIP - same as from SAMD and NRF ports */
    displayio_parallelbus_obj_t* self = MP_OBJ_TO_PTR(obj);
    common_hal_digitalio_digitalinout_set_value(&self->chip_select, false);
    return true;
}

void common_hal_displayio_parallelbus_send(mp_obj_t obj, display_byte_type_t byte_type,
	display_chip_select_behavior_t chip_select, const uint8_t *data, uint32_t data_length) {
    displayio_parallelbus_obj_t* self = MP_OBJ_TO_PTR(obj);
    common_hal_digitalio_digitalinout_set_value(&self->command, byte_type == DISPLAY_DATA);

    /* Currently the write pin number must be < 32.
       Future: To accommodate write pin numbers >= 32, will need to update to choose a different register
       for set/reset (out1_w1tc and out1_w1ts) */

    uint32_t* clear_write = (uint32_t*) &self->write_group->out_w1tc;
    uint32_t* set_write = (uint32_t*) &self->write_group->out_w1ts;
    uint32_t mask = self->write_mask;

    /* Setup structures for data writing.  The ESP32-S2 port differs from the SAMD and NRF ports
    because I have not found a way to write a single byte into the ESP32-S2 registers.
    For the ESP32-S2, I create a 32-bit data_buffer that is used to transfer the data bytes. */

    *clear_write = mask; // Clear the write pin to prepare the registers before storing register settings into data_buffer
    uint32_t data_buffer = *self->bus; // store the initial output register values into the data output buffer
    uint8_t* data_address = ((uint8_t*) &data_buffer) + (self->data0_pin / 8); /* address inside data_buffer where
    							each data byte will be written (as offset by (data0_pin/8) number of bytes) */

    for (uint32_t i = 0; i < data_length; i++) {

    	/* Question: Is there a faster way of stuffing the data byte into the data_buffer, is bit arithmetic
    	faster than writing to the byte address? */

    	/* Note: May be able to eliminate either the clear_write or set_write since the data buffer
    	can be written with the write pin cleared or set already, and depending upon whether the display
    	latches the data on the rising or falling edge of the write pin.  Remember: This method
    	will require the write pin to be controlled by the same GPIO register as the data pins.  */

        *clear_write = mask; // clear the write pin (See comment above, this may not be necessary).
        *(data_address) = data[i]; // stuff the data byte into the data_buffer at the correct offset byte location
        *self->bus = data_buffer; // write the data to the output register
        *set_write = mask; // set the write pin
    }

}

void common_hal_displayio_parallelbus_end_transaction(mp_obj_t obj) {
	/* SNIP - same as from SAMD and NRF ports */
    displayio_parallelbus_obj_t* self = MP_OBJ_TO_PTR(obj);
    common_hal_digitalio_digitalinout_set_value(&self->chip_select, true);
}

/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
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
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "hardware/structs/iobank0.h"
#include "hardware/irq.h"
#include "hardware/structs/systick.h"
#include "hardware/pio.h"
#include "hardware/flash.h"
#include "hardware/dma.h"

#include "bsp/board.h"
#include "tusb.h"

#include "n64send.pio.h"

#define USE_GPIO_IRQ

#define N64_DIO_PIN	14

#define FLASH_TARGET_SIZE	(32 * 1024)
#define FLASH_TARGET_OFFSET	(2 * 1024 * 1024 - 32 * 1024)

#define N64SEND_DATA(d0, d1, b) ((((b) - 1) << 16) | ((d0) << 8) | (d1))

enum {
    USB_UNKNOWN = 0,
    USB_XPAD,
    USB_HID_GAMEPAD,
    USB_MOUSE,
    USB_KEYBOARD
};

const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);

static uint8_t _dev_addr;

static volatile uint8_t input_device = USB_UNKNOWN;

static uint16_t m_vid = 0;
static uint16_t m_pid = 0;

static volatile uint8_t enable_vibro = 0;
static volatile uint8_t disable_vibro = 0;

static volatile uint8_t buttons[2] = { 0, 0 };
static volatile uint8_t sticks[2] = { 0, 0 };

static volatile uint16_t randnet_keys[3];
static volatile uint8_t  randnet_pressed;
static volatile bool     randnet_error;
static volatile bool     randnet_home;
static volatile uint8_t  randnet_led_status;

static volatile uint8_t use_rumble_pack = 0;
static volatile uint8_t memory_pak_changed = 0;

static volatile bool core1_disable_irq = false;

static uint8_t data_block[32];
static uint8_t memory_pak[32768];

static volatile PIO pio;
static volatile uint sm;
static volatile uint pio_offset;

static volatile uint32_t pio_dma_chan;

// 16 words (data) + 1 word (crc) + 1 word (stop)
static volatile uint32_t dma_buffer[18] __attribute__((aligned (16)));

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

extern void hid_app_task(void);

void debug_dump_16(uint8_t *ptr);

void tuh_xpad_mount_cb(uint8_t dev_addr)
{
    _dev_addr = dev_addr;

    tuh_vid_pid_get(dev_addr, &m_vid, &m_pid);

    printf("A xpad device %04X:%04X with address %d is mounted\r\n", m_vid, m_pid, dev_addr);

    input_device = USB_XPAD;
}

static int8_t analog_value(int16_t val)
{
    val = val / 0x190;

    if (val < 10 && val > -10) return 0;

    if (val > 0x50) val = 0x50;

    if (val < -0x50) val = -0x50;

    return val;
}

void tuh_xpad_read_cb(uint8_t dev_addr, uint8_t *report, xpad_controller_t *info)
{
    uint8_t b = 0;
    uint8_t b1 = 0;

//    printf("buttons %04X lx=%d ly=%d rx=%d ry=%d lt=%d rt=%d\n", info->buttons, info->lx, info->ly, info->rx, info->ry, info->lt, info->rt);

    /*if (info->buttons & XPAD_HAT_UP)    b |= 0x08; // D-U    D-UP
    if (info->buttons & XPAD_HAT_DOWN)  b |= 0x04; // D-D    D-D
    if (info->buttons & XPAD_HAT_LEFT)  b |= 0x02; // D-L    D-L
    if (info->buttons & XPAD_HAT_RIGHT) b |= 0x01; // D-R    D-R

    if (analog_value(info->ry) > 40)    b1|= 0x08; // RS-U  C-U
    if (analog_value(info->ry) < -40)   b1|= 0x04; // RS-D  C-D
    if (analog_value(info->rx) < -40)   b1|= 0x02; // RS-L  C-L
    if (analog_value(info->rx) > 40)    b1|= 0x01; // RS-R  C-R

    if (info->buttons & XPAD_PAD_A)     b |= 0x80; // A      A
    if (info->buttons & XPAD_PAD_B)     b |= 0x40; // B      B
    if (info->lt > 512)                 b |= 0x20; // LT    Z
    if (info->rt > 512)                 b |= 0x20; // LT    Z
    if (info->buttons & XPAD_START)     b |= 0x10; // START  START

    if (info->buttons & XPAD_PAD_LB)    b1|= 0x20; // LB      L
    if (info->buttons & XPAD_PAD_RB)    b1|= 0x10; // RB      R

    buttons[0] = b;
    buttons[1] = b1;
    sticks[0] = analog_value(info->lx);
    sticks[1] = analog_value(info->ly);

	*/

	/ D-Pad
    if (info->buttons & XPAD_HAT_UP)    b |= 0x08;
    if (info->buttons & XPAD_HAT_DOWN)  b |= 0x04;
    if (info->buttons & XPAD_HAT_LEFT)  b |= 0x02;
    if (info->buttons & XPAD_HAT_RIGHT) b |= 0x01;

    // C-Buttons
    if (info->buttons & XPAD_PAD_LB)    b1 |= 0x08; // C-Up
    if (info->buttons & XPAD_PAD_RB)    b1 |= 0x04; // C-Down
    if (info->lt > 512)                 b1 |= 0x02; // C-Left
    if (info->rt > 512)                 b1 |= 0x01; // C-Right

    // Standard Buttons
    if (info->buttons & XPAD_PAD_A)     b |= 0x80; // A
    if (info->buttons & XPAD_PAD_B)     b |= 0x40; // B
    if (info->buttons & XPAD_PAD_X)     b |= 0x20; // Z
    if (info->buttons & XPAD_PAD_Y)     b |= 0x10; // R
    //if (info->buttons & XPAD_PAD_LS)    b1 |= 0x20; // L

    // Start
    if (info->buttons & XPAD_START)     b |= 0x10;

    // Stick-Priorisierung: linker bevorzugt
    if (abs(analog_value(info->lx)) > 10 || abs(analog_value(info->ly)) > 10) {
        sticks[0] = analog_value(info->lx);
        sticks[1] = analog_value(info->ly);
    } else {
        sticks[0] = analog_value(info->rx);
        sticks[1] = analog_value(info->ry);
    }

    buttons[0] = b;
    buttons[1] = b1;



	

    if (info->buttons & XPAD_XLOGO) {
	use_rumble_pack = !use_rumble_pack;
    }

    //debug_dump_16(report);
}

static void xpad_task(void)
{
    if (enable_vibro == 1) {
        if (input_device == USB_XPAD) {
            tuh_xpad_vibro(_dev_addr, 1);
        }
//	printf("Start vibro\n");
	enable_vibro = 0;
    }

    if (disable_vibro == 1) {
        if (input_device == USB_XPAD) {
            tuh_xpad_vibro(_dev_addr, 0);
        }
//	printf("Stop vibro\n");
	disable_vibro = 0;
    }
}

static void led_blinking_task(void)
{
    const uint32_t interval_ms = use_rumble_pack ? 500 : 1000;
    static uint32_t start_ms = 0;

    static bool led_state = false;

    // Blink every interval ms
    if ( board_millis() - start_ms < interval_ms) return; // not enough time
    start_ms += interval_ms;

    board_led_write(led_state);
    led_state = 1 - led_state; // toggle
}

void usb_host_process(void)
{
    tusb_init();

    while (1) {
	tuh_task();

	led_blinking_task();

#if CFG_TUH_XPAD
	xpad_task();
#endif

#if CFG_TUH_HID
	hid_app_task();
#endif

	if (core1_disable_irq) {
	    uint32_t ints = save_and_disable_interrupts();
	    multicore_fifo_push_blocking(0x4321);
	    uint32_t g = multicore_fifo_pop_blocking();
	    restore_interrupts(ints);
	}
    }
}

void enable_keyboard(void)
{
    randnet_keys[0] = 0;
    randnet_keys[1] = 0;
    randnet_keys[2] = 0;
    randnet_pressed = 0;
    randnet_error = false;
    randnet_home = false;

    printf("Keyboard enabled\n");
    input_device = USB_KEYBOARD;
}

void enable_mouse(void)
{
    printf("Mouse enabled\n");
    input_device = USB_MOUSE;
}

void enable_hid_gamepad(void)
{
    printf("HID gamepad enabled\n");
    input_device = USB_HID_GAMEPAD;
}

void update_keys(uint16_t keys[3], bool error, bool home)
{
    randnet_keys[0] = keys[0];
    randnet_keys[1] = keys[1];
    randnet_keys[2] = keys[2];

    randnet_error = error;
    randnet_home = home;

//    printf("%02X %02X %02X %s %s\n", randnet_keys[0], randnet_keys[1], randnet_keys[2], error ? "[ERROR]" : "", home ? "[HOME]" : "");
}

void update_mouse(uint8_t butts, int8_t x, int8_t y, int8_t wheel, int8_t acpan)
{
    uint8_t b = 0;
    uint8_t b1 = 0;

//    printf("buttons=%02X x=%d y=%d wheel=%d acpan=%d\n", butts, x, y, wheel, acpan);

    if (butts & MOUSE_BUTTON_LEFT)   b |= 0x80; // MOUSE LB  A
    if (butts & MOUSE_BUTTON_RIGHT)  b |= 0x40; // MOUSE RB  B
    if (butts & MOUSE_BUTTON_MIDDLE) b |= 0x10; // MOUSE MB  START
    if (wheel > 0) b1 |= 0x08;                  // MOUSE W-U C-U
    if (wheel < 0) b1 |= 0x04;                  // MOUSE W-D C-D
    if (acpan > 0) b1 |= 0x02;                  // MOUSE W-L C-L
    if (acpan < 0) b1 |= 0x01;                  // MOUSE W-R C-R
    buttons[0] = b;
    buttons[1] = b1;
    sticks[0] = x;
    sticks[1] = -y;
}

void debug_dump_16(uint8_t *ptr)
{
    int i;
    char tmp[128];
    const char dig[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

    memset(tmp, ' ', sizeof(tmp));
    tmp[127] = 0;

    for (i = 0; i < 16; i++) {
	tmp[i * 3 + 0] = dig[ptr[i] >> 4];
	tmp[i * 3 + 1] = dig[ptr[i] & 0xf];
	tmp[i * 3 + 2] = ' ';
	tmp[49 + i] = (ptr[i] >= 32) ? ptr[i] : '.';
    }
    tmp[48] = ' ';
    tmp[65] = 0;
    printf("DUMP16: %s\n", tmp);
}

static uint16_t __not_in_flash_func(calc_address_crc)(uint16_t address)
{
    /* CRC table */
    uint16_t xor_table[16] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x15, 0x1F, 0x0B, 0x16, 0x19, 0x07, 0x0E, 0x1C, 0x0D, 0x1A, 0x01 };
    uint16_t crc = 0;

    /* Make sure we have a valid address */
    address &= ~0x1F;

    /* Go through each bit in the address, and if set, xor the right value into the output */
    for(int i = 15; i >= 5; i--) {
        /* Is this bit set? */
        if(((address >> i) & 0x1)) {
            crc ^= xor_table[i];
        }
    }

    /* Just in case */
    crc &= 0x1F;

    /* Create a new address with the CRC appended */
    return address | crc;
}

static uint8_t __not_in_flash_func(calc_data_crc)( uint8_t *data )
{
    uint8_t ret = 0;

    for(int i = 0; i <= 32; i++) {
        for(int j = 7; j >= 0; j--) {
            int tmp = 0;

            if(ret & 0x80) {
                tmp = 0x85;
            }

            ret <<= 1;

            if(i < 32) {
                if(data[i] & (0x01 << j)) {
                    ret |= 0x1;
                }
            }
            ret ^= tmp;
        }
    }

    return ret;
}

static inline uint8_t reverse(uint8_t b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

#define TICKS_1US	197UL
//#define TICKS_1US	175

static inline void wait_ticks(uint32_t count)
{
    systick_hw->csr = 0x0;
    systick_hw->rvr = 0xFFFFFF;
    systick_hw->csr = 0x5;
    uint32_t old = systick_hw->cvr;
    while (old - systick_hw->cvr < count) {
    }
}

static int __not_in_flash_func(read_data_block)(uint8_t *data_block)
{
    uint8_t byte = 0;
    int bits_read = 0;
    int bytes_read = 0;

    while (1) {
	wait_ticks(TICKS_1US * 2);
	byte <<= 1;
	byte |= (gpio_get(N64_DIO_PIN) ? 1 : 0);

	bits_read++;

	if (bits_read == 8) {
	    data_block[bytes_read++] = byte;
	    byte = 0;
	    bits_read = 0;
	    if (bytes_read == 32) {
//		wait_ticks(TICKS_1US * 2); // console stop bit
//		wait_ticks(TICKS_1US);     //
		uint8_t crc = calc_data_crc(data_block);
//		wait_ticks(TICKS_1US * 3);

		dma_buffer[0] = N64SEND_DATA(crc, 0x00, 8);
		dma_buffer[1] = 0;


		pio_sm_exec(pio, sm, pio_encode_jmp(pio_offset + n64send_dma_offset_loop));

		dma_channel_transfer_from_buffer_now(pio_dma_chan, dma_buffer, 2);
		dma_channel_wait_for_finish_blocking(pio_dma_chan);

		while (pio_sm_get_pc(pio, sm) != (pio_offset + n64send_dma_offset_stop)) {}

		return bytes_read;
	    }
	}

	int timeout = 300;
	while(!gpio_get(N64_DIO_PIN) && timeout--) {
	}

	if (timeout != 0) {
	    timeout = 300;
	    while(gpio_get(N64_DIO_PIN) && timeout--) {
	    }
	}

	if (timeout == 0) {
	    return -3;
	}
    }
}

static int __not_in_flash_func(write_data_block)(uint8_t *data_block)
{
    uint8_t crc = calc_data_crc(data_block);

    for (int i = 0; i < 32; i+= 2) {
	dma_buffer[i >> 1] = N64SEND_DATA(data_block[i + 0], data_block[i + 1], 16);
    }
    dma_buffer[16] = N64SEND_DATA(crc, 0x00, 8);
    dma_buffer[17] = 0;

    pio_sm_exec(pio, sm, pio_encode_jmp(pio_offset + n64send_dma_offset_loop));

    dma_channel_transfer_from_buffer_now(pio_dma_chan, dma_buffer, 18);
    dma_channel_wait_for_finish_blocking(pio_dma_chan);

    while (pio_sm_get_pc(pio, sm) != (pio_offset + n64send_dma_offset_stop)) {}
}

static uint32_t __not_in_flash_func(read_command)()
{
    int bits_read = 0;
    uint32_t command = 0;
    int timeout;

    while (1) {
	wait_ticks(TICKS_1US * 2);
	command <<= 1;
	command |= (gpio_get(N64_DIO_PIN) ? 1 : 0);

	bits_read++;

	if (bits_read == 9) {
	    // if not command 0x02 and 0x03
	    if ((command >> 1) != 0x02 && (command >> 1) != 0x03 && (command >> 1) != 0x13) {
		command >>= 1;

		wait_ticks(TICKS_1US * 4);

		if (command == 0x00 || command == 0xFF) {
		    if (input_device == USB_MOUSE) {
			dma_buffer[0] = N64SEND_DATA(0x02, 0x00, 16);
			dma_buffer[1] = N64SEND_DATA(0x01, 0x00, 8);
		    } else if (input_device == USB_KEYBOARD) {
			dma_buffer[0] = N64SEND_DATA(0x00, 0x02, 16);
			dma_buffer[1] = N64SEND_DATA(0x01, 0x00, 8);
		    } else {
			dma_buffer[0] = N64SEND_DATA(0x05, 0x00, 16);
			dma_buffer[1] = N64SEND_DATA(0x01, 0x00, 8);
		    }
		    dma_buffer[2] = 0;

		    pio_sm_exec(pio, sm, pio_encode_jmp(pio_offset + n64send_dma_offset_loop));

		    dma_channel_transfer_from_buffer_now(pio_dma_chan, dma_buffer, 3);
		    dma_channel_wait_for_finish_blocking(pio_dma_chan);

		    while (pio_sm_get_pc(pio, sm) != (pio_offset + n64send_dma_offset_stop)) {}
		} else if (command == 0x01) {
		    if (input_device != USB_KEYBOARD) {
			dma_buffer[0] = N64SEND_DATA(buttons[0], buttons[1], 16);
			dma_buffer[1] = N64SEND_DATA(sticks[0], sticks[1], 16);
			dma_buffer[2] = 0;

			pio_sm_exec(pio, sm, pio_encode_jmp(pio_offset + n64send_dma_offset_loop));

			dma_channel_transfer_from_buffer_now(pio_dma_chan, dma_buffer, 3);
			dma_channel_wait_for_finish_blocking(pio_dma_chan);

			while (pio_sm_get_pc(pio, sm) != (pio_offset + n64send_dma_offset_stop)) {}

			if (input_device == USB_MOUSE) {
			    sticks[0] = 0;
			    sticks[1] = 0;
			}
		    }
		} else {
		    printf("Unk cmd %X\n", command);
		}

		return command;
	    }
	}

	timeout = 300;
	while(!gpio_get(N64_DIO_PIN) && timeout--) {
	}

	if (timeout != 0) {
	    timeout = 300;
	    while(gpio_get(N64_DIO_PIN) && timeout--) {
	    }
	}

	if (timeout == 0) {
	    return -1;
	}

	// command 0x03 + address 2 bytes
	if (bits_read == 24) {
	    if ((command >> 16) == 0x03) {
		if (read_data_block(data_block) != 32) {
		    return -2;
		}

		uint32_t addr = command & 0xFFE0;

		if (addr < 0x8000) {
		    memmove(&memory_pak[addr], data_block, 32);
		    memory_pak_changed = 1;
		} else if (use_rumble_pack && (command & 0xFFE0) == 0xC000) {
		    if (data_block[0] == 0x00) {
			// stop rumble pack
			disable_vibro = 1;
		    } else {
			// start rumble pack
			enable_vibro = 1;
		    }
		}
	    } else {
		wait_ticks(TICKS_1US * 3); // skip console stop bit

		uint32_t addr = command & 0xFFE0;

		if (addr < 0x8000) {
		    memmove(data_block, &memory_pak[addr], 32);
		} else if (use_rumble_pack && (command & 0xFFE0) == 0x8000) {
		    memset(data_block, 0x80, 32);
		} else {
		    memset(data_block, 0x00, 32);
		}
		write_data_block(data_block);
	    }
	    return command;
	} else if (bits_read == 16) {
	    if ((command >> 8) == 0x13) {
		wait_ticks(TICKS_1US * 3); // skip console stop bit

		randnet_led_status = command & 0xff;

		uint8_t *ptr = (uint8_t *)randnet_keys;

		dma_buffer[0] = N64SEND_DATA(ptr[1], ptr[0], 16);
		dma_buffer[1] = N64SEND_DATA(ptr[3], ptr[2], 16);
		dma_buffer[2] = N64SEND_DATA(ptr[5], ptr[4], 16);
		dma_buffer[3] = N64SEND_DATA(((randnet_error ? 0x10 : 0x00) | (randnet_home ? 0x01 : 0x00)), 0, 8);
		dma_buffer[4] = 0;

		pio_sm_exec(pio, sm, pio_encode_jmp(pio_offset + n64send_dma_offset_loop));

		dma_channel_transfer_from_buffer_now(pio_dma_chan, dma_buffer, 5);
		dma_channel_wait_for_finish_blocking(pio_dma_chan);

		while (pio_sm_get_pc(pio, sm) != (pio_offset + n64send_dma_offset_stop)) {}
	    }
	}
    }
}

static void __not_in_flash_func(gpio_irq_handler)(void)
{
    io_irq_ctrl_hw_t *irq_ctrl_base = get_core_num() ?
                                           &iobank0_hw->proc1_irq_ctrl : &iobank0_hw->proc0_irq_ctrl;

    uint gpio = N64_DIO_PIN;
    io_ro_32 *status_reg = &irq_ctrl_base->ints[gpio / 8];
    uint events = (*status_reg >> 4 * (gpio % 8)) & 0xf;

    if (events & GPIO_IRQ_EDGE_FALL) {
	uint32_t cmd = read_command();

//	if (cmd != 0x00 && cmd != 0x01) {
//	printf(": %X\n", cmd);
//	}

        gpio_acknowledge_irq(gpio, events);
    } else {
	printf("unk irq\n");
    }
}

static void __not_in_flash_func(main_loop)(void)
{
    while(1) {
#ifdef USE_GPIO_IRQ
	__wfi();
#else
	while(!gpio_get(N64_DIO_PIN)) {
	}

	while(gpio_get(N64_DIO_PIN)) {
	}

	uint32_t cmd = read_command();
#endif
	if (!gpio_get(N64_DIO_PIN) && memory_pak_changed) {
	    printf("Save memory pak: flash erase ... ");
	    uint32_t ints = save_and_disable_interrupts();
	    core1_disable_irq = true;
	    uint32_t g = multicore_fifo_pop_blocking();


	    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_TARGET_SIZE);
	    printf("write ... ");
	    flash_range_program(FLASH_TARGET_OFFSET, memory_pak, FLASH_TARGET_SIZE);

	    memory_pak_changed = 0;
	    core1_disable_irq = false;

	    multicore_fifo_push_blocking(0x1234);
	    restore_interrupts (ints);
	    printf("done\n");
	}
    }
}

int main(void)
{
    set_sys_clock_khz(200000, true);

    board_init();

    printf("USB to N64 adapter\n");

    if (watchdog_caused_reboot()) {
        TU_LOG2("Rebooted by Watchdog!\n");
    } else {
        TU_LOG2("Clean boot\n");
    }

    printf("clock sys = %d\n", clock_get_hz(clk_sys));

    printf("Load memory pak ... ");
    memory_pak_changed = 0;
    memmove(memory_pak, flash_target_contents, FLASH_TARGET_SIZE);
    printf("done\n");

    gpio_init(N64_DIO_PIN);
    gpio_put(N64_DIO_PIN, 0);
    gpio_pull_up(N64_DIO_PIN);
    gpio_set_dir(N64_DIO_PIN, GPIO_IN);

    {
	printf("PIO DMA enabled\n");

	pio = pio0;

	pio_offset = pio_add_program(pio, &n64send_dma_program);

	sm = pio_claim_unused_sm(pio, true);

	pio_dma_chan = dma_claim_unused_channel(true);

	dma_channel_config pio_dma_chan_config = dma_channel_get_default_config(pio_dma_chan);
	channel_config_set_transfer_data_size(&pio_dma_chan_config, DMA_SIZE_32);
	channel_config_set_read_increment(&pio_dma_chan_config, true);
	channel_config_set_write_increment(&pio_dma_chan_config, false);
	channel_config_set_dreq(&pio_dma_chan_config, pio_get_dreq(pio, sm, true));

	dma_channel_configure(
	    pio_dma_chan,
	    &pio_dma_chan_config,
	    &pio->txf[sm],
	    NULL,
	    0,
	    false
	);

	pio_sm_config c = n64send_dma_program_get_default_config(pio_offset);

	sm_config_set_in_shift(&c, false, false, 32);
	sm_config_set_out_shift(&c, false, false, 32);

	sm_config_set_in_pins(&c, N64_DIO_PIN);
	sm_config_set_out_pins(&c, N64_DIO_PIN, 1);
	sm_config_set_set_pins(&c, N64_DIO_PIN, 1);

	pio_gpio_init(pio, N64_DIO_PIN);

	pio_sm_set_consecutive_pindirs(pio, sm, N64_DIO_PIN, 1, false);

	sm_config_set_clkdiv(&c, 16.625f);

	sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

	pio_sm_init(pio, sm, pio_offset, &c);

	pio_sm_set_enabled(pio, sm, true);
    }

    core1_disable_irq = false;

    multicore_reset_core1();
    multicore_launch_core1(usb_host_process);

    TU_LOG2("Controller enabled.\n");

#ifdef USE_GPIO_IRQ
    gpio_acknowledge_irq(N64_DIO_PIN, GPIO_IRQ_EDGE_FALL);
    gpio_set_irq_enabled(N64_DIO_PIN, GPIO_IRQ_EDGE_FALL, true);

    irq_set_exclusive_handler(IO_IRQ_BANK0, gpio_irq_handler);
    irq_set_enabled(IO_IRQ_BANK0, true);

    printf("GPIO IRQ enabled\n");
#endif

    main_loop();

    return 0;
}

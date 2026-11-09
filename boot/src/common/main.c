/*
 * Copyright (C) 2023-2024 OpenMV, LLC.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Any redistribution, use, or modification in source or binary form
 *    is done solely for personal benefit and not for any commercial
 *    purpose or for monetary gain. For commercial licensing options,
 *    please contact openmv@openmv.io
 *
 * THIS SOFTWARE IS PROVIDED BY THE LICENSOR AND COPYRIGHT OWNER "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE LICENSOR OR COPYRIGHT
 * OWNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * DFU bootloader main.
 */
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "boot_config.h"
#include "dfu.h"
#include "header.h"

omv_dfu_state_t dfu_state = DFU_STATE_DETACHED;

__attribute__((section(".boot_header"), used))
const omv_boot_header_t omv_boot_header = {
    .magic = OMV_BOOT_MAGIC_VALUE,
    .major = OMV_BOOT_VERSION_MAJOR,
    .minor = OMV_BOOT_VERSION_MINOR,
    .patch = OMV_BOOT_VERSION_PATCH,
    .reserved = {0},
};

#define check_timeout_ms(start_ms, timeout) \
    ((port_ticks_ms() - (start_ms)) > (timeout))

int main(void) {
    // Initialize low-level sub-systems.
    // NOTE: in OMV_BOOT_SKIP_USB builds, this does NOT enable USB.
    port_init();

    // Read magic BEFORE any USB activity.
    bool forced = false;
    #ifdef OMV_BOOT_MAGIC_ADDR
    forced = OMV_BOOT_MAGIC_VALUE == *((uint32_t *) OMV_BOOT_MAGIC_ADDR);
    *((uint32_t *) OMV_BOOT_MAGIC_ADDR) = 0;
    #endif

    #ifdef OMV_BOOT_SKIP_USB
    // Fast path: skip USB unless the magic was set explicitly.
    if (forced) {
        port_usb_init();
        tud_init(TUD_OPT_RHPORT);

        // Wait for enumeration. Give the host 1s since the user
        // explicitly requested DFU (IDE "Run" or pyb.dfu()).
        uint32_t start_ms = port_ticks_ms();
        while (true) {
            tud_task();
            if (tud_mounted() || check_timeout_ms(start_ms, 1000)) {
                break;
            }
            port_led_blink(DFU_LED_BLINK_FAST);
        }

        // Wait for DFU commands until the host disconnects or detaches.
        start_ms = port_ticks_ms();
        while (tud_mounted()) {
            tud_task();

            switch (dfu_state) {
                case DFU_STATE_RESET:
                    break;
                case DFU_STATE_ATTACHED:
                    port_led_blink(DFU_LED_BLINK_SLOW);
                    continue;
                case DFU_STATE_DETACHED:
                    if (check_timeout_ms(start_ms, OMV_BOOT_DFU_TIMEOUT)) {
                        break;
                    }
                    port_led_blink(DFU_LED_BLINK_FAST);
                    continue;
            }
            break;
        }

        tud_disconnect();
        tud_deinit(TUD_OPT_RHPORT);
        port_usb_deinit();
    }
    #else
    // Standard path: USB is already enabled in port_init().
    tud_init(TUD_OPT_RHPORT);

    uint32_t start_ms = port_ticks_ms();

    while (true) {
        tud_task();
        if (tud_mounted() || check_timeout_ms(start_ms, 100)) {
            break;
        }
        port_led_blink(DFU_LED_BLINK_FAST);
    }

    start_ms = port_ticks_ms();

    while (tud_mounted()) {
        tud_task();

        switch (dfu_state) {
            case DFU_STATE_RESET:
                break;
            case DFU_STATE_ATTACHED:
                port_led_blink(DFU_LED_BLINK_SLOW);
                continue;
            case DFU_STATE_DETACHED:
                if (!forced && check_timeout_ms(start_ms, OMV_BOOT_DFU_TIMEOUT)) {
                    break;
                }
                port_led_blink(DFU_LED_BLINK_FAST);
                continue;
        }
        break;
    }

    tud_disconnect();
    tud_deinit(TUD_OPT_RHPORT);
    #endif

    // Deinitialize low-level sub-systems.
    port_deinit();

    // JUMP!
    ((void (*) (void)) (*((uint32_t *) (OMV_BOOT_JUMP + 4)))) ();
}
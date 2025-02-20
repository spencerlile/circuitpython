/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016-2017 Scott Shawcroft for Adafruit Industries
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

#include <stdint.h>
#include <string.h>

#include "extmod/vfs.h"
#include "extmod/vfs_fat.h"

#include "genhdr/mpversion.h"
#include "py/nlr.h"
#include "py/compile.h"
#include "py/frozenmod.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/stackctrl.h"

#include "lib/mp-readline/readline.h"
#include "lib/utils/pyexec.h"

#include "mpconfigboard.h"
#include "supervisor/port.h"
#include "supervisor/filesystem.h"
// TODO(tannewt): Figure out how to choose language at compile time.
#include "supervisor/messages/en-US.h"
#include "supervisor/shared/autoreload.h"
#include "supervisor/shared/rgb_led_status.h"
#include "supervisor/serial.h"

void do_str(const char *src, mp_parse_input_kind_t input_kind) {
    mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
    if (lex == NULL) {
        //printf("MemoryError: lexer could not allocate memory\n");
        return;
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, input_kind);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, MP_EMIT_OPT_NONE, true);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        // uncaught exception
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}

static char heap[PORT_HEAP_SIZE];

void reset_mp(void) {
    reset_status_led();
    autoreload_stop();

    // Sync the file systems in case any used RAM from the GC to cache. As soon
    // as we re-init the GC all bets are off on the cache.
    filesystem_flush();

    // Clear the readline history. It references the heap we're about to destroy.
    readline_init0();

    #if MICROPY_ENABLE_GC
    gc_init(heap, heap + sizeof(heap));
    #endif
    mp_init();
    mp_obj_list_init(mp_sys_path, 0);
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_)); // current dir (or base dir of the script)
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_));
    // Frozen modules are in their own pseudo-dir, e.g., ".frozen".
    // Prioritize .frozen over /lib.
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_FROZEN_FAKE_DIR_QSTR));
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_lib));

    mp_obj_list_init(mp_sys_argv, 0);
}
#define STRING_LIST(...) {__VA_ARGS__, ""}

// Look for the first file that exists in the list of filenames, using mp_import_stat().
// Return its index. If no file found, return -1.
const char* first_existing_file_in_list(const char ** filenames) {
    for (int i = 0; filenames[i] != (char*)""; i++) {
        mp_import_stat_t stat = mp_import_stat(filenames[i]);
        if (stat == MP_IMPORT_STAT_FILE) {
            return filenames[i];
        }
    }
    return NULL;
}

bool maybe_run_list(const char ** filenames, pyexec_result_t* exec_result) {
    const char* filename = first_existing_file_in_list(filenames);
    if (filename == NULL) {
        return false;
    }
    mp_hal_stdout_tx_str(filename);
    mp_hal_stdout_tx_str(MSG_OUTPUT_SUFFIX);
    pyexec_file(filename, exec_result);
    return true;
}

bool start_mp(safe_mode_t safe_mode) {
    bool serial_connected_at_start = serial_connected();
    #ifdef CIRCUITPY_AUTORELOAD_DELAY_MS
    if (serial_connected_at_start) {
        serial_write(MSG_NEWLINE);
        if (autoreload_is_enabled()) {
            serial_write(MSG_AUTORELOAD_ON);
        } else if (safe_mode != NO_SAFE_MODE) {
            serial_write(MSG_SAFE_MODE_ON);
        } else if (!autoreload_is_enabled()) {
            serial_write(MSG_AUTORELOAD_OFF);
        }
    }
    #endif

    pyexec_result_t result;

    result.return_code = 0;
    result.exception_type = NULL;
    result.exception_line = 0;

    bool found_main = false;

    if (safe_mode != NO_SAFE_MODE) {
        serial_write(MSG_SAFE_MODE_NO_MAIN);
    } else {
        new_status_color(MAIN_RUNNING);

        const char *supported_filenames[] = STRING_LIST("code.txt", "code.py", "main.py", "main.txt");
        const char *double_extension_filenames[] = STRING_LIST("code.txt.py", "code.py.txt", "code.txt.txt","code.py.py",
                                                    "main.txt.py", "main.py.txt", "main.txt.txt","main.py.py");
        reset_mp();
        found_main = maybe_run_list(supported_filenames, &result);
        if (!found_main){
            found_main = maybe_run_list(double_extension_filenames, &result);
            if (found_main) {
                serial_write(MSG_DOUBLE_FILE_EXTENSION);
            }
        }

        reset_port();
        reset_board();
        reset_status_led();

        if (result.return_code & PYEXEC_FORCED_EXIT) {
            return reload_requested;
        }
    }

    // Wait for connection or character.
    bool serial_connected_before_animation = false;
    rgb_status_animation_t animation;
    prep_rgb_status_animation(&result, found_main, safe_mode, &animation);
    while (true) {
        #ifdef MICROPY_VM_HOOK_LOOP
            MICROPY_VM_HOOK_LOOP
        #endif
        if (reload_requested) {
            return true;
        }

        if (serial_connected() && serial_bytes_available()) {
            // Skip REPL if reload was requested.
            return (serial_read() == CHAR_CTRL_D);
        }

        if (!serial_connected_before_animation && serial_connected()) {
            if (serial_connected_at_start) {
                serial_write(MSG_NEWLINE MSG_NEWLINE);
            }

            if (!serial_connected_at_start) {
                if (autoreload_is_enabled()) {
                    serial_write(MSG_AUTORELOAD_ON);
                } else {
                    serial_write(MSG_AUTORELOAD_OFF);
                }
            }
            // Output a user safe mode string if its set.
            #ifdef BOARD_USER_SAFE_MODE
            if (safe_mode == USER_SAFE_MODE) {
                serial_write(MSG_NEWLINE MSG_SAFE_MODE_USER_REQUESTED);
                serial_write(BOARD_USER_SAFE_MODE_ACTION);
                serial_write(MSG_NEWLINE MSG_SAFE_MODE_USER_EXIT);
                serial_write(BOARD_USER_SAFE_MODE_ACTION);
                serial_write(MSG_NEWLINE);
            } else
            #endif
            if (safe_mode != NO_SAFE_MODE) {
                serial_write(MSG_NEWLINE MSG_BAD_SAFE_MODE MSG_NEWLINE);
                if (safe_mode == HARD_CRASH) {
                    serial_write(MSG_SAFE_MODE_CRASH MSG_NEWLINE);
                    serial_write(MSG_SAFE_MODE_FILE_ISSUE MSG_NEWLINE);
                    serial_write(MSG_SAFE_MODE_ISSUE_LINK MSG_NEWLINE);
                } else if (safe_mode == BROWNOUT) {
                    serial_write(MSG_SAFE_MODE_BROWN_OUT_LINE_1 MSG_NEWLINE);
                    serial_write(MSG_SAFE_MODE_BROWN_OUT_LINE_2 MSG_NEWLINE);
                }
            }
            serial_write(MSG_NEWLINE MSG_WAIT_BEFORE_REPL MSG_NEWLINE);
        }
        if (serial_connected_before_animation && !serial_connected()) {
            serial_connected_at_start = false;
        }
        serial_connected_before_animation = serial_connected();

        tick_rgb_status_animation(&animation);
    }
}

void __attribute__ ((noinline)) run_boot_py(safe_mode_t safe_mode) {
    // If not in safe mode, run boot before initing USB and capture output in a
    // file.
    if (filesystem_present() && safe_mode == NO_SAFE_MODE && MP_STATE_VM(vfs_mount_table) != NULL) {
        static const char *boot_py_filenames[] = STRING_LIST("settings.txt", "settings.py", "boot.py", "boot.txt");

        new_status_color(BOOT_RUNNING);

        #ifdef CIRCUITPY_BOOT_OUTPUT_FILE
        FIL file_pointer;
        boot_output_file = &file_pointer;

        // Get the base filesystem.
        FATFS *fs = &((fs_user_mount_t *) MP_STATE_VM(vfs_mount_table)->obj)->fatfs;

        bool have_boot_py = first_existing_file_in_list(boot_py_filenames) != NULL;

        bool skip_boot_output = false;

        // If there's no boot.py file that might write some changing output,
        // read the existing copy of CIRCUITPY_BOOT_OUTPUT_FILE and see if its contents
        // match the version info we would print anyway. If so, skip writing CIRCUITPY_BOOT_OUTPUT_FILE.
        // This saves wear and tear on the flash and also prevents filesystem damage if power is lost
        // during the write, which may happen due to bobbling the power connector or weak power.

        static const size_t NUM_CHARS_TO_COMPARE = 160;
        if (!have_boot_py && f_open(fs, boot_output_file, CIRCUITPY_BOOT_OUTPUT_FILE, FA_READ) == FR_OK) {

            char file_contents[NUM_CHARS_TO_COMPARE];
            UINT chars_read = 0;
            f_read(boot_output_file, file_contents, NUM_CHARS_TO_COMPARE, &chars_read);
            f_close(boot_output_file);
            skip_boot_output =
                // + 2 accounts for  \r\n.
                chars_read == strlen(MICROPY_FULL_VERSION_INFO) + 2 &&
                strncmp(file_contents, MICROPY_FULL_VERSION_INFO, strlen(MICROPY_FULL_VERSION_INFO)) == 0;
        }

        if (!skip_boot_output) {
            // Wait 1.5 seconds before opening CIRCUITPY_BOOT_OUTPUT_FILE for write,
            // in case power is momentary or will fail shortly due to, say a low, battery.
            mp_hal_delay_ms(1500);

            // USB isn't up, so we can write the file.
            filesystem_writable_by_python(true);
            f_open(fs, boot_output_file, CIRCUITPY_BOOT_OUTPUT_FILE, FA_WRITE | FA_CREATE_ALWAYS);

            // Switch the filesystem back to non-writable by Python now instead of later,
            // since boot.py might change it back to writable.
            filesystem_writable_by_python(false);

            // Write version info to boot_out.txt.
            mp_hal_stdout_tx_str(MICROPY_FULL_VERSION_INFO);
            mp_hal_stdout_tx_str("\r\n");
        }
        #endif

        // TODO(tannewt): Re-add support for flashing boot error output.
        bool found_boot = maybe_run_list(boot_py_filenames, NULL);
        (void) found_boot;

        #ifdef CIRCUITPY_BOOT_OUTPUT_FILE
        if (!skip_boot_output) {
            f_close(boot_output_file);
            filesystem_flush();
        }
        boot_output_file = NULL;
        #endif

        // Reset to remove any state that boot.py setup. It should only be used to
        // change internal state that's not in the heap.
        reset_port();
        reset_mp();
    }
}

int __attribute__((used)) main(void) {
    // initialise the cpu and peripherals
    safe_mode_t safe_mode = port_init();

    rgb_led_status_init();

    // Stack limit should be less than real stack size, so we have a chance
    // to recover from limit hit.  (Limit is measured in bytes.)
    mp_stack_set_top((char*)&_estack);
    mp_stack_set_limit((char*)&_estack - (char*)&_ebss - 1024);

#if MICROPY_MAX_STACK_USAGE
    // _ezero (same as _ebss) is an int, so start 4 bytes above it.
    mp_stack_set_bottom(&_ezero + 1);
    mp_stack_fill_with_sentinel();
#endif

    // Create a new filesystem only if we're not in a safe mode.
    // A power brownout here could make it appear as if there's
    // no SPI flash filesystem, and we might erase the existing one.
    filesystem_init(safe_mode == NO_SAFE_MODE, false);

    // Reset everything and prep MicroPython to run boot.py.
    reset_port();
    reset_board();
    reset_mp();

    // Turn on autoreload by default but before boot.py in case it wants to change it.
    autoreload_enable();

    // By default our internal flash is readonly to local python code and
    // writable over USB. Set it here so that boot.py can change it.
    filesystem_writable_by_python(false);

    run_boot_py(safe_mode);

    // Start serial and HID after giving boot.py a chance to tweak behavior.
    serial_init();

    // Boot script is finished, so now go into REPL/main mode.
    int exit_code = PYEXEC_FORCED_EXIT;
    bool skip_repl = true;
    bool first_run = true;
    for (;;) {
        if (!skip_repl) {
            reset_mp();
            autoreload_suspend();
            new_status_color(REPL_RUNNING);
            if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
                exit_code = pyexec_raw_repl();
            } else {
                exit_code = pyexec_friendly_repl();
            }
            autoreload_resume();
            reset_port();
            reset_board();
        }
        if (exit_code == PYEXEC_FORCED_EXIT) {
            if (!first_run) {
                serial_write(MSG_SOFT_REBOOT MSG_NEWLINE);
            }
            first_run = false;
            skip_repl = start_mp(safe_mode);
        } else if (exit_code != 0) {
            break;
        }
    }
    mp_deinit();
    return 0;
}

void gc_collect(void) {
    // WARNING: This gc_collect implementation doesn't try to get root
    // pointers from CPU registers, and thus may function incorrectly.
    void *dummy;
    gc_collect_start();
    // This collects root pointers from the VFS mount table. Some of them may
    // have lost their references in the VM even though they are mounted.
    gc_collect_root((void**)&MP_STATE_VM(vfs_mount_table), sizeof(mp_vfs_mount_t) / sizeof(mp_uint_t));
    // This naively collects all object references from an approximate stack
    // range.
    gc_collect_root(&dummy, ((mp_uint_t)&_estack - (mp_uint_t)&dummy) / sizeof(mp_uint_t));
    gc_collect_end();
}

void NORETURN nlr_jump_fail(void *val) {
    HardFault_Handler();
    while (true) {}
}

void NORETURN __fatal_error(const char *msg) {
    HardFault_Handler();
    while (true) {}
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    printf("Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    __fatal_error("Assertion failed");
}
#endif

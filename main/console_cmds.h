/**
 * @file console_cmds.h
 * @brief Registers interactive console commands for the OTA demo
 *        (e.g. "ota" to trigger an update from the Serial Monitor).
 */
#pragma once

/**
 * @brief Initialize the UART REPL console and register custom commands.
 *        Blocking — runs the console loop on the calling task forever,
 *        so call this last from app_main (or spawn it in its own task).
 */
void console_start(void);

#ifndef CONSOLE_REPL_H
#define CONSOLE_REPL_H

#include "esp_err.h"

// Context handler function type
// Called when entering a context to register commands
typedef esp_err_t (*context_init_fn)(void);

// Context cleanup function type
// Called when leaving a context to unregister commands
typedef void (*context_cleanup_fn)(void);

// Register a new context (component/subsystem)
// name: context name (e.g., "scene", "device", "midi")
// init_fn: function to register context-specific commands
// cleanup_fn: function to unregister commands (can be NULL)
esp_err_t console_register_context(const char* name, context_init_fn init_fn, context_cleanup_fn cleanup_fn);

// Initialize and start the console REPL
// This will create an interactive command prompt over USB Serial/JTAG
esp_err_t console_repl_init(void);

#endif // CONSOLE_REPL_H


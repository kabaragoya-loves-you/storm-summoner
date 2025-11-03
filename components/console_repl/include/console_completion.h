#ifndef CONSOLE_COMPLETION_H
#define CONSOLE_COMPLETION_H

#include "linenoise/linenoise.h"

// Initialize the console completion system
void console_completion_init(void);

// Completion callback for Linenoise
void console_completion_callback(const char *buf, linenoiseCompletions *lc);

#endif // CONSOLE_COMPLETION_H


#ifndef SAMPLE_HOLD_CONSOLE_H
#define SAMPLE_HOLD_CONSOLE_H

// Initialize Sample+Hold console commands
void sample_hold_console_init(void);

// Get list of registered command names
const char* const* sample_hold_console_get_commands(int* count);

#endif // SAMPLE_HOLD_CONSOLE_H

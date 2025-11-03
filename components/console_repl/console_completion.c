#include "console_completion.h"
#include "esp_console.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "console_completion";

// Global completion callback for Linenoise
void console_completion_callback(const char *buf, linenoiseCompletions *lc) {
  // This will be called by Linenoise for tab completion
  // The ESP console library handles command name completion automatically
  // This callback can be extended for custom argument completion in the future
  
  // For now, we rely on the ESP console's built-in completion
  // Future enhancement: add argument-specific completions here
}

void console_completion_init(void) {
  ESP_LOGD(TAG, "Initializing console completion");
  linenoiseSetCompletionCallback(console_completion_callback);
}


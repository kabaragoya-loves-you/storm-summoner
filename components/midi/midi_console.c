#include "midi_console.h"
#include "midi_out.h"
#include "midi_passthrough.h"
#include "midi_loopback.h"
#include "midi_out_uart.h"
#include "midi_in_debug.h"
#include "device_config.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "midi_console";

static const char* registered_commands[] = {
  "info", "channel", "interfaces", "uart_mode", "passthrough", "loopback", "debug"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  midi_out_config_t cfg = midi_out_get_config();
  bool usb_to_uart_pass = midi_passthrough_usb_to_uart_is_enabled();
  bool uart_to_usb_pass = midi_passthrough_uart_to_usb_is_enabled();
  bool uart_loop = midi_loopback_uart_is_enabled();
  bool usb_loop = midi_loopback_usb_is_enabled();
  uint8_t channel = device_config_get_channel();
  
  const char* iface_str;
  switch (cfg.active_interfaces) {
    case MIDI_OUT_INTERFACE_NONE: iface_str = "None"; break;
    case MIDI_OUT_INTERFACE_UART: iface_str = "UART only"; break;
    case MIDI_OUT_INTERFACE_USB: iface_str = "USB only"; break;
    case MIDI_OUT_INTERFACE_BOTH: iface_str = "Both"; break;
    default: iface_str = "Unknown"; break;
  }
  
  ESP_LOGI(TAG, "====== MIDI CONFIG ======");
  ESP_LOGI(TAG, "MIDI channel: %d", channel);
  ESP_LOGI(TAG, "Active interfaces: %s", iface_str);
  ESP_LOGI(TAG, "UART tempo: %s", cfg.uart_send_tempo ? "enabled" : "disabled");
  ESP_LOGI(TAG, "UART transport: %s", cfg.uart_send_transport ? "enabled" : "disabled");
  ESP_LOGI(TAG, "USB tempo: %s", cfg.usb_send_tempo ? "enabled" : "disabled");
  ESP_LOGI(TAG, "USB transport: %s", cfg.usb_send_transport ? "enabled" : "disabled");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Passthrough USB→UART: %s", usb_to_uart_pass ? "enabled" : "disabled");
  ESP_LOGI(TAG, "Passthrough UART→USB: %s", uart_to_usb_pass ? "enabled" : "disabled");
  ESP_LOGI(TAG, "Loopback UART: %s", uart_loop ? "enabled" : "disabled");
  ESP_LOGI(TAG, "Loopback USB: %s", usb_loop ? "enabled" : "disabled");
  ESP_LOGI(TAG, "MIDI IN debug: %s", midi_in_debug_is_enabled() ? "enabled" : "disabled");
  ESP_LOGI(TAG, "=========================");
  
  return 0;
}

// Command: channel
static struct {
  struct arg_int *channel_num;
  struct arg_end *end;
} channel_args;

static int cmd_channel(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &channel_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, channel_args.end, argv[0]);
    return 1;
  }
  
  int ch = channel_args.channel_num->ival[0];
  if (ch < 1 || ch > 16) {
    ESP_LOGE(TAG, "Channel must be 1-16");
    return 1;
  }
  
  esp_err_t ret = device_config_set_channel(ch);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "MIDI channel set to %d", ch);
  } else {
    ESP_LOGE(TAG, "Failed to set channel: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: interfaces
static struct {
  struct arg_str *iface;
  struct arg_end *end;
} interfaces_args;

static int cmd_interfaces(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &interfaces_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, interfaces_args.end, argv[0]);
    return 1;
  }
  
  const char* iface_str = interfaces_args.iface->sval[0];
  midi_out_interface_t iface;
  
  if (strcmp(iface_str, "none") == 0) {
    iface = MIDI_OUT_INTERFACE_NONE;
  } else if (strcmp(iface_str, "uart") == 0) {
    iface = MIDI_OUT_INTERFACE_UART;
  } else if (strcmp(iface_str, "usb") == 0) {
    iface = MIDI_OUT_INTERFACE_USB;
  } else if (strcmp(iface_str, "both") == 0) {
    iface = MIDI_OUT_INTERFACE_BOTH;
  } else {
    ESP_LOGE(TAG, "Unknown interface. Use: none, uart, usb, or both");
    return 1;
  }
  
  midi_out_set_interfaces(iface);
  ESP_LOGI(TAG, "MIDI interfaces set to: %s", iface_str);
  
  return 0;
}

// Command: uart_mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} uart_mode_args;

static int cmd_uart_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &uart_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, uart_mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode_str = uart_mode_args.mode->sval[0];
  midi_transmit_mode_t mode;
  
  if (strcmp(mode_str, "both") == 0) {
    mode = MIDI_TRANSMIT_BOTH;
  } else if (strcmp(mode_str, "type_a") == 0 || strcmp(mode_str, "a") == 0) {
    mode = MIDI_TRANSMIT_TYPE_A;
  } else if (strcmp(mode_str, "type_b") == 0 || strcmp(mode_str, "b") == 0) {
    mode = MIDI_TRANSMIT_TYPE_B;
  } else if (strcmp(mode_str, "ts") == 0) {
    mode = MIDI_TRANSMIT_TS;
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: both, type_a, type_b, or ts");
    return 1;
  }
  
  midi_out_uart_set_mode(mode);
  ESP_LOGI(TAG, "UART transmit mode set to: %s", mode_str);
  
  return 0;
}

// Command: passthrough
static struct {
  struct arg_str *direction;
  struct arg_str *enable;
  struct arg_end *end;
} passthrough_args;

static int cmd_passthrough(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &passthrough_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, passthrough_args.end, argv[0]);
    return 1;
  }
  
  const char* dir_str = passthrough_args.direction->sval[0];
  const char* enable_str = passthrough_args.enable->sval[0];
  
  bool enable;
  if (strcmp(enable_str, "on") == 0 || strcmp(enable_str, "1") == 0) {
    enable = true;
  } else if (strcmp(enable_str, "off") == 0 || strcmp(enable_str, "0") == 0) {
    enable = false;
  } else {
    ESP_LOGE(TAG, "Use: on or off");
    return 1;
  }
  
  if (strcmp(dir_str, "usb_to_uart") == 0) {
    midi_passthrough_usb_to_uart_enable(enable);
    ESP_LOGI(TAG, "USB→UART passthrough: %s", enable ? "enabled" : "disabled");
  } else if (strcmp(dir_str, "uart_to_usb") == 0) {
    midi_passthrough_uart_to_usb_enable(enable);
    ESP_LOGI(TAG, "UART→USB passthrough: %s", enable ? "enabled" : "disabled");
  } else {
    ESP_LOGE(TAG, "Unknown direction. Use: usb_to_uart or uart_to_usb");
    return 1;
  }
  
  return 0;
}

// Command: loopback
static struct {
  struct arg_str *interface;
  struct arg_str *enable;
  struct arg_end *end;
} loopback_args;

static int cmd_loopback(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &loopback_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, loopback_args.end, argv[0]);
    return 1;
  }
  
  const char* iface_str = loopback_args.interface->sval[0];
  const char* enable_str = loopback_args.enable->sval[0];
  
  bool enable;
  if (strcmp(enable_str, "on") == 0 || strcmp(enable_str, "1") == 0) {
    enable = true;
  } else if (strcmp(enable_str, "off") == 0 || strcmp(enable_str, "0") == 0) {
    enable = false;
  } else {
    ESP_LOGE(TAG, "Use: on or off");
    return 1;
  }
  
  if (strcmp(iface_str, "uart") == 0) {
    midi_loopback_uart_enable(enable);
    ESP_LOGI(TAG, "UART loopback: %s", enable ? "enabled" : "disabled");
  } else if (strcmp(iface_str, "usb") == 0) {
    midi_loopback_usb_enable(enable);
    ESP_LOGI(TAG, "USB loopback: %s", enable ? "enabled" : "disabled");
  } else {
    ESP_LOGE(TAG, "Unknown interface. Use: uart or usb");
    return 1;
  }
  
  return 0;
}

// Command: debug
static struct {
  struct arg_str *enable;
  struct arg_end *end;
} debug_args;

static int cmd_debug(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &debug_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, debug_args.end, argv[0]);
    return 1;
  }
  
  const char* enable_str = debug_args.enable->sval[0];
  
  if (strcmp(enable_str, "on") == 0 || strcmp(enable_str, "1") == 0) {
    midi_in_debug_enable();
    ESP_LOGI(TAG, "MIDI IN debug: enabled");
  } else if (strcmp(enable_str, "off") == 0 || strcmp(enable_str, "0") == 0) {
    midi_in_debug_disable();
    ESP_LOGI(TAG, "MIDI IN debug: disabled");
  } else {
    ESP_LOGE(TAG, "Use: on or off");
    return 1;
  }
  
  return 0;
}

esp_err_t midi_console_init(void) {
  ESP_LOGI(TAG, "Registering midi commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show MIDI configuration",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // channel command
  channel_args.channel_num = arg_int1(NULL, NULL, "<1-16>", "MIDI channel");
  channel_args.end = arg_end(2);
  
  const esp_console_cmd_t channel_cmd = {
    .command = "channel",
    .help = "Set global MIDI channel",
    .hint = NULL,
    .func = &cmd_channel,
    .argtable = &channel_args
  };
  esp_console_cmd_register(&channel_cmd);
  
  // interfaces command
  interfaces_args.iface = arg_str1(NULL, NULL, "<none|uart|usb|both>", "Active interfaces");
  interfaces_args.end = arg_end(2);
  
  const esp_console_cmd_t interfaces_cmd = {
    .command = "interfaces",
    .help = "Set active MIDI interfaces",
    .hint = NULL,
    .func = &cmd_interfaces,
    .argtable = &interfaces_args
  };
  esp_console_cmd_register(&interfaces_cmd);
  
  // uart_mode command
  uart_mode_args.mode = arg_str1(NULL, NULL, "<both|type_a|type_b|ts>", "UART transmit mode");
  uart_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t uart_mode_cmd = {
    .command = "uart_mode",
    .help = "Set UART transmit mode",
    .hint = NULL,
    .func = &cmd_uart_mode,
    .argtable = &uart_mode_args
  };
  esp_console_cmd_register(&uart_mode_cmd);
  
  // passthrough command
  passthrough_args.direction = arg_str1(NULL, NULL, "<usb_to_uart|uart_to_usb>", "Direction");
  passthrough_args.enable = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  passthrough_args.end = arg_end(3);
  
  const esp_console_cmd_t passthrough_cmd = {
    .command = "passthrough",
    .help = "Control MIDI passthrough",
    .hint = NULL,
    .func = &cmd_passthrough,
    .argtable = &passthrough_args
  };
  esp_console_cmd_register(&passthrough_cmd);
  
  // loopback command
  loopback_args.interface = arg_str1(NULL, NULL, "<uart|usb>", "Interface");
  loopback_args.enable = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  loopback_args.end = arg_end(3);
  
  const esp_console_cmd_t loopback_cmd = {
    .command = "loopback",
    .help = "Control MIDI loopback",
    .hint = NULL,
    .func = &cmd_loopback,
    .argtable = &loopback_args
  };
  esp_console_cmd_register(&loopback_cmd);
  
  // debug command
  debug_args.enable = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  debug_args.end = arg_end(2);
  
  const esp_console_cmd_t debug_cmd = {
    .command = "debug",
    .help = "Enable/disable MIDI IN debug logging",
    .hint = NULL,
    .func = &cmd_debug,
    .argtable = &debug_args
  };
  esp_console_cmd_register(&debug_cmd);
  
  return ESP_OK;
}

void midi_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering midi commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}


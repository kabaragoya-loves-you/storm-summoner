#ifndef _MIDI_IN_PARSER_H_
#define _MIDI_IN_PARSER_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MIDI parser
 * Allocates buffers and initializes SysEx update handler
 */
void midi_in_parser_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _MIDI_IN_PARSER_H_ */

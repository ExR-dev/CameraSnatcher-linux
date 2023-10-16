#ifndef INCLUDE_INPUT_HANDLER_H
#define INCLUDE_INPUT_HANDLER_H

#include <SDL2/SDL.h>


typedef enum Increment_Type
{
    CONTINUOUS, // Increment while holding down.
    STEPWISE, // Increment when pressed.
    TOGGLE // Invert when pressed.
} Increment_Type;

typedef struct Key_Mapping
{
    void *ptr; // Pointer to the mapped variable.
    char *name; // The display name of the mapping.
    int SDL_key; // The key associated with this mapping.
    Increment_Type incr_type;
    float step; // The size of an increment.
} Key_Mapping;


int handle_keypresses(const Key_Mapping mappings[], int m_count, const Uint8 *state, const Uint8 *last_state);

#endif
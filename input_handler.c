
#include "include/input_handler.h"

#include "include/img_data.h"

#include <stdio.h>
#include <SDL2/SDL.h>
#include <stdbool.h>


int handle_keypresses(const Key_Mapping mappings[], int m_count, const Uint8 *state, const Uint8 last_state[])
{
    if (state[SDL_SCANCODE_ESCAPE] == 1)
    {
        printf("Pressed escape.\n");
        return 1; // Return escape code.
    }

    if (state[SDL_SCANCODE_TAB] == 1 && last_state[SDL_SCANCODE_TAB] != 1)
    {
        // Print current settings.
        printf("\n");
        for (int i = 0; i < m_count; i++)
        {
            printf("%-18s : ", mappings[i].name);

            switch (mappings[i].incr_type)
            {
            case CONTINUOUS:
                printf("%6.2f", *(float*)mappings[i].ptr);
                break;

            case STEPWISE:
                printf("%6d", (int)(*(float*)mappings[i].ptr));
                break;

            case TOGGLE:
                printf("%6s", *(float*)mappings[i].ptr == 0.0f ? "false" : "true");
                break;
            }
            printf("\n");
        }
        printf("\n");
        return 0;
    }


    bool    up = 1 == state[SDL_SCANCODE_UP];
    bool  down = 1 == state[SDL_SCANCODE_DOWN];
    bool   lup = 1 == last_state[SDL_SCANCODE_UP];
    bool ldown = 1 == last_state[SDL_SCANCODE_DOWN];
    bool  mult = 1 == state[SDL_SCANCODE_M];
    bool shift = 1 <= state[SDL_SCANCODE_LSHIFT] + state[SDL_SCANCODE_RSHIFT];
    bool   alt = 1 <= state[SDL_SCANCODE_LALT] + state[SDL_SCANCODE_RALT];

    for (int i = 0; i < m_count; i++)
    {
        // Skip if the key to this mapping is not being pressed.
        if (state[mappings[i].SDL_key] != 1)
            continue;
        
        float last_val = *(float*)mappings[i].ptr; // Store value before change.
        switch (mappings[i].incr_type)
        {
        case CONTINUOUS:
            if (last_state[mappings[i].SDL_key] != 1)
                printf("%s: %.2f\n", mappings[i].name, last_val);

            if (mult)
            {
                // Increase multiplicatively.
                if      (up)   *(float*)mappings[i].ptr *= (shift ? 1.25f : (alt ? 1.01f : 1.05f));
                else if (down) *(float*)mappings[i].ptr *= (shift ? 0.8f : (alt ? 0.99f : 0.95f));
            }
            else
            {
                // Increase additively.
                if (up)         *(float*)mappings[i].ptr += mappings[i].step * (shift ? 10.0f : (alt ? 0.1f : 1.0f));
                else if (down)  *(float*)mappings[i].ptr -= mappings[i].step * (shift ? 10.0f : (alt ? 0.1f : 1.0f));
            }

            *(float*)mappings[i].ptr = MAX(0.0f, *(float*)mappings[i].ptr);

            if (*(float*)mappings[i].ptr != last_val)
                printf("%s: %.2f\n", mappings[i].name, *(float*)mappings[i].ptr);
            break;

        case STEPWISE:
            if (last_state[mappings[i].SDL_key] != 1)
                printf("%s: %.2f\n", mappings[i].name, last_val);

            if      (up && !lup)     *(float*)mappings[i].ptr += mappings[i].step;
            else if (down && !ldown) *(float*)mappings[i].ptr -= mappings[i].step;

            *(float*)mappings[i].ptr = MAX(0.0f, *(float*)mappings[i].ptr);
            
            if (*(float*)mappings[i].ptr != last_val)
                printf("%s: %.2f\n", mappings[i].name, *(float*)mappings[i].ptr);
            break;

        case TOGGLE:
            if (last_state[mappings[i].SDL_key] != 1)
            {
                *(float*)mappings[i].ptr = (*(float*)mappings[i].ptr == 0.0f) ? 1.0f : 0.0f;
                printf("%s: %s\n", mappings[i].name, ((*(float*)mappings[i].ptr == 0.0f) ? "false" : "true"));
            }
            break;
        }
    }
    return 0;
}


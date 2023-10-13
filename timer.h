#ifndef INCLUDE_TIMER_H
#define INCLUDE_TIMER_H

enum timer_type 
{
	FRAME = 1,
	MANIPULATION = 2,
	CONVERSION = 3,
    T_CONVERSION = 4,
	SCAN = 5,
    T_SCAN = 6
};


int timer_begin_measure(enum timer_type type);
int timer_end_measure(enum timer_type type);

int timer_init();
int timer_quit();
int timer_conclude();

#endif
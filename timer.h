#ifndef INCLUDE_TIMER_H
#define INCLUDE_TIMER_H

enum timer_type 
{
	FRAME = 1,
	CONVERSION = 2,
    T_CONVERSION = 3,
	SCAN = 4,
    T_SCAN = 5
};


int timer_begin_measure(enum timer_type type);
int timer_end_measure(enum timer_type type);

int timer_init();
int timer_quit();
int timer_conclude();

#endif
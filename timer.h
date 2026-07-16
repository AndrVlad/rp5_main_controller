#ifndef TIMER_H
#define TIMER_H

#include <iostream>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <atomic>
#include <thread>
#include <chrono>

bool is_timer_ovflw();
void deinit_timer();
void start_timer(int time_minutes);
void stop_timer();

#endif

#ifndef TIMER_H
#define TIMER_H

#include <iostream>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <atomic>
#include <thread>
#include <chrono>

#define TIM_HACKRF 0
#define TIM_WAIT_ACK 1

bool is_timer_ovflw(int id);
void stop_timer(int id);
void deinit_timer(int id);
void deinit_timer();
void start_timer(int id, int time_seconds);
void stop_timer();

#endif

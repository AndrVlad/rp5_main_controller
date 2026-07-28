#include "timer.h"

bool timer_ovflw = false;

timer_t timerid;
struct sigevent sev;
struct itimerspec its;
struct sigaction sa;

bool is_timer_ovflw() {
    return timer_ovflw;
}

void deinit_timer() {
    stop_timer();
    timer_delete(timerid);
    return;
}

// Обработчик сигнала таймера
void timer_handler(int sig, siginfo_t *si, void *uc) {
    timer_ovflw = true;
    return;
}

void start_timer(int time_minutes) {
    
    sa.sa_flags = SA_SIGINFO;  
    sa.sa_sigaction = timer_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGRTMIN, &sa, NULL);

    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    sev.sigev_value.sival_ptr = &timerid;
    timer_create(CLOCK_REALTIME, &sev, &timerid);

    its.it_value.tv_sec = time_minutes;//*60;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 0; 
    its.it_interval.tv_nsec = 0;

    timer_settime(timerid, 0, &its, NULL);

    //timer_delete(timerid);

    return;
}

void stop_timer(){

    struct itimerspec its;
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;
    
    timer_ovflw = true;
    timer_settime(timerid, 0, &its, NULL);
    
    return;
}

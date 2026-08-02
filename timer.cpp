#include "timer.h"

// Максимальное количество таймеров, которым можно управлять
#define MAX_TIMERS 5

// Массивы для хранения состояния каждого таймера
bool timer_ovflw[MAX_TIMERS] = {false};
timer_t timerid[MAX_TIMERS];
bool is_created[MAX_TIMERS] = {false}; 

struct sigevent sev;
struct itimerspec its;
struct sigaction sa;

// Проверка переполнения конкретного таймера по его индексу
bool is_timer_ovflw(int id) {
    if (id < 0 || id >= MAX_TIMERS) return false;
    return timer_ovflw[id];
}

// Сброс флага переполнения 
void clear_timer_ovflw(int id) {
    if (id >= 0 && id < MAX_TIMERS) {
        timer_ovflw[id] = false;
    }
}

// Остановка конкретного таймера
void stop_timer(int id) {
    if (id < 0 || id >= MAX_TIMERS || !is_created[id]) return;

    struct itimerspec stop_its;
    stop_its.it_value.tv_sec = 0;
    stop_its.it_value.tv_nsec = 0;
    stop_its.it_interval.tv_sec = 0;
    stop_its.it_interval.tv_nsec = 0;
    
    timer_settime(timerid[id], 0, &stop_its, NULL);
    timer_ovflw[id] = false; 
}

// Полное удаление конкретного таймера
void deinit_timer(int id) {
    if (id < 0 || id >= MAX_TIMERS || !is_created[id]) return;

    stop_timer(id);
    timer_delete(timerid[id]);
    timer_ovflw[id] = false;
    is_created[id] = false;
}

// Единый обработчик сигналов для всех таймеров
void timer_handler(int sig, siginfo_t *si, void *uc) {
    
    int id = si->si_value.sival_int;
    
    if (id >= 0 && id < MAX_TIMERS) {
        timer_ovflw[id] = true;
    }
}

// Запуск таймера с указанием его индекса
void start_timer(int id, int time_seconds) {
    if (id < 0 || id >= MAX_TIMERS) return;

        sa.sa_flags = SA_SIGINFO;  
    sa.sa_sigaction = timer_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGRTMIN, &sa, NULL);

    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    sev.sigev_value.sival_int = id; 

     if (!is_created[id]) {
        timer_create(CLOCK_REALTIME, &sev, &timerid[id]);
        is_created[id] = true;
    }

    timer_ovflw[id] = false;

    its.it_value.tv_sec = time_seconds; 
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 0; 
    its.it_interval.tv_nsec = 0;

    timer_settime(timerid[id], 0, &its, NULL);
}

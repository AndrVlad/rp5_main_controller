#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <signal.h>
#include <cstdlib>
#include <gpiod.hpp>
#include <sys/wait.h>
#include <signal.h>
#include <bits/stdc++.h>
#include <regex>

#include "src/ina219.h"

#include "timer.h"

#define MINIMAL_BATTERY_VOLTAGE_V 10
#define MINIMAL_LOAD_CURRENT_A 1


const char* FILE_PATH = "/mnt/ramdisk/2467.000MHz_20260713_145425_DC+16.iq";

int serial_fd = -1;
int current_state = 2;
int next_state = -1;
int hackrf_cmd, hackrf_cmd_prev = -1;
int poll_sim800c_counter = 0;
std::atomic<bool> running(true);
std::atomic<bool> sms_received(false);
std::atomic<bool> hackrf_running(false);
pid_t hackrf_pid = -1;
std::string sms_text;
std::string sms_sender;
std::string sms_recipient;
bool rx_ok = 0;
bool wait_ans, force_start = 0;

std::string current_action = "NONE";

enum State {WAKE_UP = 1, FORCED_START, POLLING_SIM, DELETING_SMS, CHECKING_SIM_STORAGE, CLEARING_SIM_STORAGE, HACK_RF_INTERACTION, IDLE,TURN_OFF, POWER_OFF};
enum hackRFCMD {STOP_HACKRF = 0, START_HACKRF_INF, START_HACKRF, SET_RECIPIENT_NUM, GET_BAT_VOLTAGE = 10};

INA219* i = nullptr;

const char* stateNames[] = {
    "",  
    "WAKE_UP",
    "FORCED_START",
    "POLLING_SIM",
    "DELETING_SMS",
    "CHECKING_SIM_STORAGE",
    "CLEARING_SIM_STORAGE",
    "HACK_RF_INTERACTION",
    "IDLE",
    "TURN_OFF",
    "POWER_OFF"
};

const char* deviceState[] = {
    "NONE",  
    "CONTINUOUS",
    "TIME"
};

struct sms_t {
    std::string index;
    std::string status;
    std::string source;
    std::string destination;
    std::string date_time;
    std::string text;
} sms{};

struct notification_t {
    std::string sms_storage;
    std::string sms_index;
} notification;



void AT_parser(const std::string& line) {
    return;
}

std::string get_device_state(int state) {
    return deviceState[state];
}

std::string get_notification_sms_index();

int open_port(const char* port, int baudrate) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        std::cerr << " Не удалось открыть порт: " << port << std::endl;
        return -1;
    }
    
    struct termios options;
    tcgetattr(fd, &options);
    
    speed_t speed;
    switch (baudrate) {
        case 9600:   speed = B9600; break;
        case 19200:  speed = B19200; break;
        case 38400:  speed = B38400; break;
        case 57600:  speed = B57600; break;
        case 115200: speed = B115200; break;
        default:     speed = B9600; break;
    }
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
    
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~CRTSCTS;
    
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_iflag &= ~(INLCR | ICRNL | IGNCR);
    
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 10;
    
    tcsetattr(fd, TCSANOW, &options);
    tcflush(fd, TCIOFLUSH);
    
    return fd;
}

void close_port() {
    if (serial_fd != -1) {
        close(serial_fd);
        serial_fd = -1;
    }
}

void send_command(const std::string& cmd) {
    if (serial_fd == -1) return;
    
    std::string data = cmd;
    if (data.find("\r\n") == std::string::npos) {
        data += "\r\n";
    }
    
    write(serial_fd, data.c_str(), data.length());
    std::cout << "[Send]: " << cmd << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void send_sms_content(const std::string& cmd) {
    if (serial_fd == -1) return;
    
    std::string data = cmd;
    data += "\x1A";
    
    write(serial_fd, data.c_str(), data.length());
    std::cout << "[Send]: " << cmd << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

std::string read_line() {
    if (serial_fd == -1) return "";
    
    char buffer[256];
    std::string result;
    
    while (true) {
        int n = read(serial_fd, buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            if (result.empty()) return "";
            break;
        }
        
        buffer[n] = '\0';
        result += buffer;
        
        if (result.length() >= 2 && result.substr(result.length() - 2) == "\r\n") {
            break;
        }
    }
    
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) {
        result.pop_back();
    }

    while (!result.empty() && (result.front() == '\r' || result.front() == '\n')) {
        result.erase(0, 1);
    }
    
    return result;
}

void set_current_action (int action) {
    current_action = deviceState[action];
}

void signal_handler(int sig) {
    if (sig == SIGINT) {
        std::cout << "\n\n  Остановка программы..." << std::endl;
        running = false;
    }
}

bool parse_sms_list(const std::string& line) {
    
    std::regex pattern(
        R"(\+CMGL:\s*(\d+),\"([^\"]+)\",\"([^\"]+)\",\"([^\"]*)\",\"([^\"]+)\"\r\n([^\r\n]+))"
    );
    std::smatch match;

    if (std::regex_search(line, match, pattern)) {
        sms.index = match[1].str();
        sms.status = match[2].str();
        sms.source = match[3].str();
        sms.destination = match[4].str();
        sms.date_time = match[5].str();
        sms.text = match[6].str();
        
        std::cout << "Индекс: " << sms.index << std::endl;
        std::cout << "Статус: " << sms.status << std::endl;
        std::cout << "Отправитель: " << sms.source << std::endl;
        std::cout << "Получатель:" << sms.destination << std::endl;
        std::cout << "Дата, время:" << sms.date_time << std::endl;
        std::cout << "Текст:" << sms.text << std::endl;
        
        return true;
    } else {
        std::cerr << "Ошибка: строка не соответствует формату +CMGL" << std::endl;
        std::cout << "Полученные данные от SIM800C " << line << std::endl;
        return false;
    }
}

bool parse_sms(const std::string& line) {
    
    std::regex pattern(
	R"(\+CMGR:\s*\"([^\"]+)\",\"([^\"]+)\",\"([^\"]*)\",\"([^\"]+)\"\r\n([^\r\n]+))"
    );
    std::smatch match;
  
    sms.index = get_notification_sms_index();
  
    if (std::regex_search(line, match, pattern)) {
        sms.status = match[1].str();
        sms.source = match[2].str();
        sms.destination = match[3].str();
        sms.date_time = match[4].str();
        sms.text = match[5].str();
        
        std::cout << "Индекс: " << sms.index << std::endl;
        std::cout << "Статус: " << sms.status << std::endl;
        std::cout << "Отправитель: " << sms.source << std::endl;
        std::cout << "Получатель:" << sms.destination << std::endl;
        std::cout << "Дата, время:" << sms.date_time << std::endl;
        std::cout << "Текст:" << sms.text << std::endl;
        
        return true;
    } else {
        std::cerr << "Ошибка: строка не соответствует формату +CMGR" << std::endl;
        std::cout << "Полученные данные от SIM800C " << line << std::endl;
        return false;
    }
}

bool parse_notification(const std::string& line) {
    
    std::regex pattern(
        R"(\+CMTI:\s*\"([^\"]+)\",(\d+))"
    );
    std::smatch match;

    if (std::regex_search(line, match, pattern)) {
        notification.sms_storage = match[1].str();
        notification.sms_index = match[2].str();

        std::cout << "Хранилище СМС: " << notification.sms_storage << std::endl;
        std::cout << "Индекс СМС: " << notification.sms_index << std::endl;
  
        return true;
    } else {
        std::cerr << "Ошибка: строка не соответствует формату +CMTI" << std::endl;
        std::cout << "Полученные данные от SIM800C " << line << std::endl;
        return false;
    }
}

bool is_sim_storage_full(const std::string& line) {
    
    std::regex pattern(
        R"(\+CPMS:\s*\"([^\"]+)\",(\d+),(\d+),\"([^\"]+)\",(\d+),(\d+),\"([^\"]+)\",(\d+),(\d+)\s*)"
    );
    std::smatch match;

    std::string mem1, used1, total1;

    if (std::regex_search(line, match, pattern)) {
        mem1 = match[7].str();
        used1 = match[8].str();
        total1 = match[9].str();
        
        std::cout << "mem3: " << mem1 << std::endl;
        std::cout << "used3: " << used1 << std::endl;
        std::cout << "total3: " << total1 << std::endl;

    } else {
        std::cerr << "Ошибка: строка не соответствует формату +CPMS" << std::endl;
        std::cout << "Полученные данные от SIM800C " << line << std::endl;
        return false;
    }
    
    //if (stoi(used1) >= (stoi(total1) - 1)) {
    if (stoi(used1) >= 1) {
        std::cout << "Заполнено" << std::endl;
        return true;
    } else {
    std::cout << "Не заполнено" << std::endl;
        return false;
    }
    
}

void send_sms(const std::string& content) {
    
     if (sms_recipient == "") {
        std::cout << "The recipient of the message is not known" << std::endl;
        return;
    }
    
    send_command("AT");
    send_command("AT+CMGS=\""+sms_recipient+"\"");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    std::string line = read_line();

    if (line.find(">") != std::string::npos) {
        rx_ok = false;
        send_sms_content(content);
    } else {
        std::cout << "SIM800C did not response on AT+CMGS" << std::endl;
        return;
    }

    std::cout << "SMS send on SIM800C" << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(5));													 
    return;

}

std::string get_sms_index_from_notif() {
    return notification.sms_index;
}

void setState(int next_state) {
    current_state = next_state;
    std::cout << "Changed state on " << stateNames[next_state] << std::endl;
}

std::string get_sms_index(const std::string& line) {
   
    return sms.index; 
}

int get_sms_text(const std::string& line) {
    
    return std::stoi(sms.text);
}

void set_sms_recipient() {
    sms_recipient = sms.source;
    return;
}

std::string get_notification_sms_index() {
    return notification.sms_index;
}

void start_hackrf_transfer(bool loop_transfer) {
    if (hackrf_running) {
        std::cout << "HackRF already running" << std::endl;
        send_sms(current_action+" mode is already running");
        return;
    }
    std::string loop_tx;
    pid_t pid = fork();
    
    if (pid == -1) {
        std::cerr << "fork() error" << std::endl;

        if (!loop_transfer) {
            send_sms("Error: "+get_device_state(2)+" is not running");
        } else {
            send_sms("Error: "+get_device_state(1)+" mode is not running");
        }

        return;
    }
    
    if (pid == 0) {
        
        execlp("hackrf_transfer", 
               "hackrf_transfer",
               "-t", FILE_PATH,
               "-f", "2467000000",
               "-x", "47",
               "-a", "1",
               "-s", "16000000",
               "-R",
               nullptr);
               
        std::cerr << "Ошибка запуска hackrf_transfer" << std::endl;

        if (!loop_transfer) {
            send_sms("Error: "+get_device_state(2)+" mode is not running");
        } else {
            send_sms("Error: "+get_device_state(1)+" mode is not running");
        }
        exit(1);
    }
    
    std::cout << "loop transfer = " << loop_transfer << std::endl;
    if (!loop_transfer) {
        std::cout << "timer started" << std::endl;
        start_timer(180); 
    } 
    
    hackrf_pid = pid;
    hackrf_running = true;
    std::cout << "HackRF запущен (PID: " << pid << ")" << std::endl;

    if (!loop_transfer) {
        set_current_action(2);
													
    } else {
        set_current_action(1);
    }
	
	send_sms(current_action+" mode is running");
	/*
    std::this_thread::sleep_for(std::chrono::seconds(5));

    if (check_load_current()) {
        send_sms(current_action+" mode is running");
    } else {
        hackrf_cmd_prev = -1;
        hackrf_cmd = -1;

        deinit_timer();
        set_current_action(0);
        std::cout << "Error: HackRF transfer is not started. Current val: " << std::to_string() << "A" << std::endl;
        stop_hackrf_transfer();
        std::this_thread::sleep_for(std::chrono::seconds(3));
        send_sms("Error: HackRF transfer is not started. Current mode: NONE");
    } */

    return;
}

void stop_hackrf_transfer() {
    if (hackrf_running && hackrf_pid > 0) {
        std::cout << "Остановка HackRF (PID: " << hackrf_pid << ")" << std::endl;
        kill(hackrf_pid, SIGTERM);  
        
        int status;
        int wait_time = 0;
        while (wait_time < 30) {  
            pid_t result = waitpid(hackrf_pid, &status, WNOHANG);
            if (result == hackrf_pid) {
                hackrf_running = false;
                hackrf_pid = -1;
                std::cout << "HackRF остановлен" << std::endl;
                send_sms("Stopped "+current_action+" mode");
                set_current_action(0);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            wait_time++;
        }
        
        kill(hackrf_pid, SIGKILL);
        waitpid(hackrf_pid, &status, 0);
        hackrf_running = false;
        hackrf_pid = -1;
        std::cout << "HackRF принудительно остановлен" << std::endl;
        send_sms("Stopped "+current_action+" mode");
        set_current_action(0);
    } else {
        std::cout << "None of the process are running" << std::endl;
        send_sms("None of the process are running");
    }
}

bool is_hackrf_transfer_running() {

    if (hackrf_pid <= 0 || !hackrf_running) {
        return false;
    }
    
    int status;
    pid_t result = waitpid(hackrf_pid, &status, WNOHANG);
    
    if (result > 0) {
        hackrf_running = false;
        hackrf_pid = -1;
        return false;
    }
    
    return true;
}

void check_hackrf_transfer() {
    
    if (!is_hackrf_transfer_running() && current_action != "NONE") {
        
        hackrf_cmd_prev = -1;
        hackrf_cmd = -1;

        deinit_timer();
        set_current_action(0);
        std::cout << "Error: HackRF transfer stopped unexpectedly" << std::endl;
        send_sms("Error: HackRF transfer stopped unexpectedly. Current mode: "+current_action);
    }
    
}

void gpio_pin_set(int bcm_pin_num, bool state) {
    const std::string chip_path = "/dev/gpiochip0"; 
    const unsigned int line_offset = bcm_pin_num;
    try {
        auto request = ::gpiod::chip(chip_path)
            .prepare_request()
            .set_consumer("rp5_blink_example")
            .add_line_settings(
                line_offset,
                ::gpiod::line_settings()
                    .set_direction(::gpiod::line::direction::OUTPUT) 
            )
            .do_request(); 
          
      if (state) {
            request.set_value(line_offset, ::gpiod::line::value::ACTIVE);
        } else {
            request.set_value(line_offset, ::gpiod::line::value::INACTIVE);
        }
          
      } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
        return;
    }
}

void gpio_pin_ctrl(int bcm_pin_num, bool state, int delay_mcs=1) {
    
    const std::string chip_path = "/dev/gpiochip0"; 
    const unsigned int line_offset = bcm_pin_num;
      
    try {
        auto request = ::gpiod::chip(chip_path)
            .prepare_request()
            .set_consumer("rp5_blink_example")
            .add_line_settings(
                line_offset,
                ::gpiod::line_settings()
                    .set_direction(::gpiod::line::direction::OUTPUT) 
            )
            .do_request(); 
            
            if (state) {
                request.set_value(line_offset, ::gpiod::line::value::ACTIVE);
                std::this_thread::sleep_for(std::chrono::microseconds(delay_mcs));
                request.set_value(line_offset, ::gpiod::line::value::INACTIVE);
            } else {
                request.set_value(line_offset, ::gpiod::line::value::INACTIVE);
                std::this_thread::sleep_for(std::chrono::microseconds(delay_mcs));
                request.set_value(line_offset, ::gpiod::line::value::ACTIVE);
            }
            
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
        return;
    }
      
}

void powerOff() {
    int result = system("sudo poweroff");
    return;
}

void mount_tmpfs() {
    
    std::cout << "Mounting tempfs ..." << std::endl;
    system("sudo mkdir -p /mnt/ramdisk");
    system("sudo mount -t tmpfs -o size=1500M tmpfs /mnt/ramdisk");
    system("sudo systemctl daemon-reload");
    system("sudo mount -t tmpfs -o size=1500M tmpfs /mnt/ramdisk");
    std::cout << "tempfs mounted" << std::endl;

    return;
}

void copy_files() {
    std::cout << "Copying file..." << std::endl; 
    system("cp ~/2467.000MHz_20260713_145425_DC+16.iq /mnt/ramdisk");
    std::cout << "Copying of file end" << std::endl;
}

void delete_file() {
  system("rm /mnt/ramdisk/2467.000MHz_20260713_145425_DC+16.iq");
}

float get_battery_voltage() {

    float sup_voltage = 0;
    i->wake();
    sup_voltage = i->supply_voltage();
    i->sleep();

    /* ONLY FOR DEBUG */
    //sup_voltage = 12.1;
    sup_voltage = std::trunc(sup_voltage* 10^2) / 10^2;
    return sup_voltage;
}

float get_load_current() {
    float load_current = 0;
    i->wake();
    load_current = i->current();
    /* ONLY FOR DEBUG */
    //load_current = 1000;
    i->sleep();
    load_current = std::trunc(load_current);
    return load_current;
}

void check_battery_voltage() {
    
    float sup_voltage = 0;
    sup_voltage = get_battery_voltage();

    if (sup_voltage <= MINIMAL_BATTERY_VOLTAGE_V) {
        std::string sup_voltage_str = std::to_string(sup_voltage);
        std::cout << "Battery voltage low. Current voltage " << sup_voltage_str << std::endl;
        send_sms("Warning: Battery voltage low. Current voltage = "+sup_voltage_str+" V");
    }
    
    return;
}

bool check_load_current() {
	
    if(is_hackrf_transfer_running() && current_action != "NONE") {
        
        float current = get_load_current();
        if (get_load_current() <= MINIMAL_LOAD_CURRENT_A) {
            return false;
		
        }
        else {
            return true;
        }
    } else {
        return false;
    }
}



void ina219_init() {
    
    float SHUNT_OHMS = 0.1;
    float MAX_EXPECTED_AMPS = 3.2;
    
    i = new INA219(SHUNT_OHMS, MAX_EXPECTED_AMPS);
    i->configure(RANGE_16V, GAIN_8_320MV, ADC_12BIT, ADC_12BIT);

    return;
}

int main() {
    signal(SIGINT, signal_handler);
    
    serial_fd = open_port("/dev/ttyAMA0", 115200);
    if (serial_fd == -1) {
        return 1;
    }
    
    running=true;

    ina219_init();

    //std::this_thread::sleep_for(std::chrono::seconds(15));

    while (running && serial_fd != -1) {
        std::string line = read_line();

        if (!line.empty()) {
            std::cout << "[Received]: " << line << std::endl;
            
            if (line.find("+CMGS:") != std::string::npos) {
                rx_ok = false;
                std::cout << "SMS sending successfull" << std::endl;
                
            }

            if (line.find("+CMS ERROR:") != std::string::npos) {
                rx_ok = false;
                std::cout << "SMS sending error" << std::endl;
                
            } else {  
				rx_ok = true;
            }
        }

        switch (current_state)
        {
	    case FORCED_START:
	        hackrf_cmd = START_HACKRF_INF;
            hackrf_cmd_prev = hackrf_cmd;
            set_current_action(1);
            mount_tmpfs();
            copy_files();
            start_hackrf_transfer(1);
	        setState(WAKE_UP);
            force_start = true;
    	    break;
	    
        case WAKE_UP:
            
            if (!rx_ok) {
                send_command("AT");
		        poll_sim800c_counter++;

                if (poll_sim800c_counter >= 10) {
                    std::cout << "SIM800C doesn't response, start infinitive transfer..." << std::endl;
                    hackrf_cmd = START_HACKRF_INF;
                    mount_tmpfs();
                    copy_files();
                    setState(HACK_RF_INTERACTION);
                
                }

            } else {
                rx_ok = false;
                if (line.find("OK") != std::string::npos) {
                    setState(POLLING_SIM);
                    wait_ans = true;
                    send_command("AT+CMGL=\"REC UNREAD\"");
                }
            }
            break;

        case POLLING_SIM:
            
            if(!rx_ok && wait_ans) {
                break;
            } else if (rx_ok && wait_ans) {

                rx_ok = false;
                wait_ans = false;

                if ((line.find("+CMGL") != std::string::npos)) { // есть непрочитанное сообщение
                
                    if(parse_sms_list(line)) {
                    
                        std::cout << "Сообщение разобрано" << std::endl;
                        hackrf_cmd = get_sms_text(line);
        
                        if(hackrf_cmd == START_HACKRF || hackrf_cmd == STOP_HACKRF || hackrf_cmd == START_HACKRF_INF) {
                            std::cout << "Команда распознана" << std::endl;

                        } else if (hackrf_cmd == SET_RECIPIENT_NUM) {
                            set_sms_recipient();  
                            send_sms("Recipient has been set. Current mode: "+current_action); 
                            hackrf_cmd = -1;
  
                        } else if (hackrf_cmd == GET_BAT_VOLTAGE) {
                            float voltage = get_battery_voltage();
                            std::string voltage_str = std::to_string(voltage);
                            send_sms("Battery voltage: "+voltage_str+"V"); 
                            hackrf_cmd = -1;
                        } else {
                            std::cout << "Команда не распознана" << std::endl;
                        }

                        setState(DELETING_SMS);
                        send_command("AT");
                        send_command("AT+CMGD="+get_sms_index(line)); 
                        
                    } else {
                        std::cout << "Содержимое сообщения не распознано, удаление" << std::endl;
                        setState(DELETING_SMS);
                        send_command("AT");
                        send_command("AT+CMGD="+get_sms_index(line));
                    }
                    
                } else if (line.find("OK") != std::string::npos) { // непрочитанных сообщений нет
                    //setState(TURN_OFF);
                    std::cout << "Сообщений нет, проверка памяти SIM" << std::endl;
                    send_command("AT+CPMS?");
                    setState(CHECKING_SIM_STORAGE);
                }
                break;
            }
            break;
            
        case CHECKING_SIM_STORAGE:
            if (rx_ok) { 
                rx_ok = false;
                if (line.find("+CPMS:") != std::string::npos) {
                    if(is_sim_storage_full(line)) {
                        std::cout << "Память для сообщений переполнена, очистка..." << std::endl;
                        send_command("AT+CMGD=1,4");
                        setState(CLEARING_SIM_STORAGE);
                    } else {
                        std::cout << "Память для сообщений не переполнена, go to idle..." << std::endl;
	                    setState(IDLE);
                    }
                }
            }
        
        case CLEARING_SIM_STORAGE:
            
            if (rx_ok && (line.find("OK") != std::string::npos)) {
                wait_ans = false;
                rx_ok = false;
                std::cout << "Память SIM очищена, go to idle..." << std::endl;
		        setState(IDLE); 
            }
			 
        
        case DELETING_SMS:
            
            if (rx_ok && (line.find("OK") != std::string::npos)) {
                wait_ans = false;
                rx_ok = false;
                if (hackrf_cmd != -1) {
                    std::cout << "Сообщение удалено, взаимодействие с hackRF" << std::endl;
                    setState(HACK_RF_INTERACTION);
                } else {
                    std::cout << "Сообщение удалено" << std::endl;
                    setState(IDLE);
                }
                
            } else if (rx_ok && !line.find("OK")) {
																							
                std::cout << "Нет ответа на удаление SMS" << std::endl;
                setState(IDLE);
            }
            break;

        case HACK_RF_INTERACTION:
            
            if (hackrf_cmd == START_HACKRF) {

	            if (hackrf_cmd_prev == START_HACKRF_INF) {
		            stop_hackrf_transfer();
                    deinit_timer();
                    std::this_thread::sleep_for(std::chrono::seconds(3));
		        }
                
                std::cout << "Режим с прерыванием по времени" << std::endl;
                start_hackrf_transfer(0);  
                setState(IDLE);

            } else if (hackrf_cmd == STOP_HACKRF) {
                stop_hackrf_transfer();
                deinit_timer();
                hackrf_cmd_prev = -1;
                setState(IDLE);
                
            } else if (hackrf_cmd == START_HACKRF_INF) {
                if (hackrf_cmd_prev == START_HACKRF) {
                    stop_hackrf_transfer();
		            deinit_timer();
                    std::this_thread::sleep_for(std::chrono::seconds(3));
		        }
                std::cout << "Непрерывный режим" << std::endl;
                start_hackrf_transfer(1);
                setState(IDLE);
            }
            break;

        case IDLE:
            
            if (rx_ok) {
                rx_ok = false;
                std::cout << "Получены данные от SIM800C в процессе ожидания " << line << std::endl;
                if(line.find("+CMTI:") != std::string::npos) {
                    if(parse_notification(line)) {
                        std::cout << "Уведомление разобрано, получение СМС..." << std::endl;
                        //hackrf_cmd = STOP_HACKRF;
                        send_command("AT");
    			        std::this_thread::sleep_for(std::chrono::milliseconds(200));

                        send_command("AT+CMGR="+get_sms_index_from_notif());
                    } else {
                        std::cout << "Уведомление не разобрано" << std::endl;
                    }
                  //hackrf_cmd = STOP_HACKRF;
                  //send_command("AT+CMGD="+get_sms_index(line));
                  //setState(HACK_RF_INTERACTION);
                }
                
                if(line.find("+CMGR:") != std::string::npos) {
                    
                  if(parse_sms(line)) {
                    
                        std::cout << "Сообщение разобрано" << std::endl;
                        int hackrf_cmd_next = get_sms_text(line);
                        
                        if (hackrf_cmd_next == hackrf_cmd && !force_start) {
                            std::cout << "Заданная команда уже выполняется!" << std::endl;
		                    force_start = 0;

                        } else {
                            if(hackrf_cmd_next == START_HACKRF || hackrf_cmd_next == STOP_HACKRF || hackrf_cmd_next == START_HACKRF_INF) {
                                if (force_start) {
				                    force_start = false;
				                }
				                
                                hackrf_cmd_prev = hackrf_cmd;
				                hackrf_cmd = hackrf_cmd_next;
                                std::cout << "Команда распознана" << std::endl;

                            } else if (hackrf_cmd_next == SET_RECIPIENT_NUM) {
                                set_sms_recipient();  
                                send_sms("Recipient has been set. Current mode: "+current_action); 
                                hackrf_cmd = -1;

                            } else if (hackrf_cmd_next == GET_BAT_VOLTAGE){
                                float voltage = get_battery_voltage();
								float load_current = get_load_current();
                                std::string voltage_str = std::to_string(voltage);
								std::string load_current_str = std::to_string(load_current);
                                send_sms("Battery voltage: "+voltage_str+"V Load current: "+load_current_str+"mA"); 
                                hackrf_cmd = -1;

                            } else {
                                std::cout << "Команда не распознана" << std::endl;
                            } 
                        }
    
                    } else {
                        std::cout << "Содержимое сообщения не распознано, удаление" << std::endl;
                    }
                        setState(DELETING_SMS);
                        send_command("AT");
                        send_command("AT+CMGD="+get_sms_index(line));
                }
                
            }
/*
            if (!is_hackrf_transfer_running()) {
                std::cout << "Передача по hackRF завершена" << std::endl;
                setState(IDLE);
            } */
            
            if (is_timer_ovflw()) {
                stop_hackrf_transfer();
	            deinit_timer();
                hackrf_cmd_prev = -1;
                hackrf_cmd = -1;
            }

            check_hackrf_transfer();
            //check_battery_voltage();
            break;

        case TURN_OFF:
            send_command("AT");
	        send_command("AT+CPMS?");
            setState(CHECKING_SIM_STORAGE);
 	    next_state = POWER_OFF;
            //running = false;
            break;
        case POWER_OFF:
            running = false;
            break;
        default:
            break;
        }

    }

    // Запускаем поток для чтения
    //std::thread reader_thread(reader_thread_func);

    //reader_thread.join();
    close_port();
 //   std::this_thread::sleep_for(std::chrono::seconds(15));
    delete_file();
    deinit_timer();
//    powerOff();
    std::cout << " Программа завершена" << std::endl;
    return 0;
}

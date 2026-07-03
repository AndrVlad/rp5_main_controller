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

int serial_fd = -1;
int current_state = 1;
int hackrf_cmd = -1;
std::atomic<bool> running(true);
std::atomic<bool> sms_received(false);
std::string sms_text;
std::string sms_sender;
bool rx_ok = 0;
bool wait_ans = 0;

enum State {WAKE_UP = 1, POLLING_SIM, CLEARING_SIM_STORAGE, HACK_RF_INTERACTION, IDLE,TURN_OFF};
enum hackRFCMD {START_HACKRF = 1, STOP_HACKRF};

void AT_parser(const std::string& line) {
    return;
}

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
    std::cout << "[Отправлено]: " << cmd << std::endl;
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

void parse_sms_content(const std::string& line) {
    // Проверяем уведомление о новом SMS: +CMTI: "SM",1
    if (line.find("+CMTI:") != std::string::npos) {
        // Извлекаем индекс сообщения
        size_t pos = line.rfind(',');
        if (pos != std::string::npos) {
            std::string index_str = line.substr(pos + 1);
            // Убираем пробелы
            index_str.erase(0, index_str.find_first_not_of(" \t"));
            index_str.erase(index_str.find_last_not_of(" \t") + 1);
            
            std::cout << "\nSMS Индекс: " << index_str << std::endl;
            
            // Читаем текст сообщения
            std::string read_cmd = "AT+CMGR=" + index_str;
            send_command(read_cmd);
            
            // Даем время на получение ответа
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            
            // Читаем ответ (заголовок + текст)
            std::string header = read_line();
            std::string text = read_line();
            
            // Пропускаем OK
            std::string ok = read_line();
            //std::cout << ok << std::endl;
		//std::cout << text << std::endl;
//std::cout << header << std::endl;
//std::string delete_cmd = "AT+CMGD=" + index_str;
               // send_command(delete_cmd);
               // std::this_thread::sleep_for(std::chrono::milliseconds(200));
           // if (!text.empty()) {
                sms_text = text;
                sms_received = true;
                
                std::cout << " text " << text << " header  " << header << " ok " << ok << std::endl;
               
                
                // Удаляем сообщение после прочтения
                std::string delete_cmd = "AT+CMGD=" + index_str;
                send_command(delete_cmd);
                //std::this_thread::sleep_for(std::chrono::milliseconds(200));
            //}
        }
    }
}

// ==================== ПОТОК ЧТЕНИЯ ====================

void reader_thread_func() {
    std::cout << " Поток чтения запущен\n" << std::endl;
    
    send_command("AT+CPMS=\"SM\",\"SM\",\"SM\"");  
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    while (running && serial_fd != -1) {
        std::string line = read_line();
        if (!line.empty()) {
            std::cout << "[Получено]: " << line << std::endl;
            
            AT_parser(line);

            parse_sms_content(line);
            
            // Если SMS получено, выполняем команду
            if (sms_received) {
                sms_received = false;  // Сбрасываем флаг
                
                // Выполняем bash-команду
                std::string bash_cmd = "hackrf_transfer -t dji_2467mhz_clean.iq -f 2467000000 -x 47 -a 1 -p 1";
                std::cout << "\n Выполнение команды: " << bash_cmd << std::endl;
                int result = system(bash_cmd.c_str());
                
                if (result == 0) {
                    std::cout << " Команда выполнена успешно" << std::endl;
                } else {
                    std::cout << " Ошибка выполнения команды (код: " << result << ")" << std::endl;
                }
                
                std::cout << "\n Ожидание следующего SMS...\n" << std::endl;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// ==================== ОБРАБОТЧИК ОСТАНОВКИ ====================

void signal_handler(int sig) {
    if (sig == SIGINT) {
        std::cout << "\n\n  Остановка программы..." << std::endl;
        running = false;
    }
}

void parse_sms(const std::string& line) {
    std::cout << "Разбор СМС " << line << std::endl;
    return;
}

bool is_sms_correct(const std::string& line) {
  parse_sms(line);
  return true;
}



void setState(int next_state) {
    current_state = next_state;
    std::cout << "Изменено состояние на " << current_state << std::endl;
}

std::string get_sms_index(const std::string& line) {
   
    return "1"; 
}

int get_sms_content(const std::string& line) {
    return 1;
}

void startHackrfTransfer() {
    return;
}

void stopHackrfTransfer() {
    return;
}

bool isHackrfProcessStopped() {
    return true;
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
    gpio_pin_ctrl(17,0,10);
    return;
}

// ==================== ОСНОВНАЯ ФУНКЦИЯ ====================

int main() {
    signal(SIGINT, signal_handler);
    
    serial_fd = open_port("/dev/ttyAMA0", 115200);
    if (serial_fd == -1) {
        return 1;
    }
    
    running=true;
    
    gpio_pin_set(17, 1);

    while (running && serial_fd != -1) {
        std::string line = read_line();

        if (!line.empty()) {
            std::cout << "[Получено]: " << line << std::endl;
            rx_ok = true;
        }

        switch (current_state)
        {
        case WAKE_UP:
            
            if (!rx_ok) {
                send_command("AT");
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

                if (line.find("+CMGL") != std::string::npos) { // есть непрочитанное сообщение
                    hackrf_cmd = get_sms_content(line);
                    if(hackrf_cmd == START_HACKRF || hackrf_cmd == STOP_HACKRF) {
                        setState(CLEARING_SIM_STORAGE);
                        send_command("AT+CMGD="+get_sms_index(line));
                        //wait_ans = true;
                    } else {
                        std::cout << "Содержимое сообщения не распознано, удаление" << std::endl;
                        setState(CLEARING_SIM_STORAGE);
                        send_command("AT+CMGD="+get_sms_index(line));
                    }
                } else if (line.find("OK") != std::string::npos) { // непрочитанных сообщений нет
                    setState(TURN_OFF);
                    //send_command("AT+CMGD="+get_sms_index(line));
                    //wait_ans = true;
                    std::cout << "Сообщений нет, отключение устройства" << std::endl;
                }
                break;
            }
            break;
        case CLEARING_SIM_STORAGE:
            
            if (rx_ok && (line.find("OK") != std::string::npos)) {
                wait_ans = false;
                rx_ok = false;
                if (hackrf_cmd != -1) {
                    std::cout << "Сообщение удалено, взаимодействие с hackRF" << std::endl;
                    setState(HACK_RF_INTERACTION);
                } else {
                    std::cout << "Сообщение удалено, выключение" << std::endl;
                    setState(TURN_OFF);
                }
                
            } else {
                std::cout << "Нет ответа на удаление SMS" << std::endl;
                // доработать логику
            }
            break;

        case HACK_RF_INTERACTION:
            
            if (hackrf_cmd == START_HACKRF) {
                std::cout << "Начата передача файла по hackrf" << std::endl;
                startHackrfTransfer();

            } else if(hackrf_cmd == STOP_HACKRF) {
                std::cout << "Остановлена передача файла по hackrf" << std::endl;
                stopHackrfTransfer();
            }

            setState(IDLE);

            break;
        case IDLE:
            
            if (rx_ok) {
                rx_ok = false;
                std::cout << "Получены данные от SIM800C в процессе ожидания " << line << std::endl;
            }

            if (isHackrfProcessStopped()) {
                std::cout << "Передача по hackRF завершена" << std::endl;
                setState(TURN_OFF);
            }

            break;

        case TURN_OFF:
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
    powerOff();
    std::cout << " Программа завершена" << std::endl;
    return 0;
}

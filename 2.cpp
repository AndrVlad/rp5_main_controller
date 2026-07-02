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

int serial_fd = -1;
int current_state = 1;
std::atomic<bool> running(true);
std::atomic<bool> sms_received(false);
std::string sms_text;
std::string sms_sender;
bool rx_ok = 0;
bool wait_ans = 0;

enum State {WAKE_UP = 1, POLLING_SIM, HACK_RF_INTERACTION};

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

bool is_sms_correct(const std::string& line) {
  parse_sms(line);
  return true;
}

void parse_sms(const std::string& line) {
    std::cout << "Разбор СМС " << line << std::endl;
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
                if (line.find("OK")) {
                    current_state = POLLING_SIM;
                    wait_ans = true;
                    send_command("AT+CMGL=\"REC UNREAD\"");
                    std::cout << "change state on " << current_state;
                }
            }
            break;

        case POLLING_SIM:
            
            if(!rx_ok && wait_ans) {
                break;
            } else if (rx_ok && wait_ans) {
                rx_ok = false;
  
                if (line.find("+CMGL")) {
                    if(is_sms_correct(line)) {
                        current_state = CLEARING_SIM_STORAGE;
                        
                    } else {
                        turn_off();        
                    }
                } else if (line.find("OK")) {
                    
                }
                
                
                running = false;
                break;
            }

            break;
        case CLEARING_SIM_STORAGE:
            
            break;
        case HACK_RF_INTERACTION:
            
            break;
        default:
            break;
        }

    }

    // Запускаем поток для чтения
    //std::thread reader_thread(reader_thread_func);

    //reader_thread.join();
    
    close_port();
    std::cout << " Программа завершена" << std::endl;
    return 0;
}

#include <iostream>
#include <unistd.h>
#include "src/ina219.h"

int main(int argc, char *argv[])
{
    float SHUNT_OHMS = 0.1;
    float MAX_EXPECTED_AMPS = 1.6;
    
    INA219 i(SHUNT_OHMS, MAX_EXPECTED_AMPS);
    i.configure(RANGE_16V, GAIN_4_160MV, ADC_12BIT, ADC_12BIT);

    std::cout << "time_s,bus_voltage_V,supply_voltage_V,shunt_voltage_mV,current_mA,power_mW" << std::endl;

    int c = 0;
    while(c < 5)
    {
        std::cout << c << ","
                    << i.voltage() << ","
                    << i.supply_voltage() << ","
                    << i.shunt_voltage() << ","
                    << i.current() << ","
                    << i.power() << std::endl;
            c++;
            usleep(1000000); // 1s
        }
    }
    return 0;
}
/*
 * tasks.c
 *
 *  Created on: Mar 8, 2026
 *      Author: Rubin Khadka
 */

#include "stm32f103xb.h"
#include "dht11.h"
#include "dwt.h"

#define MAX_RETRIES 5

// Global variables to store DHT11 data
volatile float dht11_humidity = 0.0f;
volatile float dht11_temperature = 0.0f;

void Task_DHT11_Read(void)
{
  uint8_t hum1, hum2, temp1, temp2, checksum;

  // Disable interrupts for critical section
  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  // Try up to MAX_RETRIES times
  for(int retry = 0; retry < MAX_RETRIES; retry++)
  {
    DHT11_Start();

    if(DHT11_Check_Response())
    {
      hum1 = DHT11_Read();
      hum2 = DHT11_Read();
      temp1 = DHT11_Read();
      temp2 = DHT11_Read();
      checksum = DHT11_Read();

      uint8_t calc = hum1 + hum2 + temp1 + temp2;

      if(calc == checksum)
      {
        // Humidity: combine integer and decimal parts
        dht11_humidity = (float) hum1 + (float) hum2 / 10.0f;

        // Temperature: check sign bit (0x80) for negative values
        if(temp1 & 0x80)
          dht11_temperature = -((float) (temp1 & 0x7F) + (float) temp2 / 10.0f);
        else
          dht11_temperature = (float) temp1 + (float) temp2 / 10.0f;

        break;
      }
    }
    DWT_Delay_ms(1);
  }

  // Re-enable interrupts
  __set_PRIMASK(primask);
}

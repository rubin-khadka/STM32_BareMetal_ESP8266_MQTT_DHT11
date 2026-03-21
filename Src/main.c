/*
 * main.c
 *
 *  Created on: Mar 19, 2026
 *      Author: Rubin Khadka
 */

#include "stm32f103xb.h"
#include "esp8266.h"
#include "dwt.h"
#include "timer2.h"
#include "timer3.h"
#include "usart1.h"
#include "usart2.h"
#include "tasks.h"
#include "dht11.h"
#include "i2c1.h"
#include "lcd.h"

#define DHI_READ_TICK       100
#define LCD_UPDATE_TICK     10
#define MQTT_PUBLISH_TICK   1000

int main(void)
{
  // Initialize all the peripherals
  DWT_Init();
  TIMER2_Init();
  USART1_Init();
  USART2_Init();
  I2C1_Init();
  LCD_Init();
  DHT11_Init();

  // Loop counters
  uint16_t dht_count = 0;
  uint16_t lcd_count = 0;
  uint16_t mqtt_count = 0;

  // Debug startup messages using UART2
  USART2_SendString("============================\n");
  USART2_SendString("STM32 Project Initialization\n");
  USART2_SendString("============================\n");

  char ip_buf[16];

  // Initialize ESP8266
  if(ESP_Init() != ESP8266_OK)
  {
    USART2_SendString("Failed to initialize... Check Debug logs\n");
  }
  USART2_SendString("ESP8266 Initialized\n");

  // Connect to WiFi
  if(ESP_ConnectWiFi("xxxx", "xxxxx!", ip_buf, sizeof(ip_buf)) != ESP8266_OK)
  {
    USART2_SendString("Failed to connect to wifi...\n");
  }

  LCD_Clear();
  LCD_SendString("STM32 PROJECT");
  LCD_SetCursor(1, 0);
  LCD_SendString("INITIALIZING...");

  // Connect to MQTT
  MQTT_Init();

  // Setup TIM3 for 10ms control loop
  TIMER3_SetupPeriod(10);  // 10ms period

  while(1)
  {
    // Run tasks at different rates

    // DHT11 every 1 seconds
    if(dht_count++ >= DHI_READ_TICK)
    {
      Task_DHT11_Read();
      dht_count = 0;
    }

    // LCD update every 100ms
    if(lcd_count++ >= LCD_UPDATE_TICK)
    {
      Task_LCD_Update();
      lcd_count = 0;
    }

    if(mqtt_count++ >= MQTT_PUBLISH_TICK)
    {
      Task_MQTT_Publish();
      mqtt_count = 0;
    }

    TIMER3_WaitPeriod();
  }
}

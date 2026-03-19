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

  LCD_Clear();
  LCD_SendString("STM32 PROJECT");
  LCD_SetCursor(1, 0);
  LCD_SendString("INITIALIZING...");

  TIMER2_Delay_ms(2000);

  // Debug startup messages using UART2
  USART2_SendString("==========================================\n");
  USART2_SendString("Connecting to WIFI and Getting IP Address\n");
  USART2_SendString("==========================================\n");

  char ip_buf[16];

  // Initialize ESP8266
  if(ESP_Init() != ESP8266_OK)
  {
    USART2_SendString("Failed to initialize... Check Debug logs\n");
  }
  USART2_SendString("ESP8266 initialized OK\n");

  // Connect to WiFi
  if(ESP_ConnectWiFi("mynoobu", "Sarah159!", ip_buf, sizeof(ip_buf)) != ESP8266_OK)
  {
    USART2_SendString("Failed to connect to wifi...\n");
  }

  // Setup TIM3 for 10ms control loop
  TIMER3_SetupPeriod(10);  // 10ms period

  while(1)
  {

  }
}

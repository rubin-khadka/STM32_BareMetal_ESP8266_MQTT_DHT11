/*
 * esp8266.c - Lightweight version without stdio/stdlib
 *
 *  Created on: Mar 19, 2026
 *      Author: Rubin Khadka
 */

#include "esp8266.h"
#include "usart1.h"
#include "usart2.h"
#include "timer2.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

// Get string length
static uint16_t ESP_StrLen(const char *str)
{
  uint16_t len = 0;
  while(str[len] != '\0')
    len++;
  return len;
}

// Compare two strings (returns 0 if equal)
static uint8_t ESP_StrCmp(const char *s1, const char *s2)
{
  while(*s1 && *s2 && *s1 == *s2)
  {
    s1++;
    s2++;
  }
  return *s1 - *s2;
}

// Find substring in string
static char* ESP_StrStr(const char *haystack, const char *needle)
{
  uint16_t needle_len = 0;
  uint16_t haystack_len = 0;
  uint16_t i, j;

  // Get needle length
  while(needle[needle_len] != '\0')
    needle_len++;
  if(needle_len == 0)
    return (char*) haystack;

  // Get haystack length
  while(haystack[haystack_len] != '\0')
    haystack_len++;
  if(haystack_len < needle_len)
    return NULL;

  // Search
  for(i = 0; i <= haystack_len - needle_len; i++)
  {
    for(j = 0; j < needle_len; j++)
    {
      if(haystack[i + j] != needle[j])
        break;
    }
    if(j == needle_len)
      return (char*) &haystack[i];
  }
  return NULL;
}

// Copy string with length limit
static uint16_t ESP_StrCopy(char *dest, const char *src, uint16_t n)
{
    uint16_t i;
    for(i = 0; i < n - 1 && src[i] != '\0'; i++)
    {
        dest[i] = src[i];
    }
    dest[i] = '\0';
    return i;
}

// Convert integer to string
static uint8_t ESP_ItoA(int num, char *buffer, uint8_t buffer_len)
{
  uint8_t i = 0;
  uint8_t j = 0;
  char temp[12];
  uint8_t is_negative = 0;

  if(num == 0)
  {
    if(buffer_len > 1)
    {
      buffer[0] = '0';
      buffer[1] = '\0';
      return 1;
    }
    return 0;
  }

  if(num < 0)
  {
    is_negative = 1;
    num = -num;
  }

  // Convert digits
  while(num > 0 && i < sizeof(temp) - 1)
  {
    temp[i++] = '0' + (num % 10);
    num /= 10;
  }

  // Add sign
  if(is_negative && j < buffer_len - 1)
  {
    buffer[j++] = '-';
  }

  // Reverse digits
  while(i > 0 && j < buffer_len - 1)
  {
    buffer[j++] = temp[--i];
  }

  buffer[j] = '\0';
  return j;
}

// Simple formatter for AT commands
static void ESP_FormatCommand(
    char *buffer,
    uint16_t buf_len,
    const char *format,
    const char *param1,
    const char *param2)
{
  uint16_t i = 0;
  uint16_t f = 0;
  uint8_t param_count = 0;

  while(format[f] != '\0' && i < buf_len - 1)
  {
    if(format[f] == '%' && format[f + 1] == 's')
    {
      param_count++;
      const char *param = NULL;

      // Select correct parameter based on count
      if(param_count == 1)
        param = (param1 != NULL) ? param1 : "";
      else if(param_count == 2)
        param = (param2 != NULL) ? param2 : "";

      // Insert the parameter
      if(param)
      {
        while(*param != '\0' && i < buf_len - 1)
        {
          buffer[i++] = *param++;
        }
      }

      f += 2; // Skip %s
    }
    else
    {
      buffer[i++] = format[f++];
    }
  }

  buffer[i] = '\0';
}

// Format command with number parameter
static void ESP_FormatCmdWithNum(char *buffer, uint16_t buf_len, const char *format, int num)
{
  uint16_t i = 0;
  uint16_t f = 0;
  char num_str[12];

  // Convert number to string
  ESP_ItoA(num, num_str, sizeof(num_str));

  while(format[f] != '\0' && i < buf_len - 1)
  {
    if(format[f] == '%' && format[f + 1] == 'd')
    {
      // Replace %d with number
      uint16_t n = 0;
      while(num_str[n] != '\0' && i < buf_len - 1)
      {
        buffer[i++] = num_str[n++];
      }
      f += 2; // Skip %d
    }
    else
    {
      buffer[i++] = format[f++];
    }
  }

  buffer[i] = '\0';
}

// Build TCP connect command: AT+CIPSTART="TCP","broker",port\r\n
static void Build_TCP_Connect(char *buffer, uint16_t buf_len, const char *broker, uint16_t port)
{
  uint16_t i = 0;
  char port_str[6];
  uint8_t p = 0;

  // Convert port number to string
  if(port == 0)
  {
    port_str[p++] = '0';
  }
  else
  {
    // Handle different port sizes
    if(port >= 10000)
      port_str[p++] = '0' + (port / 10000) % 10;
    if(port >= 1000)
      port_str[p++] = '0' + (port / 1000) % 10;
    if(port >= 100)
      port_str[p++] = '0' + (port / 100) % 10;
    if(port >= 10)
      port_str[p++] = '0' + (port / 10) % 10;
    port_str[p++] = '0' + (port % 10);
  }
  port_str[p] = '\0';

  // Build: AT+CIPSTART="TCP","broker",port\r\n
  i += ESP_StrCopy(buffer + i, "AT+CIPSTART=\"TCP\",\"", buf_len - i);
  i += ESP_StrCopy(buffer + i, broker, buf_len - i);
  i += ESP_StrCopy(buffer + i, "\",", buf_len - i);
  i += ESP_StrCopy(buffer + i, port_str, buf_len - i);
  i += ESP_StrCopy(buffer + i, "\r\n", buf_len - i);
}

// ESP8266 Driver Implementation
ESP8266_ConnectionState ESP_ConnState = ESP8266_DISCONNECTED;
static char esp_rx_buffer[512];

// Forward declarations
static ESP8266_Status ESP_SendCommand(const char *cmd, const char *ack, uint32_t timeout);
static ESP8266_Status ESP_SendBinary(uint8_t *bin, uint16_t len, const char *ack, uint32_t timeout);
static ESP8266_Status ESP_GetIP(char *ip_buffer, uint16_t buffer_len);
static uint16_t MQTT_BuildConnect(
    uint8_t *packet,
    const char *clientID,
    const char *username,
    const char *password,
    uint16_t keepalive);

// Public Functions
ESP8266_Status ESP_Init(void)
{
  ESP8266_Status res;

  USART2_SendString("ESP_Init: Starting...\r\n");
  TIMER2_Delay_ms(1000);

  // Test AT
  res = ESP_SendCommand("AT\r\n", "OK", 2000);
  if(res != ESP8266_OK)
  {
    USART2_SendString("ESP_Init: AT failed\r\n");
    return res;
  }

  // Disable echo
  res = ESP_SendCommand("ATE0\r\n", "OK", 2000);
  if(res != ESP8266_OK)
  {
    USART2_SendString("ESP_Init: ATE0 failed\r\n");
    return res;
  }

  USART2_SendString("ESP_Init: Success\r\n");
  return ESP8266_OK;
}

ESP8266_Status ESP_ConnectWiFi(const char *ssid, const char *password, char *ip_buffer, uint16_t buffer_len)
{
  char cmd[128];
  ESP8266_Status result;

  USART2_SendString("ESP_ConnectWiFi: Setting station mode\r\n");

  // Set station mode
  ESP_FormatCommand(cmd, sizeof(cmd), "AT+CWMODE=1\r\n", NULL, NULL);
  result = ESP_SendCommand(cmd, "OK", 2000);
  if(result != ESP8266_OK)
  {
    USART2_SendString("ESP_ConnectWiFi: Failed to set mode\r\n");
    return result;
  }

  USART2_SendString("ESP_ConnectWiFi: Connecting to ");
  USART2_SendString(ssid);
  USART2_SendString("\r\n");

  // Connect to WiFi
  ESP_FormatCommand(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);
  result = ESP_SendCommand(cmd, "WIFI CONNECTED", 15000);
  if(result != ESP8266_OK)
  {
    USART2_SendString("ESP_ConnectWiFi: Connection failed\r\n");
    ESP_ConnState = ESP8266_DISCONNECTED;
    return result;
  }

  USART2_SendString("ESP_ConnectWiFi: Connected to WiFi\r\n");
  ESP_ConnState = ESP8266_CONNECTED_NO_IP;

  // Get IP address
  result = ESP_GetIP(ip_buffer, buffer_len);
  if(result != ESP8266_OK)
  {
    USART2_SendString("ESP_ConnectWiFi: Failed to get IP\r\n");
    return result;
  }

  USART2_SendString("ESP_ConnectWiFi: IP: ");
  USART2_SendString(ip_buffer);
  USART2_SendString("\r\n");

  return ESP8266_OK;
}

ESP8266_ConnectionState ESP_GetConnectionState(void)
{
  return ESP_ConnState;
}

// MQTT Functions
ESP8266_Status ESP_MQTT_Connect(
    const char *broker,
    uint16_t port,
    const char *clientID,
    const char *username,
    const char *password,
    uint16_t keepalive)
{
  char cmd[64];
  uint8_t packet[128];
  uint16_t len = 0;
  ESP8266_Status res;

  USART2_SendString("MQTT_Connect: Connecting to broker\r\n");

  // Build TCP connect command correctly
  Build_TCP_Connect(cmd, sizeof(cmd), broker, port);

  // Debug: show what we're sending
  USART2_SendString("Sending: ");
  USART2_SendString(cmd);

  // Step 1: TCP connect to broker
  res = ESP_SendCommand(cmd, "CONNECT", 5000);
  if(res != ESP8266_OK)
  {
    USART2_SendString("MQTT_Connect: TCP connect failed\r\n");
    return res;
  }

  USART2_SendString("MQTT_Connect: TCP connected!\r\n");

  // Step 2: Build MQTT CONNECT packet
  len = MQTT_BuildConnect(packet, clientID, username, password, keepalive);

  USART2_SendString("MQTT_Connect: Built CONNECT packet, size: ");
  USART2_SendNumber(len);
  USART2_SendString("\r\n");

  // Step 3: Send packet length to ESP8266
  ESP_FormatCmdWithNum(cmd, sizeof(cmd), "AT+CIPSEND=%d\r\n", len);
  res = ESP_SendCommand(cmd, ">", 2000);
  if(res != ESP8266_OK)
  {
    USART2_SendString("MQTT_Connect: CIPSEND failed\r\n");
    return res;
  }

  // Step 4: Send the MQTT CONNECT packet and wait for CONNACK
  res = ESP_SendBinary(packet, len, "\x20\x02\x00\x00", 5000);
  if(res != ESP8266_OK)
  {
    USART2_SendString("MQTT_Connect: CONNACK failed\r\n");
    return res;
  }

  USART2_SendString("MQTT_Connect: Success!\r\n");
  return ESP8266_OK;
}

ESP8266_Status ESP_MQTT_Publish(const char *topic, const char *message, uint8_t qos)
{
  char cmd[32];
  uint8_t packet[256];
  uint16_t len = 0;
  ESP8266_Status res;
  uint16_t topic_len, msg_len;
  uint16_t i;

  USART2_SendString("MQTT_Publish: Publishing...\r\n");

  // Build MQTT PUBLISH packet
  packet[len++] = 0x30 | (qos << 1);  // PUBLISH with QoS

  // Remaining length placeholder (will fill later)
  uint16_t rem_len_pos = len;
  len++;

  // Topic length and topic
  topic_len = ESP_StrLen(topic);
  packet[len++] = topic_len >> 8;
  packet[len++] = topic_len & 0xFF;

  for(i = 0; i < topic_len; i++)
  {
    packet[len++] = topic[i];
  }

  // Message
  msg_len = ESP_StrLen(message);
  for(i = 0; i < msg_len; i++)
  {
    packet[len++] = message[i];
  }

  // Set remaining length
  packet[rem_len_pos] = len - 2;  // Exclude fixed header first 2 bytes

  // Send command
  ESP_FormatCmdWithNum(cmd, sizeof(cmd), "AT+CIPSEND=%d\r\n", len);
  res = ESP_SendCommand(cmd, ">", 2000);
  if(res != ESP8266_OK)
  {
    USART2_SendString("MQTT_Publish: CIPSEND failed\r\n");
    return res;
  }

  // Send packet
  res = ESP_SendBinary(packet, len, "SEND OK", 5000);
  if(res != ESP8266_OK)
  {
    USART2_SendString("MQTT_Publish: Send failed\r\n");
    return res;
  }

  USART2_SendString("MQTT_Publish: Success\r\n");
  return ESP8266_OK;
}

ESP8266_Status ESP_MQTT_Subscribe(const char *topic, uint8_t qos)
{
  char cmd[32];
  uint8_t packet[128];
  uint16_t len = 0;
  ESP8266_Status res;
  uint16_t topic_len, i;

  USART2_SendString("MQTT_Subscribe: Subscribing...\r\n");

  // Build SUBSCRIBE packet
  packet[len++] = 0x82;  // SUBSCRIBE

  // Remaining length placeholder
  uint16_t rem_len_pos = len;
  len++;

  // Packet identifier (0x0001)
  packet[len++] = 0x00;
  packet[len++] = 0x01;

  // Topic
  topic_len = ESP_StrLen(topic);
  packet[len++] = topic_len >> 8;
  packet[len++] = topic_len & 0xFF;

  for(i = 0; i < topic_len; i++)
  {
    packet[len++] = topic[i];
  }

  packet[len++] = qos;  // Requested QoS

  // Set remaining length
  packet[rem_len_pos] = len - 2;

  // Send command
  ESP_FormatCmdWithNum(cmd, sizeof(cmd), "AT+CIPSEND=%d\r\n", len);
  res = ESP_SendCommand(cmd, ">", 2000);
  if(res != ESP8266_OK)
  {
    USART2_SendString("MQTT_Subscribe: CIPSEND failed\r\n");
    return res;
  }

  // Send packet and wait for SUBACK
  res = ESP_SendBinary(packet, len, "\x90", 5000);  // SUBACK = 0x90
  if(res != ESP8266_OK)
  {
    USART2_SendString("MQTT_Subscribe: SUBACK failed\r\n");
    return res;
  }

  USART2_SendString("MQTT_Subscribe: Success\r\n");
  return ESP8266_OK;
}

ESP8266_Status ESP_MQTT_Ping(void)
{
  char cmd[32];
  ESP8266_Status res;
  uint8_t packet[2];

  USART2_SendString("MQTT_Ping: Sending PINGREQ\r\n");

  packet[0] = 0xC0;  // PINGREQ
  packet[1] = 0x00;

  // Send command
  ESP_FormatCommand(cmd, sizeof(cmd), "AT+CIPSEND=2\r\n", NULL, NULL);
  res = ESP_SendCommand(cmd, ">", 2000);
  if(res != ESP8266_OK)
  {
    USART2_SendString("MQTT_Ping: CIPSEND failed\r\n");
    return res;
  }

  // Send packet and wait for PINGRESP
  res = ESP_SendBinary(packet, 2, "\xD0", 2000);  // PINGRESP = 0xD0
  if(res != ESP8266_OK)
  {
    USART2_SendString("MQTT_Ping: PINGRESP failed\r\n");
    return res;
  }

  USART2_SendString("MQTT_Ping: Success\r\n");
  return ESP8266_OK;
}

ESP8266_Status ESP_MQTT_HandleIncoming(
    char *topic_buffer,
    uint16_t topic_buf_len,
    char *msg_buffer,
    uint16_t msg_buf_len)
{
  // This function would parse +IPD messages from ESP8266
  // For now, return NOT_IMPLEMENTED
  return ESP8266_ERROR;
}

// Static Helper Functions
static ESP8266_Status ESP_GetIP(char *ip_buffer, uint16_t buffer_len)
{
  for(int attempt = 1; attempt <= 3; attempt++)
  {
    USART2_SendString("ESP_GetIP: Attempt ");
    USART2_SendNumber(attempt);
    USART2_SendString("\r\n");

    // Send AT+CIFSR
    ESP8266_Status result = ESP_SendCommand("AT+CIFSR\r\n", "OK", 5000);
    if(result != ESP8266_OK)
    {
      USART2_SendString("ESP_GetIP: CIFSR failed\r\n");
      continue;
    }

    // Parse response for STAIP
    char *search = esp_rx_buffer;
    char *ip_start = NULL;

    while((search = ESP_StrStr(search, "STAIP,\"")) != NULL)
    {
      ip_start = search + 7;  // Skip "STAIP,""
      char *end = ip_start;
      while(*end != '\0' && *end != '"')
        end++;

      if(*end == '"' && (end - ip_start) < buffer_len)
      {
        ESP_StrCopy(ip_buffer, ip_start, (end - ip_start) + 1);

        if(ESP_StrCmp(ip_buffer, "0.0.0.0") == 0)
        {
          USART2_SendString("ESP_GetIP: IP not ready\r\n");
          ESP_ConnState = ESP8266_CONNECTED_NO_IP;
          TIMER2_Delay_ms(1000);
          break;
        }

        USART2_SendString("ESP_GetIP: Found IP\r\n");
        ESP_ConnState = ESP8266_CONNECTED_IP;
        return ESP8266_OK;
      }
      search += 6;
    }

    TIMER2_Delay_ms(500);
  }

  USART2_SendString("ESP_GetIP: Failed\r\n");
  ESP_ConnState = ESP8266_CONNECTED_NO_IP;
  return ESP8266_ERROR;
}

static ESP8266_Status ESP_SendCommand(const char *cmd, const char *ack, uint32_t timeout)
{
  uint8_t ch;
  uint16_t idx = 0;
  uint32_t tickstart;
  uint8_t found = 0;

  // Clear buffer
  for(idx = 0; idx < sizeof(esp_rx_buffer); idx++)
  {
    esp_rx_buffer[idx] = 0;
  }
  idx = 0;

  USART2_SendString(">> ");
  USART2_SendString(cmd);
  if(cmd[ESP_StrLen(cmd) - 1] != '\n')
  {
    USART2_SendString("\r\n");
  }

  tickstart = TIMER2_GetMillis();

  // Send command
  if(ESP_StrLen(cmd) > 0)
  {
    USART1_SendString(cmd);
  }

  // Wait for response
  while((TIMER2_GetMillis() - tickstart) < timeout && idx < sizeof(esp_rx_buffer) - 1)
  {
    if(USART1_DataAvailable())
    {
      ch = USART1_GetChar();
      esp_rx_buffer[idx++] = ch;
      esp_rx_buffer[idx] = '\0';

      // Check for ACK
      if(!found && ESP_StrStr(esp_rx_buffer, ack))
      {
        found = 1;
        USART2_SendString("<< Found ACK: ");
        USART2_SendString(ack);
        USART2_SendString("\r\n");
      }

      // Check for ERROR
      if(ESP_StrStr(esp_rx_buffer, "ERROR"))
      {
        USART2_SendString("<< Got ERROR\r\n");
        return ESP8266_ERROR;
      }
    }
  }

  if(found)
    return ESP8266_OK;

  if(idx == 0)
  {
    USART2_SendString("<< No response\r\n");
    return ESP8266_NO_RESPONSE;
  }

  USART2_SendString("<< Timeout\r\n");
  return ESP8266_TIMEOUT;
}

static ESP8266_Status ESP_SendBinary(uint8_t *bin, uint16_t len, const char *ack, uint32_t timeout)
{
  uint16_t idx = 0;
  uint32_t tickstart;
  uint8_t found = 0;
  uint16_t i;

  // Clear buffer
  for(idx = 0; idx < sizeof(esp_rx_buffer); idx++)
  {
    esp_rx_buffer[idx] = 0;
  }
  idx = 0;

  USART2_SendString(">> Sending binary (");
  USART2_SendNumber(len);
  USART2_SendString(" bytes)\r\n");

  tickstart = TIMER2_GetMillis();

  // Send binary data
  for(i = 0; i < len; i++)
  {
    USART1_SendChar(bin[i]);
  }

  // Wait for response
  while((TIMER2_GetMillis() - tickstart) < timeout && idx < sizeof(esp_rx_buffer) - 1)
  {
    if(USART1_DataAvailable())
    {
      esp_rx_buffer[idx++] = USART1_GetChar();
      esp_rx_buffer[idx] = '\0';

      // Check for ACK
      if(!found && ESP_StrStr(esp_rx_buffer, ack))
      {
        found = 1;
        USART2_SendString("<< Found ACK: ");
        USART2_SendString(ack);
        USART2_SendString("\r\n");
      }

      // Check for ERROR
      if(ESP_StrStr(esp_rx_buffer, "ERROR"))
      {
        USART2_SendString("<< Got ERROR\r\n");
        return ESP8266_ERROR;
      }
    }
  }

  if(found)
    return ESP8266_OK;

  USART2_SendString("<< Binary send timeout\r\n");
  return ESP8266_TIMEOUT;
}

static uint16_t MQTT_BuildConnect(
    uint8_t *packet,
    const char *clientID,
    const char *username,
    const char *password,
    uint16_t keepalive)
{
  uint16_t len = 0;
  uint16_t i;
  uint16_t cid_len, uname_len, pwd_len;

  // Fixed header
  packet[len++] = 0x10;  // CONNECT
  uint16_t rem_len_pos = len;
  len++;  // Placeholder for remaining length

  // Protocol name: MQTT (4 bytes)
  packet[len++] = 0x00;
  packet[len++] = 0x04;
  packet[len++] = 'M';
  packet[len++] = 'Q';
  packet[len++] = 'T';
  packet[len++] = 'T';

  // Protocol level
  packet[len++] = 0x04;  // MQTT 3.1.1

  // Connect flags
  uint8_t flags = 0x02;  // Clean session
  if(username != NULL && username[0] != '\0')
    flags |= 0x80;
  if(password != NULL && password[0] != '\0')
    flags |= 0x40;
  packet[len++] = flags;

  // Keep alive
  packet[len++] = (keepalive >> 8) & 0xFF;
  packet[len++] = keepalive & 0xFF;

  // Client ID
  cid_len = ESP_StrLen(clientID);
  packet[len++] = (cid_len >> 8) & 0xFF;
  packet[len++] = cid_len & 0xFF;
  for(i = 0; i < cid_len; i++)
  {
    packet[len++] = clientID[i];
  }

  // Username
  if(username != NULL && username[0] != '\0')
  {
    uname_len = ESP_StrLen(username);
    packet[len++] = (uname_len >> 8) & 0xFF;
    packet[len++] = uname_len & 0xFF;
    for(i = 0; i < uname_len; i++)
    {
      packet[len++] = username[i];
    }
  }

  // Password
  if(password != NULL && password[0] != '\0')
  {
    pwd_len = ESP_StrLen(password);
    packet[len++] = (pwd_len >> 8) & 0xFF;
    packet[len++] = pwd_len & 0xFF;
    for(i = 0; i < pwd_len; i++)
    {
      packet[len++] = password[i];
    }
  }

  // Set remaining length
  packet[rem_len_pos] = len - 2;  // Exclude fixed header first 2 bytes

  return len;
}

/**
 * unsigned char  uint8_t
 * unsigned short uint16_t
 * unsigned int   uint32_t
 */
#include "stm32f0xx.h"
#include "arm_math.h"
#include "stdlib.h"

#define CLOCK_SPEED 16000000
#define USART_BAUD_RATE 115200

#define USART1_TX_DMA_CHANNEL DMA1_Channel2
#define USART1_TDR_ADDRESS (unsigned int)(&(USART1->TDR))

#define USART_DATA_RECEIVED_FLAG 1
#define NETWORK_SEARCHING_STATUS_LED_FLAG 2
#define SUCCESSUFULLY_CONNECTED_TO_NETWORK_FLAG 4

#define GET_VISIBLE_NETWORK_LIST_FLAG 1
#define DISABLE_ECHO_FLAG 2
#define CONNECT_TO_NETWORK_FLAG 4
#define CONNECTION_STATUS_FLAG 8
#define OBTAIN_CONNECTION_STATUS_FLAG 16

#define USART_DATA_RECEIVED_BUFFER_SIZE 100

#define TIMER3_PERIOD_MS 0.13f
#define TIMER3_PERIOD_S (TIMER3_PERIOD_MS / 1000)
#define TIMER3_100MS (unsigned short)(100 / TIMER3_PERIOD_MS)

unsigned short piped_tasks_to_send[10];
unsigned int sent_flag;
unsigned int successfully_received_flags;
unsigned int general_flags;

char USART_OK[] __attribute__ ((section(".text.const"))) = "OK";
char DEFAULT_ACCESS_POINT_NAME[] __attribute__ ((section(".text.const"))) = "Asus";
char DEFAULT_ACCESS_POINT_PASSWORD[] __attribute__ ((section(".text.const"))) = "";
char ESP8226_REQUEST_DISABLE_ECHO[] __attribute__ ((section(".text.const"))) = "ATE0\r\n";
char ESP8226_RESPONSE_BUSY[] __attribute__ ((section(".text.const"))) = "busy";
char ESP8226_REQUEST_GET_VISIBLE_NETWORK_LIST[] __attribute__ ((section(".text.const"))) = "AT+CWLAP\r\n";
char ESP8226_REQUEST_GET_CONNECTION_STATUS[] __attribute__ ((section(".text.const"))) = "AT+CWJAP?\r\n";
char ESP8226_RESPONSE_NOT_CONNECTED_STATUS[] __attribute__ ((section(".text.const"))) = "No AP";
char ESP8226_REQUEST_CONNECT_TO_NETWORK_AND_SAVE[] __attribute__ ((section(".text.const"))) = "AT+CWJAP_DEF=\"{1}\",\"{2}\"\r\n";
char ESP8226_REQUEST_GET_VERSION_ID[] __attribute__ ((section(".text.const"))) = "AT+GMR\r\n";

char *usart_data_to_be_transmitted_buffer = NULL;
char usart_data_received_buffer[USART_DATA_RECEIVED_BUFFER_SIZE];
volatile unsigned char usart_received_bytes;

void (*send_usart_data_function)() = NULL;
volatile unsigned int send_usart_data_timer_counter;
volatile unsigned short send_usart_data_timout_s = 0xFFFF;
volatile unsigned short send_usart_data_errors_counter;
volatile unsigned short network_searching_status_led_counter;

void Clock_Config();
void Pins_Config();
void TIMER3_Confing();
void set_flag(unsigned int *flags, unsigned int flag_value);
void reset_flag(unsigned int *flags, unsigned int flag_value);
unsigned char read_flag_state(unsigned int *flags, unsigned int flag_value);
void DMA_Config();
void USART_Config();
void set_appropriate_successfully_recieved_flag();
void disable_echo();
void get_network_list();
void connect_to_network();
void get_connection_status();
void execute_usart_data_sending(void (*function_to_execute)(), unsigned char timeout);
void send_usard_data(char string[]);
unsigned char is_usart_response_contains_elements(char *data_to_be_contained[], unsigned char elements_count);
unsigned char is_usart_response_contains_element(char string_to_be_contained[]);
unsigned char contains_string(char being_compared_string[], char string_to_be_contained[]);
void clear_usart_data_received_buffer();
void *set_string_parameters(char string[], char *parameters[]);
unsigned short get_string_length(char string[]);
unsigned short get_current_piped_task_to_send();
void remove_current_piped_task_to_send();
void add_piped_task_to_send(unsigned short task);
void on_successfully_receive_general_actions(unsigned short successfully_received_flag);

void DMA1_Channel2_3_IRQHandler() {
   DMA_ClearITPendingBit(DMA1_IT_TC2);
}

void TIM3_IRQHandler() {
   TIM_ClearITPendingBit(TIM3, TIM_IT_Update);

   if (usart_received_bytes > 0) {
      usart_received_bytes = 0;
      set_flag(&general_flags, USART_DATA_RECEIVED_FLAG);
   }
   if (send_usart_data_function != NULL) {
      send_usart_data_timer_counter++;
   }
   network_searching_status_led_counter++;
}

void USART1_IRQHandler() {
   if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == SET) {
      TIM_SetCounter(TIM3, 0);
      usart_data_received_buffer[usart_received_bytes] = USART_ReceiveData(USART1);
      usart_received_bytes++;
   } else if (USART_GetFlagStatus(USART1, USART_FLAG_ORE)) {
      USART_ClearITPendingBit(USART1, USART_IT_ORE);
      USART_ClearFlag(USART1, USART_FLAG_ORE);
      send_usart_data_errors_counter++;
   } else if (USART_GetFlagStatus(USART1, USART_FLAG_IDLE)) {
      USART_ClearITPendingBit(USART1, USART_IT_ORE);
      USART_ClearFlag(USART1, USART_FLAG_IDLE);
      send_usart_data_errors_counter++;
   } else if (USART_GetFlagStatus(USART1, USART_FLAG_NE)) {
      USART_ClearITPendingBit(USART1, USART_IT_ORE);
      USART_ClearFlag(USART1, USART_FLAG_NE);
      send_usart_data_errors_counter++;
   } else if (USART_GetFlagStatus(USART1, USART_FLAG_FE)) {
      USART_ClearITPendingBit(USART1, USART_IT_ORE);
      USART_ClearFlag(USART1, USART_FLAG_FE);
      send_usart_data_errors_counter++;
   }
}

int main() {
   Clock_Config();
   Pins_Config();
   DMA_Config();
   USART_Config();
   TIMER3_Confing();

   piped_tasks_to_send[0] = DISABLE_ECHO_FLAG;
   piped_tasks_to_send[1] = OBTAIN_CONNECTION_STATUS_FLAG;

   while (1) {
      // Seconds
      unsigned char send_usart_data_passed_time = (unsigned char)(TIMER3_PERIOD_S * send_usart_data_timer_counter);

      if (read_flag_state(&general_flags, USART_DATA_RECEIVED_FLAG)) {
         reset_flag(&general_flags, USART_DATA_RECEIVED_FLAG);
         send_usart_data_timer_counter = 0;

         if (usart_data_to_be_transmitted_buffer != NULL) {
            free(usart_data_to_be_transmitted_buffer);
            usart_data_to_be_transmitted_buffer = NULL;
         }

         set_appropriate_successfully_recieved_flag();
      } else if (send_usart_data_passed_time >= send_usart_data_timout_s) {
         send_usart_data_timer_counter = 0;
         if (usart_data_to_be_transmitted_buffer != NULL) {
            free(usart_data_to_be_transmitted_buffer);
            usart_data_to_be_transmitted_buffer = NULL;
         }

         if (send_usart_data_function != NULL) {
            send_usart_data_function();
         }
      }

      if (read_flag_state(&successfully_received_flags, DISABLE_ECHO_FLAG)) {
         on_successfully_receive_general_actions(DISABLE_ECHO_FLAG);
      }
      if (read_flag_state(&successfully_received_flags, OBTAIN_CONNECTION_STATUS_FLAG)) {
         reset_flag(&successfully_received_flags, OBTAIN_CONNECTION_STATUS_FLAG);

         if (is_usart_response_contains_element(DEFAULT_ACCESS_POINT_NAME)) {
            // Connected
            set_flag(&general_flags, SUCCESSUFULLY_CONNECTED_TO_NETWORK_FLAG);
            GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_SET);
            on_successfully_receive_general_actions(0);
         } else if (is_usart_response_contains_element(ESP8226_RESPONSE_NOT_CONNECTED_STATUS)) {
            // Connect
            add_piped_task_to_send(GET_VISIBLE_NETWORK_LIST_FLAG);
            add_piped_task_to_send(CONNECT_TO_NETWORK_FLAG);
            on_successfully_receive_general_actions(0);
         } else {
            send_usart_data_errors_counter++;
         }
      }
      if (read_flag_state(&successfully_received_flags, GET_VISIBLE_NETWORK_LIST_FLAG)) {
         on_successfully_receive_general_actions(GET_VISIBLE_NETWORK_LIST_FLAG);
      }
      if (read_flag_state(&successfully_received_flags, CONNECT_TO_NETWORK_FLAG)) {
         on_successfully_receive_general_actions(CONNECT_TO_NETWORK_FLAG);

         set_flag(&general_flags, SUCCESSUFULLY_CONNECTED_TO_NETWORK_FLAG);
         GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_SET);
      }

      unsigned short current_piped_task_to_send = get_current_piped_task_to_send();

      if (send_usart_data_function == NULL && current_piped_task_to_send != 0) {
         if (current_piped_task_to_send == DISABLE_ECHO_FLAG) {
            execute_usart_data_sending(disable_echo, 10);
         } else if (current_piped_task_to_send == OBTAIN_CONNECTION_STATUS_FLAG) {
            execute_usart_data_sending(get_connection_status, 5);
         } else if (current_piped_task_to_send == GET_VISIBLE_NETWORK_LIST_FLAG) {
            execute_usart_data_sending(get_network_list, 30);
         } else if (current_piped_task_to_send == CONNECT_TO_NETWORK_FLAG) {
            execute_usart_data_sending(connect_to_network, 10);
         }
      }

      // LED blinking
      if (network_searching_status_led_counter >= TIMER3_100MS && !read_flag_state(&general_flags, SUCCESSUFULLY_CONNECTED_TO_NETWORK_FLAG)) {
         network_searching_status_led_counter = 0;

         if (read_flag_state(&general_flags, NETWORK_SEARCHING_STATUS_LED_FLAG)) {
            reset_flag(&general_flags, NETWORK_SEARCHING_STATUS_LED_FLAG);
            GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_SET);
         } else {
            set_flag(&general_flags, NETWORK_SEARCHING_STATUS_LED_FLAG);
            GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET);
         }
      }
   }
}

void on_successfully_receive_general_actions(unsigned short successfully_received_flag) {
   if (successfully_received_flag) {
      reset_flag(&successfully_received_flags, successfully_received_flag);
   }
   send_usart_data_function = NULL;
   send_usart_data_errors_counter = 0;
   remove_current_piped_task_to_send();
}

unsigned short get_current_piped_task_to_send() {
   return piped_tasks_to_send[0];
}

void remove_current_piped_task_to_send() {
   for (unsigned char i = 0; piped_tasks_to_send[i] != 0; i++) {
      unsigned short next_task = piped_tasks_to_send[i + 1];
      piped_tasks_to_send[i] = next_task;
   }
}

void add_piped_task_to_send(unsigned short task) {
   for (unsigned char i = 0; i < 10; i++) {
      if (piped_tasks_to_send[i] == 0) {
         piped_tasks_to_send[i] = task;
         break;
      }
   }
}

void set_appropriate_successfully_recieved_flag() {
   if (read_flag_state(&sent_flag, DISABLE_ECHO_FLAG)) {
      reset_flag(&sent_flag, DISABLE_ECHO_FLAG);

      if (is_usart_response_contains_element(USART_OK)) {
         set_flag(&successfully_received_flags, DISABLE_ECHO_FLAG);
      } else {
         send_usart_data_errors_counter++;
      }
   }
   if (read_flag_state(&sent_flag, GET_VISIBLE_NETWORK_LIST_FLAG)) {
      reset_flag(&sent_flag, GET_VISIBLE_NETWORK_LIST_FLAG);

      if (is_usart_response_contains_element(DEFAULT_ACCESS_POINT_NAME)) {
         set_flag(&successfully_received_flags, GET_VISIBLE_NETWORK_LIST_FLAG);
      } else {
         send_usart_data_errors_counter++;
      }
   }
   if (read_flag_state(&sent_flag, CONNECT_TO_NETWORK_FLAG)) {
      reset_flag(&sent_flag, CONNECT_TO_NETWORK_FLAG);

      if (is_usart_response_contains_element(USART_OK)) {
         set_flag(&successfully_received_flags, CONNECT_TO_NETWORK_FLAG);
      } else {
         send_usart_data_errors_counter++;
      }
   }
   if (read_flag_state(&sent_flag, OBTAIN_CONNECTION_STATUS_FLAG)) {
      reset_flag(&sent_flag, OBTAIN_CONNECTION_STATUS_FLAG);
      set_flag(&successfully_received_flags, OBTAIN_CONNECTION_STATUS_FLAG);
   }
}

unsigned char is_usart_response_contains_element(char string_to_be_contained[]) {
   if (contains_string(usart_data_received_buffer, string_to_be_contained)) {
      return 1;
   } else {
      return 0;
   }
}

//char *data_to_be_contained[] = {ESP8226_REQUEST_DISABLE_ECHO, USART_OK};
unsigned char is_usart_response_contains_elements(char *data_to_be_contained[], unsigned char elements_count) {
   for (unsigned char elements_index = 0; elements_index < elements_count; elements_index++) {
      if (!contains_string(usart_data_received_buffer, data_to_be_contained[elements_index])) {
         return 0;
      }
   }
   return 1;
}

unsigned char contains_string(char being_compared_string[], char string_to_be_contained[]) {
   unsigned char found = 0;

   if (*being_compared_string == '\0' || *string_to_be_contained == '\0') {
      return found;
   }

   for (; *being_compared_string != '\0'; being_compared_string++) {
      unsigned char all_chars_are_equal = 1;

      for (char *char_address = string_to_be_contained; *char_address != '\0';
            char_address++, being_compared_string++) {
         if (*being_compared_string == '\0') {
            return found;
         }

         all_chars_are_equal = *being_compared_string == *char_address ? 1 : 0;

         if (!all_chars_are_equal) {
            break;
         }
      }

      if (all_chars_are_equal) {
         found = 1;
         break;
      }
   }
   return found;
}

void disable_echo() {
   send_usard_data(ESP8226_REQUEST_DISABLE_ECHO);
   set_flag(&sent_flag, DISABLE_ECHO_FLAG);
}

void get_network_list() {
   send_usard_data(ESP8226_REQUEST_GET_VISIBLE_NETWORK_LIST);
   set_flag(&sent_flag, GET_VISIBLE_NETWORK_LIST_FLAG);
}

void get_connection_status() {
   send_usard_data(ESP8226_REQUEST_GET_CONNECTION_STATUS);
   set_flag(&sent_flag, OBTAIN_CONNECTION_STATUS_FLAG);
}

void connect_to_network() {
   char *parameters[] = {DEFAULT_ACCESS_POINT_NAME, DEFAULT_ACCESS_POINT_PASSWORD, NULL};
   usart_data_to_be_transmitted_buffer = set_string_parameters(ESP8226_REQUEST_CONNECT_TO_NETWORK_AND_SAVE, parameters);
   send_usard_data(usart_data_to_be_transmitted_buffer);
   set_flag(&sent_flag, CONNECT_TO_NETWORK_FLAG);
}

void execute_usart_data_sending(void (*function_to_execute)(), unsigned char timeout) {
   send_usart_data_timout_s = timeout;
   send_usart_data_function = function_to_execute;
   function_to_execute();
}

void Clock_Config() {
   RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);
   RCC_PLLCmd(DISABLE);
   while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == SET);
   RCC_PLLConfig(RCC_PLLSource_HSI_Div2, RCC_PLLMul_4); // 8MHz / 2 * 4
   RCC_PCLKConfig(RCC_HCLK_Div1);
   RCC_PLLCmd(ENABLE);
   while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);
   RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
}

void Pins_Config() {
   // Connect BOOT0 to ground

   RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA | RCC_AHBPeriph_GPIOB, ENABLE);

   GPIO_InitTypeDef gpioInitType;
   gpioInitType.GPIO_Pin = 0x99FD; // Pins PA0, 2 - 8, 11, 12, 15. PA13, PA14 - Debugger pins
   gpioInitType.GPIO_Mode = GPIO_Mode_IN;
   gpioInitType.GPIO_Speed = GPIO_Speed_Level_2; // 10 MHz
   gpioInitType.GPIO_PuPd = GPIO_PuPd_UP;
   GPIO_Init(GPIOA, &gpioInitType);

   gpioInitType.GPIO_Pin = (1<<GPIO_PinSource9) | (1<<GPIO_PinSource10);
   gpioInitType.GPIO_PuPd = GPIO_PuPd_NOPULL;
   gpioInitType.GPIO_Mode = GPIO_Mode_AF;
   gpioInitType.GPIO_OType = GPIO_OType_PP;
   GPIO_Init(GPIOA, &gpioInitType);

   // For USART1
   GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_1);
   GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_1);

   gpioInitType.GPIO_Pin = GPIO_Pin_All;
   gpioInitType.GPIO_Mode = GPIO_Mode_IN;
   gpioInitType.GPIO_PuPd = GPIO_PuPd_UP;
   GPIO_Init(GPIOB, &gpioInitType);

   // PA1 LED
   gpioInitType.GPIO_Pin = GPIO_Pin_1;
   gpioInitType.GPIO_Mode = GPIO_Mode_OUT;
   gpioInitType.GPIO_Speed = GPIO_Speed_Level_1;
   gpioInitType.GPIO_PuPd = GPIO_PuPd_DOWN;
   gpioInitType.GPIO_OType = GPIO_OType_PP;
   GPIO_Init(GPIOA, &gpioInitType);
}

/**
 * USART frame time Tfr = (1 / USART_BAUD_RATE) * 10bits
 * Timer time to be sure the frame is ended Tt = Tfr + 0.5Tfr
 * Frequency = 16Mhz, USART_BAUD_RATE = 115200. Tt = 0.13ms
 */
void TIMER3_Confing() {
   RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

   TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
   TIM_TimeBaseStructure.TIM_Period = CLOCK_SPEED * 15 / USART_BAUD_RATE;
   TIM_TimeBaseStructure.TIM_Prescaler = 0;
   TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
   TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
   TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

   NVIC_EnableIRQ(TIM3_IRQn);
   TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

   TIM_Cmd(TIM3, ENABLE);
}

void DMA_Config() {
   RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1 , ENABLE);

   DMA_InitTypeDef dmaInitType;
   dmaInitType.DMA_PeripheralBaseAddr = USART1_TDR_ADDRESS;
   //dmaInitType.DMA_MemoryBaseAddr = (uint32_t)(&usartDataToBeTransmitted);
   dmaInitType.DMA_DIR = DMA_DIR_PeripheralDST; // Specifies if the peripheral is the source or destination
   dmaInitType.DMA_BufferSize = 0;
   dmaInitType.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
   dmaInitType.DMA_MemoryInc = DMA_MemoryInc_Enable; // DMA_MemoryInc_Enable if DMA_InitTypeDef.DMA_BufferSize > 1
   dmaInitType.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
   dmaInitType.DMA_MemoryDataSize = DMA_PeripheralDataSize_Byte;
   dmaInitType.DMA_Mode = DMA_Mode_Normal;
   dmaInitType.DMA_Priority = DMA_Priority_High;
   dmaInitType.DMA_M2M = DMA_M2M_Disable;
   DMA_Init(USART1_TX_DMA_CHANNEL, &dmaInitType);

   DMA_ITConfig(USART1_TX_DMA_CHANNEL, DMA_IT_TC, ENABLE);
   NVIC_EnableIRQ(USART1_IRQn);

   DMA_Cmd(USART1_TX_DMA_CHANNEL, ENABLE);
}

void USART_Config() {
   RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

   USART_OverSampling8Cmd(USART1, DISABLE);

   USART_InitTypeDef USART_InitStructure;
   USART_InitStructure.USART_BaudRate = USART_BAUD_RATE;
   USART_InitStructure.USART_WordLength = USART_WordLength_8b;
   USART_InitStructure.USART_StopBits = USART_StopBits_1;
   USART_InitStructure.USART_Parity = USART_Parity_No;
   USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
   USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
   USART_Init(USART1, &USART_InitStructure);

   USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
   USART_ITConfig(USART1, USART_IT_ERR, ENABLE);
   NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);

   USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE);

   USART_Cmd(USART1, ENABLE);
}

void set_flag(unsigned int *flags, unsigned int flag_value) {
   *flags |= flag_value;
}

void reset_flag(unsigned int *flags, unsigned int flag_value) {
   *flags &= ~(*flags & flag_value);
}

unsigned char read_flag_state(unsigned int *flags, unsigned int flag_value) {
   return *flags & flag_value;
}

void send_usard_data(char *string) {
   clear_usart_data_received_buffer();
   DMA_Cmd(USART1_TX_DMA_CHANNEL, DISABLE);
   unsigned char bytes_to_send = get_string_length(string);

   if (bytes_to_send == 0) {
      return;
   }

   DMA_SetCurrDataCounter(USART1_TX_DMA_CHANNEL, bytes_to_send);
   USART1_TX_DMA_CHANNEL->CMAR = (unsigned int) string;
   USART_ClearFlag(USART1, USART_FLAG_TC);
   DMA_Cmd(USART1_TX_DMA_CHANNEL, ENABLE);
}

/**
 * Supports only 9 parameters (1 - 9). Do not forget to call free() function on returned pointer when it's no longer needed
 *
 * *parameters array of pointers to strings. The last parameter has to be NULL
 */
void *set_string_parameters(char string[], char *parameters[]) {
   unsigned char open_brace_found = 0;
   unsigned char parameters_length = 0;
   unsigned short result_string_length = 0;

   for (; parameters[parameters_length] != NULL; parameters_length++) {
   }

   // Calculate the length without symbols to be replaced ('{x}')
   for (char *string_pointer = string; *string_pointer != '\0'; string_pointer++) {
      if (*string_pointer == '{') {
         if (open_brace_found) {
            return NULL;
         }
         open_brace_found = 1;
         continue;
      }
      if (*string_pointer == '}') {
         if (!open_brace_found) {
            return NULL;
         }
         open_brace_found = 0;
         continue;
      }
      if (open_brace_found) {
         continue;
      }

      result_string_length++;
   }

   if (open_brace_found) {
      return NULL;
   }

   for (unsigned char i = 0; parameters[i] != NULL; i++) {
      result_string_length += get_string_length(parameters[i]);
   }
   // 1 is for the last \0 character
   result_string_length++;

   char *a;
   a = malloc(result_string_length); // (string_length + 1) * sizeof(char)

   if (a == NULL) {
      return NULL;
   }

   unsigned char result_string_index = 0, input_string_index = 0;
   for (; result_string_index < result_string_length - 1; result_string_index++) {
      char input_string_symbol = string[input_string_index];

      if (input_string_symbol == '{') {
         input_string_index++;
         input_string_symbol = string[input_string_index] ;

         if (input_string_symbol < '1' || input_string_symbol > '9') {
            return NULL;
         }

         input_string_symbol -= 48; // Now it's not a char character, but a number
         if (input_string_symbol > parameters_length) {
            return NULL;
         }
         input_string_index += 2;

         // Parameters are starting with 1
         char *parameter = parameters[input_string_symbol - 1];

         for (; *parameter != '\0'; parameter++, result_string_index++) {
            *(a + result_string_index) = *parameter;
         }
         result_string_index--;
      } else {
         *(a + result_string_index) = string[input_string_index];
         input_string_index++;
      }
   }
   *(a + result_string_length - 1) = '\0';
   return a;
}

unsigned short get_string_length(char string[]) {
   unsigned short length = 0;

   for (char *string_pointer = string; *string_pointer != '\0'; string_pointer++, length++) {
   }
   return length;
}

void clear_usart_data_received_buffer() {
   for (int i = 0; i < USART_DATA_RECEIVED_BUFFER_SIZE; i++) {
      if (usart_data_received_buffer[i] == '\0') {
         break;
      }

      usart_data_received_buffer[i] = '\0';
   }
}

/**
 * unsigned char  uint8_t
 * unsigned short uint16_t
 * unsigned int   uint32_t
 */
#include "stm32f0xx.h"
//#include "arm_math.h"

#define CLOCK_SPEED 16000000
#define USART_BAUD_RATE 115200

#define USART1_TX_DMA_CHANNEL DMA1_Channel2
#define USART1_TDR_ADDRESS (unsigned int)(&(USART1->TDR))

#define USART_DATA_RECEIVED_FLAG 1
#define GET_VISIBLE_NETWORK_LIST_REQUEST_SENT_FLAG 2

char USART_OK[] __attribute__ ((section(".text.const"))) = "OK";
char ESP8226_REQUEST_GET_VISIBLE_NETWORK_LIST[] __attribute__ ((section(".text.const"))) = "AT+CWLAP\r\n";
char ESP8226_REQUEST_GET_VERSION_ID[] __attribute__ ((section(".text.const"))) = "AT+GMR\r\n";

volatile char usart_data_to_be_transmitted[100];
volatile char usart_data_received[100];
volatile unsigned char usart_received_bytes;
volatile unsigned char overrun_errors;

volatile unsigned char general_flags;

void Clock_Config();
void Pins_Config();
void TIMER3_Confing();
void set_flag(unsigned char flag);
void reset_flag(unsigned char flag);
unsigned char read_flag_state(unsigned char flag);
void DMA_Config();
void USART_Config();
void send_usard_data_from_constant(char string[]);

void DMA1_Channel2_3_IRQHandler() {
   DMA_ClearITPendingBit(DMA1_IT_TC2);
}

void TIM3_IRQHandler() {
   TIM_ClearITPendingBit(TIM3, TIM_IT_Update);

   if (usart_received_bytes > 0) {
      usart_received_bytes = 0;
      set_flag(USART_DATA_RECEIVED_FLAG);
   }
}

void USART1_IRQHandler() {
   if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == SET) {
      TIM_SetCounter(TIM3, 0);
      usart_data_received[usart_received_bytes] = USART_ReceiveData(USART1);
      usart_received_bytes++;
   } else if (USART_GetFlagStatus(USART1, USART_FLAG_ORE)) {
      USART_ClearITPendingBit(USART1, USART_IT_ORE);
      USART_ClearFlag(USART1, USART_FLAG_ORE);
      overrun_errors++;
   }
}

int main() {
   Clock_Config();
   Pins_Config();
   DMA_Config();
   USART_Config();
   //TIMER3_Confing();

   send_usard_data_from_constant(ESP8226_REQUEST_GET_VISIBLE_NETWORK_LIST);
   set_flag(GET_VISIBLE_NETWORK_LIST_REQUEST_SENT_FLAG);

   while (1) {
      volatile unsigned int cnt = 3200000;
      while (--cnt > 0);

      if (read_flag_state(USART_DATA_RECEIVED_FLAG)) {
         reset_flag(USART_DATA_RECEIVED_FLAG);

         if (read_flag_state(GET_VISIBLE_NETWORK_LIST_REQUEST_SENT_FLAG))
         {
            reset_flag(GET_VISIBLE_NETWORK_LIST_REQUEST_SENT_FLAG);
         }
      }
   }
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
   gpioInitType.GPIO_Pin = 0x99FF; // Pins 0 - 8, 11, 12, 15. PA13, PA14 - Debugger pins
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

void set_flag(unsigned char flag) {
   general_flags |= flag;
}

void reset_flag(unsigned char flag) {
   general_flags &= ~(general_flags & flag);
}

unsigned char read_flag_state(unsigned char flag) {
   return general_flags & flag;
}

void send_usard_data_from_constant(char string[]) {
   DMA_Cmd(USART1_TX_DMA_CHANNEL, DISABLE);
   unsigned int first_element_address = (unsigned int) string;

   unsigned int bytes_to_send;
   for (bytes_to_send = 0; *string != '\0'; string++, bytes_to_send++)
   {}

   if (bytes_to_send > 0) {
      DMA_SetCurrDataCounter(USART1_TX_DMA_CHANNEL, bytes_to_send);
      USART1_TX_DMA_CHANNEL->CMAR = first_element_address;
      USART_ClearFlag(USART1, USART_FLAG_TC);
      DMA_Cmd(USART1_TX_DMA_CHANNEL, ENABLE);
   }
}

#include "stm32f0xx.h"
#include "arm_math.h"

#define USART1_TX_DMA_CHANNEL DMA1_Channel2
#define USART1_RX_DMA_CHANNEL DMA1_Channel3

#define USART1_TDR_ADDRESS (uint32_t)(&(USART1->TDR))
#define USART1_RDR_ADDRESS (uint32_t)(&(USART1->RDR))

const char esp8226_request_version_id[] __attribute__ ((section(".text.const"))) = "AT+GMR";
const char constant[] __attribute__ ((section(".text.const"))) = "123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456123456";

char usartDataToBeTransmitted[100];
char usartDataReceived[100];

uint8_t generalFlags;

void Clock_Config();
void Pins_Config();
void TIMER3_Confing();
void SetFlag(uint8_t flag);
void SesetFlag(uint8_t flag);
void DMA_Config();
void USART_Config();
void SendUsartData(const char *string);

void DMA1_Channel1_IRQHandler()
{
   DMA_ClearITPendingBit(DMA1_IT_TC1);
}

void TIM3_IRQHandler()
{
   TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
}

int main()
{
   Clock_Config();
   Pins_Config();
   DMA_Config();
   USART_Config();
   //TIMER3_Confing();

   while (1)
   {
      volatile uint32_t cnt = 10000000;
      while (--cnt > 0);

      SendUsartData(&esp8226_request_version_id[0]);
   }
}

void Clock_Config()
{
   RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);
   RCC_PLLCmd(DISABLE);
   while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == SET);
   RCC_PLLConfig(RCC_PLLSource_HSI_Div2, RCC_PLLMul_4); // 8MHz / 2 * 4
   RCC_PCLKConfig(RCC_HCLK_Div1);
   RCC_PLLCmd(ENABLE);
   while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);
   RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
}

void Pins_Config()
{
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

void TIMER3_Confing()
{
   RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

   TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
   TIM_TimeBaseStructure.TIM_Period = 0xFFFF; // 100
   TIM_TimeBaseStructure.TIM_Prescaler = 0; // 60000. 48MHz / 16 / 60000 * 2. The counter clock frequency CK_CNT is equal to fCK_PSC/ (PSC[15:0] + 1).
   TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
   TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
   TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

   /*NVIC_EnableIRQ(TIM3_IRQn);
   NVIC_SetPriority(TIM3_IRQn, 0);
   TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

   TIM_Cmd(TIM3, ENABLE);*/
}

void DMA_Config()
{
   RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1 , ENABLE);

   DMA_InitTypeDef dmaInitType;
   dmaInitType.DMA_PeripheralBaseAddr = USART1_TDR_ADDRESS;
   dmaInitType.DMA_MemoryBaseAddr = (uint32_t)(&usartDataToBeTransmitted[0]);
   dmaInitType.DMA_DIR = DMA_DIR_PeripheralDST; // Specifies if the peripheral is the source or destination
   dmaInitType.DMA_BufferSize = 0;
   dmaInitType.DMA_PeripheralInc = DMA_PeripheralInc_Enable;
   dmaInitType.DMA_MemoryInc = DMA_MemoryInc_Enable; // DMA_MemoryInc_Enable if DMA_InitTypeDef.DMA_BufferSize > 1
   dmaInitType.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
   dmaInitType.DMA_MemoryDataSize = DMA_PeripheralDataSize_Byte;
   dmaInitType.DMA_Mode = DMA_Mode_Normal;
   dmaInitType.DMA_Priority = DMA_Priority_High;
   dmaInitType.DMA_M2M = DMA_M2M_Disable;
   DMA_Init(USART1_TX_DMA_CHANNEL, &dmaInitType);

   DMA_ITConfig(USART1_TX_DMA_CHANNEL, DMA_IT_TC, ENABLE);
   NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);

   DMA_Cmd(USART1_TX_DMA_CHANNEL, ENABLE);
}

void USART_Config()
{
   RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

   USART_OverSampling8Cmd(USART1, DISABLE);

   USART_InitTypeDef USART_InitStructure;
   USART_InitStructure.USART_BaudRate = 115200;
   USART_InitStructure.USART_WordLength = USART_WordLength_8b;
   USART_InitStructure.USART_StopBits = USART_StopBits_1;
   USART_InitStructure.USART_Parity = USART_Parity_No;
   USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
   USART_InitStructure.USART_Mode = USART_Mode_Tx; // USART_Mode_Rx | USART_Mode_Tx;
   USART_Init(USART1, &USART_InitStructure);

   //USART_ITConfig(USART1, USART_IT_TC, ENABLE);
   USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE);

   USART_Cmd(USART1, ENABLE);
}

void SetFlag(uint8_t flag)
{
   generalFlags |= flag;
}

void ResetFlag(uint8_t flag)
{
   generalFlags &= ~(generalFlags & flag);
}

void SendUsartData(const char *string)
{
   DMA_Cmd(USART1_TX_DMA_CHANNEL, DISABLE);

   unsigned int i;
   for (i = 0; *string != '\0'; string++)
   {
      usartDataToBeTransmitted[i] = *string;
   }

   USART1_TX_DMA_CHANNEL->CNDTR = i;
   USART_ClearFlag(USART1, USART_FLAG_TC);
   DMA_Cmd(USART1_TX_DMA_CHANNEL, ENABLE);
}

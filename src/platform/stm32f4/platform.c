// Platform-dependent functions

#include "platform.h"
#include "type.h"
#include "devman.h"
#include "genstd.h"
#include <reent.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "uip_arp.h"
#include "elua_uip.h"
#include "elua_adc.h"
#include "uip-conf.h"
#include "platform_conf.h"
#include "diskio.h"
#include "common.h"
#include "buf.h"
#include "utils.h"
#include "lua.h"
#include "lauxlib.h"
#include "lrotable.h"
#include "gsm.h"
#include "gps.h"

// Platform specific includes
#include "stm32f4xx_conf.h"

// SysTick Config Data
// NOTE: when using virtual timers, SYSTICKHZ and VTMR_FREQ_HZ should have the
// same value, as they're served by the same timer (the systick)
// Max SysTick preload value is 16777215, for STM32F103RET6 @ 72 MHz, lowest acceptable rate would be about 5 Hz
#define SYSTICKHZ               1000
#define SYSTICKMS               (1000 / SYSTICKHZ)

#if ( (HCLK / SYSTICKHZ)  > SysTick_LOAD_RELOAD_Msk)
#error  "Sys tick reload value out of range"
#endif

// ****************************************************************************
// Platform initialization

// forward dcls
static void NVIC_Configuration(void);

static void timers_init();
static void pwms_init();
static void spis_init();
static void pios_init();
#ifdef BUILD_ADC
static void adcs_init();
#endif
#if (NUM_CAN > 0)
static void cans_init();
#endif
extern void usb_init();
extern void uarts_init();
static void i2cs_init();


int platform_init()
{
  // Setup IRQ's
  NVIC_Configuration();

  // Setup PIO
  pios_init();

  // Setup UARTs
  uarts_init();

  // Setup SPIs
  spis_init();

  // Setup timers
  timers_init();

  // Setup PWMs
  pwms_init();

#ifdef BUILD_ADC
  // Setup ADCs
  adcs_init();
#endif

#if (NUM_I2C > 0)
  i2cs_init();
#endif

#if (NUM_CAN > 0)
  // Setup CANs
  cans_init();
#endif

#ifdef BUILD_GSM
  gsm_setup_io();
#endif

#ifdef BUILD_GPS
  gps_setup_io();
#endif

  // Setup system timer
  cmn_systimer_set_base_freq( HCLK );
  cmn_systimer_set_interrupt_freq( SYSTICKHZ );

  // Enable SysTick
  if ( SysTick_Config( HCLK / SYSTICKHZ ) )
  {
    /* Capture error */
    while (1);
  }

  cmn_platform_init();
  
  usb_init();

  // All done
  return PLATFORM_OK;
}

#define SPEED_100K  100000
#define SPEED_400K  400000
static void i2cs_init()
{
  int i;
  for (i=0;i<NUM_I2C;i++)
    platform_i2c_setup(i, SPEED_400K);
}

// ****************************************************************************
// NVIC
// Shared by all STM32 devices.

/*******************************************************************************
* Function Name  : NVIC_Configuration
* Description    : Configures the nested vectored interrupt controller.
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
/* This struct is used for later reconfiguration of ADC interrupt */
NVIC_InitTypeDef nvic_init_structure_adc;

static void NVIC_Configuration(void)
{
  NVIC_InitTypeDef nvic_init_structure;

#ifdef  VECT_TAB_RAM
  /* Set the Vector Table base location at 0x20000000 */
  NVIC_SetVectorTable(NVIC_VectTab_RAM, 0x0);
#else  /* VECT_TAB_FLASH  */
  /* Set the Vector Table base location at 0x08000000 */
  NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x0);
#endif

  /* Configure the NVIC Preemption Priority Bits */
  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

  nvic_init_structure.NVIC_IRQChannel = SysTick_IRQn;
  nvic_init_structure.NVIC_IRQChannelPreemptionPriority = 0;
  nvic_init_structure.NVIC_IRQChannelSubPriority = 0;
  nvic_init_structure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&nvic_init_structure);

#ifdef BUILD_ADC
  nvic_init_structure_adc.NVIC_IRQChannel = DMA2_Stream0_IRQn;
  nvic_init_structure_adc.NVIC_IRQChannelPreemptionPriority = 1;
  nvic_init_structure_adc.NVIC_IRQChannelSubPriority = 1;
  nvic_init_structure_adc.NVIC_IRQChannelCmd = DISABLE;
  NVIC_Init(&nvic_init_structure_adc);
#endif
}

// ****************************************************************************
// PIO
// This is pretty much common code to all STM32 devices.
// todo: Needs updates to support different processor lines.
static GPIO_TypeDef * const pio_port[] = { GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG, GPIOH, GPIOI };
static const u32 pio_port_clk[]        = { RCC_AHB1Periph_GPIOA , RCC_AHB1Periph_GPIOB
         , RCC_AHB1Periph_GPIOC , RCC_AHB1Periph_GPIOD , RCC_AHB1Periph_GPIOE
         , RCC_AHB1Periph_GPIOF , RCC_AHB1Periph_GPIOG , RCC_AHB1Periph_GPIOH
         , RCC_AHB1Periph_GPIOI };

static void pios_init()
{
  GPIO_InitTypeDef GPIO_InitStructure;
  int port;

  for( port = 0; port < NUM_PIO; port++ )
  {
    // Enable clock to port.
    RCC_AHB1PeriphClockCmd(pio_port_clk[port], ENABLE);

    // Default all port pins to input and enable port.
    GPIO_StructInit(&GPIO_InitStructure);
#if defined(ENABLE_JTAG_SWD)
    //Except JTAG pins
    if (port==0) {
      GPIO_InitStructure.GPIO_Pin = ~(GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15);
    }
#endif
    GPIO_Init(pio_port[port], &GPIO_InitStructure);
  }

#if defined(ENABLE_JTAG_SWD) || defined(ENABLE_TRACE)
  //Mapping JTAG / SWD pins
  GPIO_PinAFConfig(GPIOB, GPIO_PinSource4,  GPIO_AF_SWJ); // PB4  TRST
  GPIO_PinAFConfig(GPIOB, GPIO_PinSource3,  GPIO_AF_SWJ); // PB3  TDO   / SWO

  GPIO_PinAFConfig(GPIOA, GPIO_PinSource13, GPIO_AF_SWJ); // PA13 TMS   / SWDIO
  GPIO_PinAFConfig(GPIOA, GPIO_PinSource14, GPIO_AF_SWJ); // PA14 TCK   / SWDCLK
  GPIO_PinAFConfig(GPIOA, GPIO_PinSource15, GPIO_AF_SWJ); // PA15 TDI

  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_4;
  GPIO_Init(GPIOB, &GPIO_InitStructure);
#endif

#ifdef ENABLE_TRACE
  //Mapping TRACE pins, PE2,3,4,5,6
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6;
  GPIO_Init(GPIOE, &GPIO_InitStructure);

  GPIO_PinAFConfig(GPIOE, GPIO_PinSource2, GPIO_AF_TRACE);
  GPIO_PinAFConfig(GPIOE, GPIO_PinSource3, GPIO_AF_TRACE);
  GPIO_PinAFConfig(GPIOE, GPIO_PinSource4, GPIO_AF_TRACE);
  GPIO_PinAFConfig(GPIOE, GPIO_PinSource5, GPIO_AF_TRACE);
  GPIO_PinAFConfig(GPIOE, GPIO_PinSource6, GPIO_AF_TRACE);
#endif

}

pio_type platform_pio_op( unsigned port, pio_type pinmask, int op )
{
  pio_type retval = 1;
  GPIO_InitTypeDef GPIO_InitStructure;
  GPIO_TypeDef * base = pio_port[ port ];


  GPIO_StructInit(&GPIO_InitStructure);

  switch( op )
  {
    case PLATFORM_IO_PORT_SET_VALUE:
      GPIO_Write(base, pinmask);
      break;

    case PLATFORM_IO_PIN_SET:
      GPIO_SetBits(base, pinmask);
      break;

    case PLATFORM_IO_PIN_CLEAR:
      GPIO_ResetBits(base, pinmask);
      break;

    case PLATFORM_IO_PORT_DIR_INPUT:
      pinmask = GPIO_Pin_All;
    case PLATFORM_IO_PIN_DIR_INPUT:
      GPIO_InitStructure.GPIO_Pin  = pinmask;
      GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;

      GPIO_Init(base, &GPIO_InitStructure);
      break;

    case PLATFORM_IO_PORT_DIR_OUTPUT:
      pinmask = GPIO_Pin_All;
    case PLATFORM_IO_PIN_DIR_OUTPUT:
      GPIO_InitStructure.GPIO_Pin   = pinmask;
      GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
      GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
      
      /* BUGFIX: RuuviTracker Rev B1 board may BURN GSM module, if PWR_KEY(PE2) is driven to 3.3V */
      /* Force PE2 to be Open-Drain */
#if defined( ELUA_BOARD_RUUVIB1 )
      if ((base == GPIOE) && (pinmask&GPIO_Pin_2)) {
	GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
	GPIO_Init(base, &GPIO_InitStructure);
	if (pinmask!=GPIO_Pin_2) { // Configure other pins to normal output
	  GPIO_InitStructure.GPIO_Pin = pinmask&~GPIO_Pin_2;
	  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	  GPIO_Init(base, &GPIO_InitStructure);
	}
      } else {
	GPIO_Init(base, &GPIO_InitStructure);
      }
#else
      GPIO_Init(base, &GPIO_InitStructure);
#endif
      break;

    case PLATFORM_IO_PORT_GET_VALUE:
      retval = pinmask == PLATFORM_IO_READ_IN_MASK ? GPIO_ReadInputData(base) : GPIO_ReadOutputData(base);
      break;

    case PLATFORM_IO_PIN_GET:
      retval = GPIO_ReadInputDataBit(base, pinmask);
      break;

    case PLATFORM_IO_PIN_PULLUP:
      GPIO_InitStructure.GPIO_Pin   = pinmask;
      GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;

      GPIO_Init(base, &GPIO_InitStructure);
      break;

    case PLATFORM_IO_PIN_PULLDOWN:
      GPIO_InitStructure.GPIO_Pin   = pinmask;
      GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_DOWN;

      GPIO_Init(base, &GPIO_InitStructure);
      break;

    case PLATFORM_IO_PIN_NOPULL:
      GPIO_InitStructure.GPIO_Pin   = pinmask;
      GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;

      GPIO_Init(base, &GPIO_InitStructure);
      break;

    default:
      retval = 0;
      break;
  }
  return retval;
}

// ****************************************************************************
// SPI
// NOTE: Only configuring 2 SPI peripherals, since the third one shares pins with JTAG

static SPI_TypeDef *const spi[]  = { SPI1, SPI2, SPI3 };
static const u8 spi_AF[]  = { GPIO_AF_SPI1, GPIO_AF_SPI2, GPIO_AF_SPI3 };

static const u16 spi_prescaler[] = { SPI_BaudRatePrescaler_2, SPI_BaudRatePrescaler_4, SPI_BaudRatePrescaler_8,
                                     SPI_BaudRatePrescaler_16, SPI_BaudRatePrescaler_32, SPI_BaudRatePrescaler_64,
                                     SPI_BaudRatePrescaler_128, SPI_BaudRatePrescaler_256 };

static const u8 spi_gpio_pins_source[][3] = {
  //SCK,    MISO,   MOSI
  {GPIO_PinSource5  , GPIO_PinSource6  , GPIO_PinSource7},
  {GPIO_PinSource13 , GPIO_PinSource14 , GPIO_PinSource15},
  {GPIO_PinSource10 , GPIO_PinSource11 , GPIO_PinSource12}
};

static const u16 spi_gpio_pins[] = { GPIO_Pin_5  | GPIO_Pin_6  | GPIO_Pin_7,
                                     GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15,
                                     GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_12,
                                   };
//                                   SCK           MISO          MOSI
static GPIO_TypeDef *const spi_gpio_port[] = { GPIOA, GPIOB, GPIOC };

static void spis_init()
{
  // Enable Clocks
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI3, ENABLE);
}

#define SPI_GET_BASE_CLK( id ) ( ( id ) == 0 ? ( HCLK / PCLK2_DIV ) : ( HCLK / PCLK1_DIV ) )

u32 platform_spi_setup( unsigned id, int mode, u32 clock, unsigned cpol, unsigned cpha, unsigned databits )
{
  SPI_InitTypeDef SPI_InitStructure;
  GPIO_InitTypeDef GPIO_InitStructure;
  u8 prescaler_idx = intlog2( ( unsigned ) ( SPI_GET_BASE_CLK( id ) / clock ) );
  int i;

  if ( prescaler_idx < 0 )
    prescaler_idx = 0;
  if ( prescaler_idx > 7 )
    prescaler_idx = 7;

  //Connect pin to SPI
  for(i = 0; i < 3; i++)
  {
    GPIO_PinAFConfig(spi_gpio_port[id], spi_gpio_pins_source[id][i], spi_AF[id]);
  }

  /* Configure SPI pins */
  GPIO_InitStructure.GPIO_Pin = spi_gpio_pins[ id ];
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP; //pull-up or pull-down
  GPIO_Init(spi_gpio_port[ id ], &GPIO_InitStructure);

  SPI_I2S_DeInit(spi[ id ]);

  /* Take down, then reconfigure SPI peripheral */
  SPI_Cmd( spi[ id ], DISABLE );
  SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
  SPI_InitStructure.SPI_Mode = mode ? SPI_Mode_Master : SPI_Mode_Slave;
  SPI_InitStructure.SPI_DataSize = ( databits == 16 ) ? SPI_DataSize_16b : SPI_DataSize_8b; // not ideal, but defaults to sane 8-bits
  SPI_InitStructure.SPI_CPOL = cpol ? SPI_CPOL_High : SPI_CPOL_Low;
  SPI_InitStructure.SPI_CPHA = cpha ? SPI_CPHA_2Edge : SPI_CPHA_1Edge;
  SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
  SPI_InitStructure.SPI_BaudRatePrescaler = spi_prescaler[ prescaler_idx ];
  SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
  SPI_InitStructure.SPI_CRCPolynomial = 7;
  SPI_Init( spi[ id ], &SPI_InitStructure );
  SPI_Cmd( spi[ id ], ENABLE );

  return ( SPI_GET_BASE_CLK( id ) / ( ( ( u16 )2 << ( prescaler_idx ) ) ) );
}

spi_data_type platform_spi_send_recv( unsigned id, spi_data_type data )
{
  SPI_I2S_SendData( spi[ id ], data );

  while ( SPI_I2S_GetFlagStatus( spi[ id ], SPI_I2S_FLAG_RXNE ) == RESET );

  return SPI_I2S_ReceiveData( spi[ id ] );
}

void platform_spi_select( unsigned id, int is_select )
{
  // This platform doesn't have a hardware SS pin, so there's nothing to do here
  id = id;
  is_select = is_select;
}


// ****************************************************************************
// Timers

u8 stm32_timer_int_periodic_flag[ NUM_PHYS_TIMER ];

// We leave out TIM6/TIM for now, as they are dedicated
const TIM_TypeDef * const timer[] = {
  TIM1,   // ID: 0
  TIM2,   // ID: 1
  TIM3,   // ID: 2
  TIM4,   // ID: 3
  TIM5,   // ID: 4
  TIM8,   // ID: 5
  TIM9,   // ID: 6
  TIM10,  // ID: 7
  TIM11,  // ID: 8
  TIM12,  // ID: 9
  TIM13,  // ID: 10
  TIM14   // ID: 11
};
#define TIM_GET_BASE_CLK( id ) ( HCLK  )
#define TIM_STARTUP_CLOCK       50000

u32 platform_timer_set_clock( unsigned id, u32 clock );

volatile unsigned int systick=0;

void SysTick_Handler( void )
{
  // Handle virtual timers
  cmn_virtual_timer_cb();

  // Handle system timer call
  cmn_systimer_periodic();

  //Allow main loop to run by disabling Sleeponexit bit
  NVIC_SystemLPConfig(NVIC_LP_SLEEPONEXIT, DISABLE);

  systick++;
}

/**
 * Delay funtion.
 * Actual resolution depends on Systick resolution.
 * @param ms number of milliseconds to sleep.
 */
void delay_ms(unsigned int ms)
{
  ms *= SYSTICKMS;
  ms += systick;
  while (ms < systick) {                              /* In case of overflow */
    NVIC_SystemLPConfig(NVIC_LP_SLEEPONEXIT, ENABLE); //Enable SleepOnExit mode for interrupt
    __WFI(); //Go to sleep (WaitForInterrupt)
  }
  while (ms > systick) {
    NVIC_SystemLPConfig(NVIC_LP_SLEEPONEXIT, ENABLE); //Enable SleepOnExit mode for interrupt
    __WFI(); //Go to sleep (WaitForInterrupt)
  }
}

static void timers_init()
{
  unsigned i;

  // Enable PHB2 Clocks
  RCC_APB2PeriphClockCmd( RCC_APB2Periph_TIM1, ENABLE );
  RCC_APB2PeriphClockCmd( RCC_APB2Periph_TIM8, ENABLE );
  RCC_APB2PeriphClockCmd( RCC_APB2Periph_TIM9, ENABLE );
  RCC_APB2PeriphClockCmd( RCC_APB2Periph_TIM10, ENABLE );
  RCC_APB2PeriphClockCmd( RCC_APB2Periph_TIM11, ENABLE );

  // Enable PHB1 Clocks
  RCC_APB1PeriphClockCmd( RCC_APB1Periph_TIM2, ENABLE );
  RCC_APB1PeriphClockCmd( RCC_APB1Periph_TIM3, ENABLE );
  RCC_APB1PeriphClockCmd( RCC_APB1Periph_TIM4, ENABLE );
  RCC_APB1PeriphClockCmd( RCC_APB1Periph_TIM5, ENABLE );
  RCC_APB1PeriphClockCmd( RCC_APB1Periph_TIM12, ENABLE );
  RCC_APB1PeriphClockCmd( RCC_APB1Periph_TIM13, ENABLE );
  RCC_APB1PeriphClockCmd( RCC_APB1Periph_TIM14, ENABLE );

  // Configure timers
  for( i = 0; i < NUM_TIMER; i ++ )
    platform_timer_set_clock( i, TIM_STARTUP_CLOCK );
}

static u32 platform_timer_get_clock( unsigned id )
{
  TIM_TypeDef* ptimer = (TIM_TypeDef*)timer[ id ];
  return TIM_GET_BASE_CLK( id ) / ( TIM_GetPrescaler( ptimer ) + 1 );
}

u32 platform_timer_set_clock( unsigned id, u32 clock )
{
  TIM_TimeBaseInitTypeDef timer_base_struct;
  TIM_TypeDef *ptimer = (TIM_TypeDef*)timer[ id ];

  TIM_DeInit(ptimer);

  u32 pre = ( TIM_GET_BASE_CLK( id ) / clock ) - 1;

  if( pre > 65535 ) // Limit prescaler to 16-bits
    pre = 65535;

  timer_base_struct.TIM_Period = 0xFFFF;
  timer_base_struct.TIM_Prescaler = ( u16 )pre;
  timer_base_struct.TIM_ClockDivision = TIM_CKD_DIV1;
  timer_base_struct.TIM_CounterMode = TIM_CounterMode_Up;
  timer_base_struct.TIM_RepetitionCounter = 0x0000;
  TIM_TimeBaseInit( (TIM_TypeDef*)timer[ id ], &timer_base_struct );
  TIM_Cmd( ptimer, ENABLE );

  return  platform_timer_get_clock( id );
}

void platform_s_timer_delay( unsigned id, timer_data_type delay_us )
{
  TIM_TypeDef *ptimer = (TIM_TypeDef*)timer[ id ];
  volatile unsigned dummy;
  timer_data_type final;

  final = ( ( u64 )delay_us * platform_timer_get_clock( id ) ) / 1000000;
  TIM_SetCounter( ptimer, 0 );
  for( dummy = 0; dummy < 200; dummy ++ );
  while( TIM_GetCounter( ptimer ) < final );
}

timer_data_type platform_s_timer_op( unsigned id, int op, timer_data_type data )
{
  u32 res = 0;
  TIM_TypeDef *ptimer = (TIM_TypeDef*)timer[ id ];
  volatile unsigned dummy;

  data = data;
  switch( op )
  {
    case PLATFORM_TIMER_OP_START:
      TIM_SetCounter( ptimer, 0 );
      for( dummy = 0; dummy < 200; dummy ++ );
      break;

    case PLATFORM_TIMER_OP_READ:
      res = TIM_GetCounter( ptimer );
      break;

    case PLATFORM_TIMER_OP_SET_CLOCK:
      res = platform_timer_set_clock( id, data );
      break;

    case PLATFORM_TIMER_OP_GET_CLOCK:
      res = platform_timer_get_clock( id );
      break;

    case PLATFORM_TIMER_OP_GET_MAX_CNT:
      res = 0xFFFF;
      break;

  }
  return res;
}

int platform_s_timer_set_match_int( unsigned id, timer_data_type period_us, int type )
{
  TIM_TypeDef* base = ( TIM_TypeDef* )timer[ id ];
  u32 period, prescaler, freq;
  timer_data_type final;
  TIM_OCInitTypeDef  TIM_OCInitStructure;

  if( period_us == 0 )
  {
    TIM_ITConfig( base, TIM_IT_CC1, DISABLE );
    base->CR1 = 0; // Why are we doing this?
    base->CR2 = 0;
    return PLATFORM_TIMER_INT_OK;
  }

  period = ( ( u64 )TIM_GET_BASE_CLK( id ) * period_us ) / 1000000;

  prescaler = ( period / 0x10000 ) + 1;
  period /= prescaler;

  platform_timer_set_clock( id, TIM_GET_BASE_CLK( id  ) / prescaler );
  freq = platform_timer_get_clock( id );
  final = ( ( u64 )period_us * freq ) / 1000000;

  if( final == 0 )
    return PLATFORM_TIMER_INT_TOO_SHORT;
  if( final > 0xFFFF )
    return PLATFORM_TIMER_INT_TOO_LONG;

  TIM_Cmd( base, DISABLE );

  TIM_OCStructInit( &TIM_OCInitStructure );
  TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_Timing;
  TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
  TIM_OCInitStructure.TIM_Pulse = final;
  TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
  TIM_OC1Init( base, &TIM_OCInitStructure );

  // Patch timer configuration to reload when period is reached
  TIM_SetAutoreload( base, final );

  TIM_OC1PreloadConfig( base, TIM_OCPreload_Enable );

  stm32_timer_int_periodic_flag[ id ] = type;

  TIM_SetCounter( base, 0 );
  TIM_Cmd( base, ENABLE );
  //TIM_ITConfig( base, TIM_IT_CC1, ENABLE );

  return PLATFORM_TIMER_INT_OK;
}

u64 platform_timer_sys_raw_read()
{
  return SysTick->LOAD - SysTick->VAL;
}

void platform_timer_sys_disable_int()
{
  SysTick->CTRL &= ~( 1 << SysTick_CTRL_TICKINT_Pos );
}

void platform_timer_sys_enable_int()
{
  SysTick->CTRL |= 1 << SysTick_CTRL_TICKINT_Pos;
}

timer_data_type platform_timer_read_sys()
{
  return cmn_systimer_get();
}

// ****************************************************************************
// CAN
// TODO: Many things
#if (NUM_CAN > 0)

#define CANx                       CAN1
#define CAN_CLK                    RCC_APB1Periph_CAN1
#define CAN_RX_PIN                 GPIO_Pin_0
#define CAN_TX_PIN                 GPIO_Pin_1
#define CAN_GPIO_PORT              GPIOD
#define CAN_GPIO_CLK               RCC_AHB1Periph_GPIOD
#define CAN_AF_PORT                GPIO_AF_CAN1
#define CAN_RX_SOURCE              GPIO_PinSource0
#define CAN_TX_SOURCE              GPIO_PinSource1

void cans_init( void )
{
  // CAN Periph clock enable
  RCC_APB1PeriphClockCmd(CAN_CLK, ENABLE);
}

/*       BS1 BS2 SJW Pre
1M:      5   3   1   4
500k:    7   4   1   6
250k:    9   8   1   8
125k:    9   8   1   16
100k:    9   8   1   20 */

#define CAN_BAUD_COUNT 5
static const u8 can_baud_bs1[]    = { CAN_BS1_9tq, CAN_BS1_9tq, CAN_BS1_9tq, CAN_BS1_7tq, CAN_BS1_5tq };
static const u8 can_baud_bs2[]    = { CAN_BS1_8tq, CAN_BS1_8tq, CAN_BS1_8tq, CAN_BS1_4tq, CAN_BS1_3tq };
static const u8 can_baud_sjw[]    = { CAN_SJW_1tq, CAN_SJW_1tq, CAN_SJW_1tq, CAN_SJW_1tq, CAN_SJW_1tq };
static const u8 can_baud_pre[]    = { 20, 16, 8, 6, 4 };
static const u32 can_baud_rate[]  = { 100000, 125000, 250000, 500000, 1000000 };

u32 platform_can_setup( unsigned id, u32 clock )
{
  CAN_InitTypeDef        CAN_InitStructure;
  CAN_FilterInitTypeDef  CAN_FilterInitStructure;
  GPIO_InitTypeDef GPIO_InitStructure;
  int cbaudidx = -1;

  /* Connect CAN pins to AF9 */
  GPIO_PinAFConfig(CAN_GPIO_PORT, CAN_RX_SOURCE, CAN_AF_PORT);
  GPIO_PinAFConfig(CAN_GPIO_PORT, CAN_TX_SOURCE, CAN_AF_PORT);

  // Configure IO Pins -- This is for STM32F103RE
  GPIO_InitStructure.GPIO_Pin   = CAN_RX_PIN | CAN_TX_PIN;
  GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
  GPIO_Init( CAN_GPIO_PORT, &GPIO_InitStructure );

  // Select baud rate up to requested rate, except for below min, where min is selected
  if ( clock >= can_baud_rate[ CAN_BAUD_COUNT - 1 ] ) // round down to peak rate if >= peak rate
    cbaudidx = CAN_BAUD_COUNT - 1;
  else
  {
    for( cbaudidx = 0; cbaudidx < CAN_BAUD_COUNT - 1; cbaudidx ++ )
    {
      if( clock < can_baud_rate[ cbaudidx + 1 ] ) // take current idx if next is too large
        break;
    }
  }

  /* Deinitialize CAN Peripheral */
  CAN_DeInit( CANx );
  CAN_StructInit( &CAN_InitStructure );

  /* CAN cell init */
  CAN_InitStructure.CAN_TTCM=DISABLE;
  CAN_InitStructure.CAN_ABOM=DISABLE;
  CAN_InitStructure.CAN_AWUM=DISABLE;
  CAN_InitStructure.CAN_NART=DISABLE;
  CAN_InitStructure.CAN_RFLM=DISABLE;
  CAN_InitStructure.CAN_TXFP=DISABLE;
  CAN_InitStructure.CAN_Mode=CAN_Mode_Normal;
  CAN_InitStructure.CAN_SJW=can_baud_sjw[ cbaudidx ];
  CAN_InitStructure.CAN_BS1=can_baud_bs1[ cbaudidx ];
  CAN_InitStructure.CAN_BS2=can_baud_bs2[ cbaudidx ];
  CAN_InitStructure.CAN_Prescaler=can_baud_pre[ cbaudidx ];
  CAN_Init( CANx, &CAN_InitStructure );

  /* CAN filter init */
  CAN_FilterInitStructure.CAN_FilterNumber=0;
  CAN_FilterInitStructure.CAN_FilterMode=CAN_FilterMode_IdMask;
  CAN_FilterInitStructure.CAN_FilterScale=CAN_FilterScale_32bit;
  CAN_FilterInitStructure.CAN_FilterIdHigh=0x0000;
  CAN_FilterInitStructure.CAN_FilterIdLow=0x0000;
  CAN_FilterInitStructure.CAN_FilterMaskIdHigh=0x0000;
  CAN_FilterInitStructure.CAN_FilterMaskIdLow=0x0000;
  CAN_FilterInitStructure.CAN_FilterFIFOAssignment=CAN_FIFO0;
  CAN_FilterInitStructure.CAN_FilterActivation=ENABLE;
  CAN_FilterInit(&CAN_FilterInitStructure);

  return can_baud_rate[ cbaudidx ];
}

u32 platform_can_op( unsigned id, int op, u32 data )
{
  u32 res = 0;
  TIM_TypeDef *ptimer = (TIM_TypeDef*)timer[ id ];
  //volatile unsigned dummy;

  data = data;
  switch( op )
  {
    case PLATFORM_TIMER_OP_READ:
      res = TIM_GetCounter( ptimer );
      break;
  }
  return res;
}

void platform_can_send( unsigned id, u32 canid, u8 idtype, u8 len, const u8 *data )
{
  CanTxMsg TxMessage;
  const char *s = ( char * )data;
  char *d;

  switch( idtype )
  {
    case ELUA_CAN_ID_STD:
      TxMessage.IDE = CAN_ID_STD;
      TxMessage.StdId = canid;
      break;
    case ELUA_CAN_ID_EXT:
      TxMessage.IDE = CAN_ID_EXT;
      TxMessage.ExtId = canid;
      break;
  }

  TxMessage.RTR=CAN_RTR_DATA;
  TxMessage.DLC=len;

  d = ( char * )TxMessage.Data;
  DUFF_DEVICE_8( len,  *d++ = *s++ );

  CAN_Transmit( CAN1, &TxMessage );
}

void USB_LP_CAN_RX0_IRQHandler(void)
{
  /*
  CanRxMsg RxMessage;

  RxMessage.StdId=0x00;
  RxMessage.ExtId=0x00;
  RxMessage.IDE=0;
  RxMessage.DLC=0;
  RxMessage.FMI=0;
  RxMessage.Data[0]=0x00;
  RxMessage.Data[1]=0x00;

  CAN_Receive(CANx, CAN_FIFO0, &RxMessage);

  if((RxMessage.ExtId==0x1234) && (RxMessage.IDE==CAN_ID_EXT)
     && (RxMessage.DLC==2) && ((RxMessage.Data[1]|RxMessage.Data[0]<<8)==0xDECA))
  {
    ret = 1;
  }
  else
  {
    ret = 0;
  }*/
}

int platform_can_recv( unsigned id, u32 *canid, u8 *idtype, u8 *len, u8 *data )
{
  CanRxMsg RxMessage;
  const char *s;
  char *d;

  if( CAN_MessagePending( CAN1, CAN_FIFO0 ) > 0 )
  {
    CAN_Receive(CAN1, CAN_FIFO0, &RxMessage);

    if( RxMessage.IDE == CAN_ID_STD )
    {
      *canid = ( u32 )RxMessage.StdId;
      *idtype = ELUA_CAN_ID_STD;
    }
    else
    {
      *canid = ( u32 )RxMessage.ExtId;
      *idtype = ELUA_CAN_ID_EXT;
    }

    *len = RxMessage.DLC;

    s = ( const char * )RxMessage.Data;
    d = ( char* )data;
    DUFF_DEVICE_8( RxMessage.DLC,  *d++ = *s++ );
    return PLATFORM_OK;
  }
  else
    return PLATFORM_UNDERFLOW;
}
#endif


#ifdef ENABLE_ENC
// ****************************************************************************
// Quadrature Encoder Support (uses timers)
// No pin configuration, many of the timers should work with default config if
// pins aren't reconfigured for another peripheral

void stm32_enc_init( unsigned id )
{
  TIM_TypeDef *ptimer = timer[ id ];

  TIM_Cmd( ptimer, DISABLE );
  TIM_DeInit( ptimer );
  TIM_SetCounter( ptimer, 0 );
  TIM_EncoderInterfaceConfig( ptimer, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
  TIM_Cmd( ptimer, ENABLE );
}

void stm32_enc_set_counter( unsigned id, unsigned count )
{
  TIM_TypeDef *ptimer = timer[ id ];

  TIM_SetCounter( ptimer, ( u16 )count );
}
#endif

// ****************************************************************************
// PWMs

#if defined( ELUA_BOARD_STM32F4ALT )
// Using Timer 8 (5 in eLua)
#define PWM_TIMER_ID    5
#define PWM_TIMER_NAME  TIM8
#define PWM_TIMER_AF    GPIO_AF_TIM8
#define PWM_GPIO_PORT   GPIOC
static const u8 pwm_gpio_pins_source[] = { GPIO_PinSource6, GPIO_PinSource7, GPIO_PinSource8, GPIO_PinSource9 };
#else
// Using Timer 4 (3 in eLua)
#define PWM_TIMER_ID    3
#define PWM_TIMER_NAME  TIM4
#define PWM_TIMER_AF    GPIO_AF_TIM4
#define PWM_GPIO_PORT   GPIOD
static const u8 pwm_gpio_pins_source[] = { GPIO_PinSource12, GPIO_PinSource13, GPIO_PinSource14, GPIO_PinSource15 };
#endif

static void pwms_init()
{
  //RCC_APB2PeriphClockCmd( RCC_APB1Periph_TIM4, ENABLE );
  //
}

// Return the PWM clock
// NOTE: Can't find a function to query for the period set for the timer,
// therefore using the struct.
// This may require adjustment if driver libraries are updated.
u32 platform_pwm_get_clock( unsigned id )
{
  return ( ( TIM_GET_BASE_CLK( PWM_TIMER_ID ) / ( TIM_GetPrescaler( PWM_TIMER_NAME ) + 1 ) ) / ( PWM_TIMER_NAME->ARR + 1 ) );
}

// Set the PWM clock
u32 platform_pwm_set_clock( unsigned id, u32 clock )
{
  TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
  TIM_TypeDef* ptimer = PWM_TIMER_NAME;
  unsigned period, prescaler;

  /* Time base configuration */
  period = TIM_GET_BASE_CLK( PWM_TIMER_ID ) / clock;

  prescaler = (period / 0x10000) + 1;
  period /= prescaler;

  TIM_TimeBaseStructure.TIM_Period = period - 1;
  TIM_TimeBaseStructure.TIM_Prescaler = prescaler - 1;
  TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseStructure.TIM_RepetitionCounter = 0x0000;
  TIM_TimeBaseInit( ptimer, &TIM_TimeBaseStructure );

  return platform_pwm_get_clock( id );
}

u32 platform_pwm_setup( unsigned id, u32 frequency, unsigned duty )
{
  TIM_OCInitTypeDef  TIM_OCInitStructure;
  TIM_TypeDef* ptimer = PWM_TIMER_NAME;
  GPIO_InitTypeDef GPIO_InitStructure;
  u32 clock;

  TIM_Cmd( ptimer, DISABLE);
  TIM_SetCounter( ptimer, 0 );

  /* Configure GPIO Pin as alternate function push-pull */
  GPIO_InitStructure.GPIO_Pin = GPIO_SOURCE2PIN(pwm_gpio_pins_source[ id ]);
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_Init(PWM_GPIO_PORT, &GPIO_InitStructure);
  GPIO_PinAFConfig(PWM_GPIO_PORT, pwm_gpio_pins_source[ id ], PWM_TIMER_AF);


  clock = platform_pwm_set_clock( id, frequency );
  TIM_ARRPreloadConfig( ptimer, ENABLE );

  /* PWM Mode configuration */
  TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
  TIM_OCInitStructure.TIM_OutputState = ( PWM_TIMER_NAME->CCER & ( ( u16 )1 << 4 * id ) ) ? TIM_OutputState_Enable : TIM_OutputState_Disable;
  TIM_OCInitStructure.TIM_OutputNState = TIM_OutputNState_Disable;
  TIM_OCInitStructure.TIM_Pulse = ( u16 )( duty * ( PWM_TIMER_NAME->ARR + 1 ) / 100 );
  TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
  TIM_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Set;

  switch ( id )
  {
    case 0:
      TIM_OC1Init( ptimer, &TIM_OCInitStructure );
      TIM_OC1PreloadConfig( ptimer, TIM_OCPreload_Enable );
      break;
    case 1:
      TIM_OC2Init( ptimer, &TIM_OCInitStructure );
      TIM_OC2PreloadConfig( ptimer, TIM_OCPreload_Enable );
      break;
    case 2:
      TIM_OC3Init( ptimer, &TIM_OCInitStructure );
      TIM_OC3PreloadConfig( ptimer, TIM_OCPreload_Enable );
      break;
    case 3:
      TIM_OC4Init( ptimer, &TIM_OCInitStructure );
      TIM_OC4PreloadConfig( ptimer, TIM_OCPreload_Enable ) ;
      break;
    default:
      return 0;
  }

  TIM_CtrlPWMOutputs(ptimer, ENABLE);

  TIM_Cmd( ptimer, ENABLE );

  return clock;
}

void platform_pwm_start( unsigned id )
{
  PWM_TIMER_NAME->CCER |= ( ( u16 )1 << 4 * id );
}

void platform_pwm_stop( unsigned id )
{
  PWM_TIMER_NAME->CCER &= ~( ( u16 )1 << 4 * id );
}

// *****************************************************************************
// CPU specific functions

u32 platform_s_cpu_get_frequency()
{
  return HCLK;
}

// *****************************************************************************
// ADC specific functions and variables

#ifdef BUILD_ADC

static const u16 adc_gpio_pins[] = { GPIO_Pin_0,  GPIO_Pin_1,  GPIO_Pin_2,  GPIO_Pin_3,
                                     GPIO_Pin_4,  GPIO_Pin_5,  GPIO_Pin_6,  GPIO_Pin_7,
                                     GPIO_Pin_0,  GPIO_Pin_1,  GPIO_Pin_0,  GPIO_Pin_1,
                                     GPIO_Pin_2,  GPIO_Pin_3,  GPIO_Pin_4,  GPIO_Pin_5};

static GPIO_TypeDef * const adc_gpio_port[] = { GPIOA, GPIOA, GPIOA, GPIOA,
                                               GPIOA, GPIOA, GPIOA, GPIOA,
                                               GPIOB, GPIOB, GPIOC, GPIOC,
                                               GPIOC, GPIOC, GPIOC, GPIOC };

/* ADC EXTEN mask */
#define CR2_EXTEN_RESET           ((uint32_t)0xCFFFFFFF)

/**
  * @brief  Enables or disables the ADCx conversion through external trigger.
  * @param  ADCx: where x can be 1, 2 or 3 to select the ADC peripheral.
  * @param  ADC_ExternalTrigConvEdge: specifies the ADC external trigger edge
  *         to start  conversion.
  *          This parameter can be one of the following values:
  *            @arg ADC_ExternalTrigConvEdge_None: external trigger disabled for
  *                                                     injected conversion
  *            @arg ADC_ExternalTrigConvEdge_Rising: detection on rising edge
  *            @arg ADC_ExternalTrigConvEdge_Falling: detection on falling edge
  *            @arg ADC_ExternalTrigConvEdge_RisingFalling: detection on both rising
  *                                                               and falling edge

  * @retval None
  */
void ADC_ExternalTrigConvCmd(ADC_TypeDef* ADCx, uint32_t ADC_ExternalTrigConvEdge)
{
  uint32_t tmpreg = 0;

  /* Check the parameters */
  assert_param(IS_ADC_ALL_PERIPH(ADCx));
  assert_param(IS_ADC_EXT_TRIG_EDGE(ADC_ExternalTrigConvEdge));

  /* Get the old register value */
  tmpreg = ADCx->CR2;
  /* Clear the old external trigger edge for regular group */
  tmpreg &= CR2_EXTEN_RESET;
  /* Set the new external trigger edge for regular group */
  tmpreg |= ADC_ExternalTrigConvEdge;
  /* Store the new register value */
  ADCx->CR2 = tmpreg;
}

/**
  * @brief  Enables or disables the selected ADC software start conversion .
  * @param  ADCx: where x can be 1, 2 or 3 to select the ADC peripheral.
  * @param  NewState: new state of the selected ADC software start conversion.
  *   This parameter can be: ENABLE or DISABLE.
  * @retval None
  */
void ADC_SoftwareStartConvCmd(ADC_TypeDef* ADCx, FunctionalState NewState)
{
  /* Check the parameters */
  assert_param(IS_ADC_ALL_PERIPH(ADCx));
  assert_param(IS_FUNCTIONAL_STATE(NewState));
  if (NewState != DISABLE)
  {
    /* Enable the selected ADC conversion on external event and start the selected
       ADC conversion */
    ADCx->CR2 |= ADC_CR2_SWSTART;
  }
  else
  {
    /* Disable the selected ADC conversion on external event and stop the selected
       ADC conversion */
    ADCx->CR2 &= (~ADC_CR2_SWSTART);
  }
}

#define ADC_DMA_STREAM  DMA2_Stream0
#define ADC_DMA_CHANNEL DMA_Channel_0
#define ADC_DMA_TCIF    DMA_IT_TCIF0

#define ADC_TRIG_CFG(adn, n) ADC_ExternalTrigConvCmd( (adn), (n)==ENABLE?ADC_ExternalTrigConvEdge_Rising:ADC_ExternalTrigConvEdge_None ) //ADC_AutoInjectedConvCmd( (adn), (n) )

#define ADC1_DR_Address ((u32)ADC1_BASE + 0x4C)

static ADC_TypeDef *const adc[] = { ADC1, ADC2, ADC3 };
static const u32 adc_timer[] = { ADC_ExternalTrigConv_T1_CC1, ADC_ExternalTrigConv_T2_TRGO, ADC_ExternalTrigConv_T3_TRGO, ADC_ExternalTrigConv_T4_CC4 };

ADC_InitTypeDef adc_init_struct;
DMA_InitTypeDef dma_init_struct;

int platform_adc_check_timer_id( unsigned id, unsigned timer_id )
{
  // NOTE: We only allow timer id 1, the TIM2, at the moment, for the sake of implementation simplicity
  return ( (timer_id == 1) ||(timer_id == 2) );
}

void platform_adc_stop( unsigned id )
{
  elua_adc_ch_state *s = adc_get_ch_state( id );
  elua_adc_dev_state *d = adc_get_dev_state( 0 );

  s->op_pending = 0;
  INACTIVATE_CHANNEL( d, id );

  // If there are no more active channels, stop the sequencer
  if( d->ch_active == 0 )
  {
    // Ensure that no external triggers are firing
    ADC_TRIG_CFG( adc[ d->seq_id ], DISABLE );

    // Also ensure that DMA interrupt won't fire ( this shouldn't really be necessary )
    nvic_init_structure_adc.NVIC_IRQChannelCmd = DISABLE;
    NVIC_Init(&nvic_init_structure_adc);

    d->running = 0;
  }
}

int platform_adc_update_sequence( )
{
  GPIO_InitTypeDef GPIO_InitStructure;
  elua_adc_dev_state *d = adc_get_dev_state( 0 );

  // NOTE: this shutdown/startup stuff may or may not be absolutely necessary
  //       it is here to deal with the situation that a dma conversion has
  //       already started and should be reset.
  ADC_TRIG_CFG( adc[ d->seq_id ], DISABLE );

  // Stop in-progress adc dma transfers
  // Later de/reinitialization should flush out synchronization problems
  ADC_DMACmd( adc[ d->seq_id ], DISABLE );

  // Bring down adc, update setup, bring back up
  ADC_Cmd( adc[ d->seq_id ], DISABLE );
  ADC_DeInit();

  // prep for configuring pins as analog input with no pull
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;

  d->seq_ctr = 0;
  while( d->seq_ctr < d->seq_len )
  {
    // Map pin as analog input
    GPIO_InitStructure.GPIO_Pin = adc_gpio_pins[ d->ch_state[ d->seq_ctr ]->id ];
    GPIO_Init(adc_gpio_port[ d->ch_state[ d->seq_ctr ]->id ], &GPIO_InitStructure);

    ADC_RegularChannelConfig( adc[ d->seq_id ], d->ch_state[ d->seq_ctr ]->id, d->seq_ctr+1, ADC_SampleTime_3Cycles );
    d->seq_ctr++;
  }
  d->seq_ctr = 0;

  adc_init_struct.ADC_NbrOfConversion = d->seq_len;
  ADC_Init( adc[ d->seq_id ], &adc_init_struct );
  ADC_Cmd( adc[ d->seq_id ], ENABLE );

  // Bring down adc dma, update setup, bring back up
  DMA_Cmd( ADC_DMA_STREAM, DISABLE );
  DMA_DeInit( ADC_DMA_STREAM );
  dma_init_struct.DMA_BufferSize = d->seq_len;
  dma_init_struct.DMA_Memory0BaseAddr = (u32)d->sample_buf;
  DMA_Init( ADC_DMA_STREAM, &dma_init_struct );
  DMA_Cmd( ADC_DMA_STREAM, ENABLE );

  ADC_DMARequestAfterLastTransferCmd( ADC1, ENABLE );

  ADC_DMACmd( adc[ d->seq_id ], ENABLE );
  DMA_ITConfig( ADC_DMA_STREAM, DMA_IT_TC , ENABLE );

  if ( d->clocked == 1 && d->running == 1 )
  {
    ADC_TRIG_CFG( adc[ d->seq_id ], ENABLE );
  }

  return PLATFORM_OK;
}

void DMA2_Stream0_IRQHandler(void)
{
  elua_adc_dev_state *d = adc_get_dev_state( 0 );
  elua_adc_ch_state *s;

  DMA_ClearITPendingBit(ADC_DMA_STREAM, ADC_DMA_TCIF );

  d->seq_ctr = 0;
  while( d->seq_ctr < d->seq_len )
  {
    s = d->ch_state[ d->seq_ctr ];
    s->value_fresh = 1;

    // Fill in smoothing buffer until warmed up
    if ( s->logsmoothlen > 0 && s->smooth_ready == 0)
      adc_smooth_data( s->id );
#if defined( BUF_ENABLE_ADC )
    else if ( s->reqsamples > 1 )
    {
      buf_write( BUF_ID_ADC, s->id, ( t_buf_data* )s->value_ptr );
      s->value_fresh = 0;
    }
#endif

    // If we have the number of requested samples, stop sampling
    if ( adc_samples_available( s->id ) >= s->reqsamples && s->freerunning == 0 )
      platform_adc_stop( s->id );

    d->seq_ctr++;
  }
  d->seq_ctr = 0;

  if( d->running == 1 )
    adc_update_dev_sequence( 0 );

  if ( d->clocked == 0 && d->running == 1 )
    ADC_SoftwareStartConvCmd( adc[ d->seq_id ], ENABLE );
}

static void adcs_init()
{
  unsigned id;
  ADC_CommonInitTypeDef ADC_CommonInitStructure;
  elua_adc_dev_state *d = adc_get_dev_state( 0 );

  for( id = 0; id < NUM_ADC; id ++ )
    adc_init_ch_state( id );

  RCC_APB2PeriphClockCmd( RCC_APB2Periph_ADC1, ENABLE );

  ADC_DeInit();
  ADC_StructInit( &adc_init_struct );

  // Universal Converter Setup
  ADC_CommonInitStructure.ADC_Mode = ADC_Mode_Independent;
  ADC_CommonInitStructure.ADC_Prescaler = ADC_Prescaler_Div8;
  ADC_CommonInitStructure.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;
  ADC_CommonInitStructure.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_5Cycles;
  ADC_CommonInit(&ADC_CommonInitStructure);

  adc_init_struct.ADC_Resolution = ADC_Resolution_12b;
  adc_init_struct.ADC_ScanConvMode = ENABLE;
  adc_init_struct.ADC_ContinuousConvMode = DISABLE;
  adc_init_struct.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T3_TRGO;
  adc_init_struct.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
  adc_init_struct.ADC_DataAlign = ADC_DataAlign_Right;
  adc_init_struct.ADC_NbrOfConversion = 1;

  // Apply default config
  ADC_Init( adc[ d->seq_id ], &adc_init_struct );

  // Enable ADC
  ADC_Cmd( adc[ d->seq_id ], ENABLE );

  // Set up DMA to handle samples
  RCC_AHB1PeriphClockCmd( RCC_AHB1Periph_DMA2, ENABLE );

  DMA_DeInit( ADC_DMA_STREAM );

  DMA_StructInit(&dma_init_struct);
  dma_init_struct.DMA_Channel = ADC_DMA_CHANNEL;
  dma_init_struct.DMA_PeripheralBaseAddr = ADC1_DR_Address;
  dma_init_struct.DMA_Memory0BaseAddr = (u32)d->sample_buf;
  dma_init_struct.DMA_DIR = DMA_DIR_PeripheralToMemory;
  dma_init_struct.DMA_BufferSize = 1;
  dma_init_struct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  dma_init_struct.DMA_MemoryInc = DMA_MemoryInc_Enable;
  dma_init_struct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
  dma_init_struct.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
  dma_init_struct.DMA_Mode = DMA_Mode_Circular;
  dma_init_struct.DMA_Priority = DMA_Priority_Low;
  DMA_Init( ADC_DMA_STREAM, &dma_init_struct );

  // Clear flags
  DMA_ClearFlag(ADC_DMA_STREAM, DMA_FLAG_TEIF0 | DMA_FLAG_TCIF0 | DMA_FLAG_HTIF0);

  DMA_ClearITPendingBit(ADC_DMA_STREAM, ADC_DMA_TCIF );

  ADC_DMARequestAfterLastTransferCmd( ADC1, DISABLE );

  ADC_DMACmd(ADC1, ENABLE );

  DMA_Cmd( ADC_DMA_STREAM, ENABLE );
  DMA_ITConfig( ADC_DMA_STREAM, DMA_IT_TC , ENABLE );

  platform_adc_set_clock( 0, 0 );
}

u32 platform_adc_set_clock( unsigned id, u32 frequency )
{
  TIM_TimeBaseInitTypeDef timer_base_struct;
  elua_adc_dev_state *d = adc_get_dev_state( 0 );

  unsigned period, prescaler;

  // Make sure sequencer is disabled before making changes
  ADC_TRIG_CFG( adc[ d->seq_id ], DISABLE );

  if ( frequency > 0 )
  {
    d->clocked = 1;
    // Attach timer to converter
    adc_init_struct.ADC_ExternalTrigConv = adc_timer[ d->timer_id ];
    adc_init_struct.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_Rising;

    period = TIM_GET_BASE_CLK( id ) / frequency;

    prescaler = (period / 0x10000) + 1;
    period /= prescaler;

    timer_base_struct.TIM_Period = period - 1;
    timer_base_struct.TIM_Prescaler = prescaler - 1;
    timer_base_struct.TIM_ClockDivision = TIM_CKD_DIV1;
    timer_base_struct.TIM_CounterMode = TIM_CounterMode_Down;
    TIM_TimeBaseInit( (TIM_TypeDef*)timer[ d->timer_id ], &timer_base_struct );

    frequency = ( TIM_GET_BASE_CLK( id ) / ( TIM_GetPrescaler( (TIM_TypeDef*)timer[ d->timer_id ] ) + 1 ) ) / period;

    // Set up output compare for timer
    TIM_SelectOutputTrigger((TIM_TypeDef*)timer[ d->timer_id ], TIM_TRGOSource_Update);
  }
  else
  {
    d->clocked = 0;

    // Switch to Software-only Trigger
    adc_init_struct.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
  }

  // Apply config
  ADC_Init( adc[ d->seq_id ], &adc_init_struct );

  return frequency;
}

int platform_adc_start_sequence( )
{
  elua_adc_dev_state *d = adc_get_dev_state( 0 );

  // Only force update and initiate if we weren't already running
  // changes will get picked up during next interrupt cycle
  if ( d->running != 1 )
  {
    adc_update_dev_sequence( 0 );

    d->running = 1;

    DMA_ClearITPendingBit( ADC_DMA_STREAM, ADC_DMA_TCIF );

    nvic_init_structure_adc.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init_structure_adc);

    if( d->clocked == 1 )
      ADC_TRIG_CFG( adc[ d->seq_id ], ENABLE );
    else
      ADC_SoftwareStartConvCmd( adc[ d->seq_id ], ENABLE );
  }

  return PLATFORM_OK;
}

#endif // ifdef BUILD_ADC

// ****************************************************************************
// Platform specific modules go here

#ifdef ENABLE_ENC

#define MIN_OPT_LEVEL 2
#include "lrodefs.h"
extern const LUA_REG_TYPE enc_map[];

const LUA_REG_TYPE platform_map[] =
{
#if LUA_OPTIMIZE_MEMORY > 0
  { LSTRKEY( "enc" ), LROVAL( enc_map ) },
#endif
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_platform( lua_State *L )
{
#if LUA_OPTIMIZE_MEMORY > 0
  return 0;
#else // #if LUA_OPTIMIZE_MEMORY > 0
  luaL_register( L, PS_LIB_TABLE_NAME, platform_map );

  // Setup the new tables inside platform table
  lua_newtable( L );
  luaL_register( L, NULL, enc_map );
  lua_setfield( L, -2, "enc" );

  return 1;
#endif // #if LUA_OPTIMIZE_MEMORY > 0
}

#else // #ifdef ENABLE_ENC

LUALIB_API int luaopen_platform( lua_State *L )
{
  return 0;
}

#endif // #ifdef ENABLE_ENC

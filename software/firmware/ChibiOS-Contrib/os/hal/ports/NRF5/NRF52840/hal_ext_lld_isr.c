/*
    Copyright (C) 2015 Stephen Caudle

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/**
 * @file    NRF52840/ext_lld_isr.h
 * @brief   NRF52840 EXT subsystem low level driver ISR code.
 *
 * @addtogroup EXT
 * @{
 */

#include "hal.h"

#if HAL_USE_EXT || defined(__DOXYGEN__)

#include "hal_ext_lld.h"
#include "hal_ext_lld_isr.h"

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local variables.                                                   */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/*===========================================================================*/
/* Driver interrupt handlers.                                                */
/*===========================================================================*/

/**
 * @brief   EXTI[0]...EXTI[1] interrupt handler.
 *
 * @isr
 */
OSAL_IRQ_HANDLER(Vector58) {

  OSAL_IRQ_PROLOGUE();

  if (NRF_GPIOTE->INTENSET & 0x01 && NRF_GPIOTE->EVENTS_IN[0])
  {
    NRF_GPIOTE->EVENTS_IN[0] = 0;
    EXTD1.config->channels[0].cb(&EXTD1, 0);
  }
  if (NRF_GPIOTE->INTENSET & 0x02 && NRF_GPIOTE->EVENTS_IN[1])
  {
    NRF_GPIOTE->EVENTS_IN[1] = 0;
    EXTD1.config->channels[1].cb(&EXTD1, 1);
  }
  if (NRF_GPIOTE->INTENSET & 0x04 && NRF_GPIOTE->EVENTS_IN[2])
  {
    NRF_GPIOTE->EVENTS_IN[2] = 0;
    EXTD1.config->channels[2].cb(&EXTD1, 2);
  }
  if (NRF_GPIOTE->INTENSET & 0x08 && NRF_GPIOTE->EVENTS_IN[3])
  {
    NRF_GPIOTE->EVENTS_IN[3] = 0;
    EXTD1.config->channels[3].cb(&EXTD1, 3);
  }
  if (NRF_GPIOTE->INTENSET & 0x10 && NRF_GPIOTE->EVENTS_IN[4])
  {
    NRF_GPIOTE->EVENTS_IN[4] = 0;
    EXTD1.config->channels[4].cb(&EXTD1, 4);
  }
  if (NRF_GPIOTE->INTENSET & 0x20 && NRF_GPIOTE->EVENTS_IN[5])
  {
    NRF_GPIOTE->EVENTS_IN[5] = 0;
    EXTD1.config->channels[5].cb(&EXTD1, 5);
  }
  if (NRF_GPIOTE->INTENSET & 0x40 && NRF_GPIOTE->EVENTS_IN[6])
  {
    NRF_GPIOTE->EVENTS_IN[6] = 0;
    EXTD1.config->channels[6].cb(&EXTD1, 6);
  }
  if (NRF_GPIOTE->INTENSET & 0x80 && NRF_GPIOTE->EVENTS_IN[7])
  {
    NRF_GPIOTE->EVENTS_IN[7] = 0;
    EXTD1.config->channels[7].cb(&EXTD1, 7);
  }

  OSAL_IRQ_EPILOGUE();
}

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Enables EXTI IRQ sources.
 *
 * @notapi
 */
void ext_lld_exti_irq_enable(void) {

  nvicEnableVector(GPIOTE_IRQn, NRF5_EXT_GPIOTE_IRQ_PRIORITY);
}

/**
 * @brief   Disables EXTI IRQ sources.
 *
 * @notapi
 */
void ext_lld_exti_irq_disable(void) {

  nvicDisableVector(GPIOTE_IRQn);
}

#endif /* HAL_USE_EXT */

/** @} */

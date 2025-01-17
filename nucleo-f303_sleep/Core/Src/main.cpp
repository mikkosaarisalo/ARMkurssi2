/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  *
  * STM32F303-prosessorin virransäästötilojen demo.
  * F303RE osaa 3 virransäästötilaa:
  * - Sleep Mode ( I/O-pinnien tilat säilyvät)
  * - Stop Mode ( I/O-pinnien tilat säilyvät, HSI ja HSE-kellosignaalit pysähtyvät, muisti ja rekisterit säilyvät)
  * - Standby Mode
  * Tilojen virrankulutus alenee progressiivisesti samalla kuin erilaisia resursseja suljetaan
  * Standby-tila ei enää ylläpidä muistin eikä prosessorin rekisterien tilaa. Ainoastaan
  * reaaliaikakellon tilataltion RAM-muisti säilyy
  *
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "rtc.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>

#include "menu.h"
#include "stm32f303xe.h"

RTC_HandleTypeDef hrtc;

UART_HandleTypeDef huart2;

void SystemClock_Config(void);

// extern "C" kertoo kääntäjälle, että sen rajaamassa lohkossa esitellyt jutut
// on käännetty C-kääntäjällä. Tässä meillä on C++ -kääntäjä, jonka
// funktioiden kutsukonventio on erilainen kuin perus-C kielessä.
// Tällä saadaan kääntäjä kutsumaan C-kielisiä binäärejä C-kielen säännöillä
extern "C" {
#include  <sys/unistd.h> // STDOUT_FILENO, STDERR_FILENO

int _read(int fd, char *ptr, int len) {
	if (fd == STDIN_FILENO ) {
		HAL_UART_Receive(&huart2, (uint8_t *) ptr, 1, HAL_MAX_DELAY);
	    HAL_UART_Transmit(&huart2, (uint8_t *) ptr, 1, HAL_MAX_DELAY);
	}
	return 1;
}

int _write(int fd, char* ptr, int len) {
  HAL_StatusTypeDef hstatus;

  if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
    hstatus = HAL_UART_Transmit(&huart2, (uint8_t *) ptr, len, HAL_MAX_DELAY);
    if (hstatus == HAL_OK)
      return len;
    else
      return EIO;
  }
  errno = EBADF;
  return -1;
}

} // extern "C"

using namespace CLIMenu;

// Kekomuistista varattu tavallinen jonomuuttuja ( samankokoinen kuin backup-muisti )
// tämän sisältöä voidaan vertailla backup-muistin tilaan eri virransäästöjuttujen jälkeen
uint8_t heapBlock[RTC_BKP_NUMBER];

menu mainMenu("Sleep demo menu----------------------");
menu wakeupMenu("Select wakeup source signal--------");
menu backupMenu("Select backup memory operation-----");

// Valikot ja valintakäsittelijät
// Sovelluksen toimintaa ohjataan valikoilla, ja kaikki varsinainen toiminta
// tapahtuu valintakäsittelijöissä

void sleepHandler( char _selector );
void stopHandler( char _selector );
void standbyHandler( char _selector );
void backupHandler( char _selector );
void wakeupSourceHandler( char _selector );
void listBackupMemory( char _selector );
void setBackupMemory( char _selector );

// Päävalikko
#define NUM_MAINMENUITEMS 4
menuItem mainMenuItems[NUM_MAINMENUITEMS] =
{	{'1', "Enter sleep mode", sleepHandler},
	{'2', "Enter stop mode", stopHandler},
	{'3', "Enter Standby mode", standbyHandler},
	{'4', "Access Backup Registers", backupHandler}
};

// Herätyslähteidsen valintavalikko
#define NUM_WKUPMENUITEMS 4
menuItem wkupMenuItems[NUM_WKUPMENUITEMS] =
{	{'1', "Wakeup with EXTI interrupt by blue button", wakeupSourceHandler},
	{'2', "Wakeup with EXTI interrupt by PA0 input", wakeupSourceHandler},
	{'3', "Wakeup by RTC", wakeupSourceHandler},
	{'4', "Disable all wakeup sources. Restart by reset only", wakeupSourceHandler}
};

// Backup-muistin käsittelyvalikko
#define NUM_BKUPMENUITEMS 8
menuItem bkupMenuItems[NUM_BKUPMENUITEMS] =
{	{'1', "List backup memory contents", listBackupMemory},
	{'2', "Clear backup memory to all 0", setBackupMemory},
	{'3', "Fill backup memory with all 0xff", setBackupMemory},
	{'4', "Fill backup memory with successive bytes", setBackupMemory},
	{'5', "List heap memory block contents", listBackupMemory},
	{'6', "Clear heap memory block to all 0", setBackupMemory},
	{'7', "Fill heap memory block with all 0xff", setBackupMemory},
	{'8', "Fill heap memory block with successive bytes", setBackupMemory}
};

// Tällä funktiolla kielletään keskeytykset kaikista demossa käytetyistä herätelähteistä
// Kukin heräte aktivoidaan erikseen aina kun siirrytään virransäästötilaan ja herätystapa valitaan.
void disableAllWakeupSources() {
	HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
	HAL_NVIC_DisableIRQ(EXTI0_IRQn);
	HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
	HAL_NVIC_DisableIRQ(RTC_WKUP_IRQn);
	// nollataan wakeup-flag joka RTC on asettanut.
	// Tämä pitää nollata itse
	__HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
}

// Sleep Mode: CPU core pysähtyy. Muisti ja rekisterit säilyvät
// Osa oheislaitteista säilyy aktiivisina ja ne voivat herättää systeemin
// Low Power regulator: oheislaitteet toimivat alennetulla kellotaajuudella
void sleepHandler( char _selector ) {
	wakeupMenu.run( false );
	printf("Entering Sleep mode now, Good Night!\r\n");
	HAL_Delay(5);
	HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
	HAL_SuspendTick();
	HAL_PWR_EnterSLEEPMode( PWR_LOWPOWERREGULATOR_ON, PWR_SLEEPENTRY_WFI );
	disableAllWakeupSources();
	HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
}

// Stop Mode: CPU core pysähtyy. Muisti ja rekisterit säilyvät
// Useimmat oheislaitteet pysähtyvät
// Reaaliaikakello on mahdollista pysäyttää virran säästämiseksi
void stopHandler( char _selector ) {
	wakeupMenu.run( false );
	printf("Entering Stop mode now, Good Night!\r\n");
	HAL_Delay(5);
	HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
	HAL_SuspendTick();
	HAL_PWR_EnterSTOPMode( PWR_LOWPOWERREGULATOR_ON, PWR_SLEEPENTRY_WFI );
	SystemClock_Config();
	disableAllWakeupSources();
	HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
}

// Standby Mode: CPU Core pysähtyy, oheislaiteiden kellot pysähtyvät
// Muisti ja rekisterit tuhoutuvat.
// Reaaliaikakello pyörii.
void standbyHandler( char _selector ) {
	wakeupMenu.run( false );
	printf("Entering Standby mode now, Farewell and see you in the next life!\r\n");
	HAL_Delay(5);
	HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
	HAL_SuspendTick();
	HAL_PWR_EnterSTANDBYMode();
}

void backupHandler( char _selector ) {
	backupMenu.run( false );
}

// Backup-rekisterin käsittelijät
uint32_t readBackupRegister(uint32_t _register) {
	if ( _register >= RTC_BKP_NUMBER ) Error_Handler();
    return HAL_RTCEx_BKUPRead(&hrtc, _register);
}

void writeBackupRegister(uint32_t _register, uint32_t _data) {
	if ( _register >= RTC_BKP_NUMBER ) Error_Handler();
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, _register, _data);
    HAL_PWR_DisableBkUpAccess();
}

// Backup-muistin manipulointifunktiot
void listBackupMemory( char _selector ) {
	if ( _selector == '1' ) {
		printf("\r\nBackup: ");
		for ( uint8_t i = 0; i < RTC_BKP_NUMBER; i++ ) {
			printf( "%2x ", readBackupRegister( i ) );
		}
	}
	else {
		printf("\r\nHeap: ");
		for ( uint8_t i = 0; i < RTC_BKP_NUMBER; i++ ) {
			printf( "%2x ", heapBlock[i] );
		}
	}
	printf("\r\n");
}

void setBackupMemory( char _selector ) {
	for ( uint8_t i = 0;  i < RTC_BKP_NUMBER; i++ ) {
		if ( _selector == '2' ) writeBackupRegister( i, 0 );
		else if ( _selector == '3' ) writeBackupRegister( i, 0xff );
		else if ( _selector == '4' ) writeBackupRegister( i, i );
		else if ( _selector == '6' ) heapBlock[i] = 0;
		else if ( _selector == '7' ) heapBlock[i] = 0xff;
		else  heapBlock[i] = i;
	}
}

// Herätyslähteen valintakäsittelijä
void wakeupSourceHandler( char _selector ) {
	switch ( _selector ) {
	case ( '1' ): {
		HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
		break;
	}
	case ( '2' ): {
		HAL_NVIC_EnableIRQ(EXTI0_IRQn);
		break;
	}
	case ( '3' ): {
		// Systeemi herää automaattisesti reaaliaikakellon keskeytyksestä.
		// Muuttuja sleepTime määrää nukkumisajan millisekunteina
#define RTC_CLOCK_FREQ 32768
#define RTC_CLOCK_DIVIDER 16
		uint32_t sleepTime = ( ( (uint32_t) 5000 ) * ( RTC_CLOCK_FREQ / RTC_CLOCK_DIVIDER ) ) / 1000;
		HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, sleepTime, RTC_WAKEUPCLOCK_RTCCLK_DIV16);
		HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);
		break;
	}
	default:
		disableAllWakeupSources();
	}
}




int main(void) {

	uint8_t command;

3	HAL_Init();

	SystemClock_Config();

	MX_GPIO_Init();
	MX_USART2_UART_Init();
	MX_RTC_Init();

	// Estetään aluksi kaikki konfiguroidut herätekeskeytykset
	// Halutut kytketään päälle myöhemmin
	disableAllWakeupSources();

	// Rakennetaan valikot
	for ( uint8_t i=0; i<NUM_MAINMENUITEMS; i++ ) {
		mainMenu.addItem( &mainMenuItems[i] );
	}

	for ( uint8_t i=0; i<NUM_WKUPMENUITEMS; i++ ) {
		wakeupMenu.addItem( &wkupMenuItems[i] );
	}

	for ( uint8_t i=0; i<NUM_BKUPMENUITEMS; i++ ) {
		backupMenu.addItem( &bkupMenuItems[i] );
	}

	// Valot päälle ja käynnistysilmoitus
	HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
	printf("\r\nARM Low Power demo\r\n\r\n");

	// Ja sitten tehdään mitä käyttäjä määrää
	mainMenu.run( true );

}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  RCC_OscInitStruct.PLL.PREDIV = RCC_PREDIV_DIV1;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2|RCC_PERIPHCLK_RTC;
  PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}


void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

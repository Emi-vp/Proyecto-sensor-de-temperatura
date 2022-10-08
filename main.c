/*
 * Autor: Christopher Chiroy
 * Coautores: Emily Ventura y Michelle Maaz
 * Fecha: 09/10/2022
 */
#include "math.h"
#include <stdio.h>
#include "driver/gpio.h"
#include "driver/timer.h"
#include "soc/gpio_struct.h"
#include "soc/timer_group_struct.h"

#include "driver/adc.h"//Libreria Modulo ADC
#include "driver/dac.h" //Libreria Modulo DAC

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
//Librerías para configurar módulo WI-FI en modo estación
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/ledc.h"

//Librería para configurar cliente HTTPS
#include "esp_http_client.h"

/*=================================================================
 * CONSTANTES
 *=================================================================*/
extern const uint8_t certificate_pem_start[] asm("_binary_certificate_pem_start");//Registro donde se indicará a donde debe ir a buscar el programa el certificado
//Se asignan las credenciales a la red que se quiere conectar
#define SSID 		 "UVG" //Nombre de la red que se conectará
#define PASS 		 "" //Contraseña que tendrá red
#define IO_USERNAME  "ven18391"//usuario
#define IO_KEY       "aio_uBjv33WbPn3YiMUO2QokE88AZ9Sd"//Credencial de API key
#define FEED_KEY_TEMPERATURA "temperatura"//Feed de temperatura

#define TIMER_BASE_CLK 80000000	// Frequencia a la que funciona el reloj interno del ESP32 => 80MHz
#define RES_PWM 4095				// 2^12 -1 //4096 -1
#define PRESCALER 80			// Factor por el que dividimos la frecuencia del reloj interno para obtener la frecuencia del timer

//Variables declaradas para pines de display, botones y potenciómetro
#define BTN_1 27 //BOTON MODO
#define BTN_2 33 //BOTON MEDICION

#define a 4
#define b 5
#define c 12
#define d 13
#define e 14
#define f 18
#define g 19
#define dp 21
#define DIG1 22
#define DIG2 23
#define DIG3 15

//Variables a utilizar en el RGB
#define LED_B 26
#define LED_G 32
#define LED_R 25
#define SERVO 2

//Variables declaradas para función de separación de valores
int UNIDADES;
int DECENAS;
int CENTENAS;


/*=================================================================
 * VARIABLES GLOBALES
 *=================================================================*/
volatile uint8_t flag_boton1 = 0;	// Variable utilizada para verificar si el timer para el anti-rebote fue activado por el BTN_1

volatile uint8_t flag_boton2 = 0;	// Variable utilizada para verificar si el timer para el anti-rebote fue activado por el BTN_2

volatile uint8_t flag_actualizar = 0;	// Variable utilizada para verificar la bandera de actualización de datos

volatile int duty_led_r = 0, inc_led_r=0;				// Variables para controlar el brillo del LED RGB
volatile int duty_led_g = 0, inc_led_g=0;				// Variables para controlar el brillo del LED RGB
volatile int duty_led_b = 0, inc_led_b=0;				// Variables para controlar el brillo del LED RGB
volatile int led_idx = 0;//INDEX
volatile int duty_servo = 110, inc_servo=0;				// Variables para controlar la posición del servo

//Arreglo para Display
uint32_t disp[10] = {
		(0 << a | 0 << b | 0 << c | 0 << d | 0 << e | 0 << f | 1 << g | 1 <<dp), // 0
		(1 << a | 0 << b | 0 << c | 1 << d | 1 << e | 1 << f | 1 << g | 1 <<dp), // 1
		(0 << a | 0 << b | 1 << c | 0 << d | 0 << e | 1 << f | 0 << g | 1 <<dp), // 2
		(0 << a | 0 << b | 0 << c | 0 << d | 1 << e | 1 << f | 0 << g | 1 <<dp), // 3
		(1 << a | 0 << b | 0 << c | 1 << d | 1 << e | 0 << f | 0 << g | 1 <<dp), // 4
		(0 << a | 1 << b | 0 << c | 0 << d | 1 << e | 0 << f | 0 << g | 1 <<dp), // 5
		(0 << a | 1 << b | 0 << c | 0 << d | 0 << e | 0 << f | 0 << g | 1 <<dp), // 6
		(0 << a | 0 << b | 0 << c | 1 << d | 1 << e | 1 << f | 1 << g | 1 <<dp), // 7
		(0 << a | 0 << b | 0 << c | 0 << d | 0 << e | 0 << f | 0 << g | 1 <<dp), // 8
		(0 << a | 0 << b | 0 << c | 1 << d | 1 << e | 0 << f | 0 << g | 1 <<dp) // 9
};

//Variables utilizadas para cambio de modos y obtención de datos del sensor de temperatura;
int modo=0;
int valor_medido;
int temperatura;
int contador=0;
/*=================================================================
 * PROTOTIPOS DE FUNCIONES
 *=================================================================*/

//Prototipos para el wifi
void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void wifi_connection();
esp_err_t client_event_post_handler(esp_http_client_event_handle_t evt);
void post_contador_temperatura(float contador);

void setup(void);
//definicion de las rutinas de conversiones  para convertir la lectura del ADC a voltaje y otro de lectura del ADC a porcentaje.
int conversion1 (int valor_medido);
float conversion2 (int valor_medido);
void funcion (int numero, int *UNIDADES, int *DECENAS, int *CENTENAS); //Se define función utilizando punteros

/*=================================================================
 * INTERRUPCIONES
 *=================================================================*/

// Interrupcción de botón
void IRAM_ATTR BTN_MODO(){
	flag_boton1 = 1;									// Indicamos que fue la interrupción de BTN_1 la que inició el timer
	gpio_set_intr_type(BTN_1, GPIO_INTR_DISABLE);		// Deshabilitamos la interrupción del botón
	timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);	// Cambiamos el valor del timer a 0
	timer_start(TIMER_GROUP_0, TIMER_0);				// Iniciamos el timer
	return;
}

void IRAM_ATTR BTN_MEDICION(){
	flag_boton2 = 1;									// Indicamos que fue la interrupción de BTN_1 la que inició el timer
	gpio_set_intr_type(BTN_2, GPIO_INTR_DISABLE);		// Deshabilitamos la interrupción del botón
	timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);	// Cambiamos el valor del timer a 0
	timer_start(TIMER_GROUP_0, TIMER_0);				// Iniciamos el timer
	return;
}

void IRAM_ATTR TMR_ISR(){
	TIMERG0.int_clr_timers.t0 = 1;				// Limpiamos bandera de interrupción
	TIMERG0.hw_timer[0].config.alarm_en = 1;	// Volvemos a habilitar la alarma

	if (flag_boton1 == 1 && gpio_get_level(BTN_1) == 0){	// Verificamos que el botón siga presionado

		if (modo ==0 ){//MODO AUTOMÁTICO - MODO 0
			modo=1; //Cambia a modo 1 (Manual)
			timer_pause(TIMER_GROUP_1, TIMER_1);				// Pausamos el temporizador
			}
		else{//MODO =1, MODO MANUAL
			modo=0;  //Cambia a modo 0 (Automático)
			timer_set_counter_value(TIMER_GROUP_1, TIMER_1, 0);	// Cambiamos el valor del timer a 0
			timer_start(TIMER_GROUP_1, TIMER_1);				// Iniciamos el timer
			}
		}
	flag_boton1 = 0;											// Limpiamos bandera de BTN_2
	gpio_set_intr_type(BTN_1, GPIO_INTR_NEGEDGE);				// Activamos la interrupción de BTN_1

	timer_pause(TIMER_GROUP_0, TIMER_0);						// Pausamos el temporizador

	//BOTON MEDICION
	if (flag_boton2 == 1 && gpio_get_level(BTN_2) == 0){	// Verificamos que el botón siga presionado
		modo=0; // Empieza encendido
		flag_actualizar = 1;// Indicamos que fue la interrupción de BTN_2 la que inició el timer
		timer_set_counter_value(TIMER_GROUP_1, TIMER_0, 0);	// Cambiamos el valor del timer a 0
		timer_start(TIMER_GROUP_1, TIMER_0);				// Iniciamos el timer
	}
	flag_boton2 = 0;										// Limpiamos bandera de BTN_2
	gpio_set_intr_type(BTN_2, GPIO_INTR_NEGEDGE);			// Activamos la interrupción de BTN_2
	return;
}

void IRAM_ATTR TMR_ISR2(){
	TIMERG1.int_clr_timers.t0 = 1;						// Limpiamos bandera de interrupción
	TIMERG1.hw_timer[0].config.alarm_en = 1;			// Volvemos a habilitar la alarma

	modo=1; 											//Empieza en modo 1 (manual) apagado
	timer_pause(TIMER_GROUP_1, TIMER_0);				// Pausamos el temporizador
    return ;
}

void IRAM_ATTR TMR_ISR3(){
	TIMERG1.int_clr_timers.t1 = 1;				// Limpiamos bandera de interrupción
	TIMERG1.hw_timer[1].config.alarm_en = 1;	// Volvemos a habilitar la alarma


	flag_actualizar = 1;		// Indicamos que fue la interrupción del timer 3 que fue activada
	modo = 0;					//Empieza en modo 0 (automático) encendido
	return;
}

void IRAM_ATTR TMR_ISR1()
{
	TIMERG0.int_clr_timers.t1 = 1;				        // Limpiamos bandera de interrupción
	TIMERG0.hw_timer[1].config.alarm_en = 1;			// Volvemos a habilitar la alarma

	// Se apagan todos los leds de los displays
	GPIO.out_w1ts = (1 << a | 1 << b | 1 << c |  1 << d | 1 << e | 1 << f | 1 << g | 1 << dp);


	if (modo ==  1){ //MODO MANUAL //EMPIEZAN APAGADOS LOS DISPLAYS

	}
	if (modo ==0 ){ //MODO AUTOMÁTICO //EMPIEZAN ECENDIDOS LOS DISPLAYS

		funcion(temperatura,&UNIDADES,&DECENAS,&CENTENAS);//Llamado de la función creada

		// Se verifica que display estaba mostrando datos, para cambiar y mostrar datos en el siguiente
		if (gpio_get_level(DIG1)){
			gpio_set_level(DIG1, 0);
			gpio_set_level(DIG2, 1);
			gpio_set_level(DIG3, 0);
			GPIO.out_w1tc = disp[DECENAS];		// Dato a mostrar en el display, con relacón a las DECENAS
			gpio_set_level(dp, 1);				// Encender punto (dp) en display
		}
		else if(gpio_get_level(DIG2)){
			gpio_set_level(DIG1, 0);
			gpio_set_level(DIG2, 0);
			gpio_set_level(DIG3, 1);
			GPIO.out_w1tc = disp[UNIDADES];			// Este display siempre muestra las UNIDADES en el display
		}

		else{
			gpio_set_level(DIG1, 1);
			gpio_set_level(DIG2, 0);
			gpio_set_level(DIG3, 0);
			GPIO.out_w1tc = disp[CENTENAS];			// Este display siempre muestra las CENTENAS en el display

			}
	}


	return ;
}
/*=================================================================
 * CÓDIGO PRINCIPAL
 *=================================================================*/
void app_main(void)
{
	nvs_flash_init();						// Inicializacion del almacenamiento no volatil (Flash externa para configuraciones del Wi-Fi)
	wifi_connection();						// Configuracion del módulo Wi-Fi
	vTaskDelay(2000 / portTICK_PERIOD_MS);	// Pausa para que el Wi-Fi termine de configurarse

	setup();//Llamado de la función creada

	while(1){

		uint8_t dac_output = 0;										  // Variable declarada para DAC
		valor_medido = adc1_get_raw(ADC1_CHANNEL_0);				  // Lectura de entrada ADC
		dac_output = 200;											  // Variable para DAC
		dac_output_voltage(DAC_CHANNEL_1, dac_output);				  // Salida de voltaje en el DAC


		//Modo Manual: Empieza apagado, servo y RGB
		if (modo ==1 ){//cambio de modo para seleccionar modo manual o automático
			post_contador_temperatura(conversion2(conversion1(valor_medido)));   //Llamado de funciones para enviar datos de temperatura
			ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 0);			     // Configuración del nuevo duty cycle
			ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);				 // Actualización de las configuraciones del canal
			ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 0);		         // Configuración del nuevo duty cycle
			ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);				 // Actualización de las configuraciones del canal
			ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, 0);		         // Configuración del nuevo duty cycle
			ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2);				 // Actualización de las configuraciones del canal

			ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_3, 0);		         // Configuración del nuevo duty cycle
			ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_3);				 // Actualización de las configuraciones del canal

		}
		//Modo Automático Empieza encendido servo, RGB y se envía conversión del sensor
		if (modo ==0 && flag_actualizar == 1){

			gpio_set_level(GPIO_NUM_2, 1);		    // Enciendo led para indicar que comienza enviar datos
			printf("Enviando datos ...\n");		    //Se imprime para indicar el envío de datos
			temperatura=conversion1(valor_medido);  //Variable para obtener datos de la conversión del sensor (potenciómetro)

			gpio_set_level(GPIO_NUM_2, 0);			// Apago led para indicar que terminó el envío de datos

			if (valor_medido <= 370 ){//Se muestra registros para color verde en RGB y movimiento de servo, a una temperatura menor de 37°C


					ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 100*RES_PWM/100);	// Configuración del nuevo duty cycle
					ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);					// Actualización de las configuraciones del canal
					ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1,0*RES_PWM/100);		// Configuración del nuevo duty cycle
					ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);					// Actualización de las configuraciones del canal
					ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, 0*RES_PWM/100);		// Configuración del nuevo duty cycle
					ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2);					// Actualización de las configuraciones del canal

					ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_3, 255);				// Configuración del nuevo duty cycle
					ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_3);					// Actualización de las configuraciones del canal

				}

			if ((valor_medido  > 370) & (temperatura< 375)){ //Se muestra registros para color amarillo en RGB y movimiento de servo, a una temperatura entre 37°C a 37.5°C

					ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 100*RES_PWM/100);	// Configuración del nuevo duty cycle
					ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);					// Actualización de las configuraciones del canal
					ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 100*RES_PWM/100);	// Configuración del nuevo duty cycle
					ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);					// Actualización de las configuraciones del canal
					ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, 0*RES_PWM/100);		// Configuración del nuevo duty cycle
					ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2);					// Actualización de las configuraciones del canal

					ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_3, 307);				// Configuración del nuevo duty cycle
					ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_3);					// Actualización de las configuraciones del canal
				}

			if (temperatura >= 375){ //Se muestra registros para color rojo en RGB y movimiento de servo, a una temperatura menor de 37.5°C

					ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 100*RES_PWM/100);	// Configuración del nuevo duty cycle
					ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);					// Actualización de las configuraciones del canal
					ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 0*RES_PWM/100);		// Configuración del nuevo duty cycle
					ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);					// Actualización de las configuraciones del canal
					ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, 0*RES_PWM/100);		// Configuración del nuevo duty cycle
					ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2);					// Actualización de las configuraciones del canal

					ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_3, 255);				// Configuración del nuevo duty cycle
					ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_3);					// Actualización de las configuraciones del canal
				}
	    flag_actualizar = 0; // Limpiamos bandera de actualizar
	}

 }
}



/*=================================================================
 * FUNCIONES
 *=================================================================*/

//Función para sacar por separado las unidades, decenas y centenas de un valor
void funcion (int numero, int *UNIDADES, int *DECENAS, int *CENTENAS){
//Ejemplo con el número 592
//Se define las condiciones para sacar por separado los valores.
    *CENTENAS= numero/100; //592/100 =5
    //592
    *DECENAS= (numero - *CENTENAS * 100)/10;
    //592 -500 = 92/10=9
    *UNIDADES= (numero - *CENTENAS * 100)-*DECENAS *10;
    //92 -90 =2

	return;
}

//Función para conversion de temperatura con potenciometro tipo entero

int conversion1 (int valor_medido){

	int conversion1 = (valor_medido * 10)/82; //Valor entre 0 y 500

	return conversion1;
}
//Función para onversion de temperatura con potenciometro tipo float
float conversion2 (int valor_medido){ //Valor entre 0 a 50.0

	float conversion2= valor_medido/10.0;

	return conversion2;
}

// Función para monitorear los eventos durante la configuración del módulo Wi-Fi
void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        printf("WiFi connecting ... \n");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        printf("WiFi connected ... \n");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        printf("WiFi lost connection ... \n");
        break;
    case IP_EVENT_STA_GOT_IP:
        printf("WiFi got IP ... \n\n");
        break;
    default:
        break;
    }
}


void setup(){
	int outputs[14] = {a, b, c, d, e, f, g, dp, LED_R, LED_G, LED_B, DIG1, DIG2, DIG3};// Se configuraron  las salidas y las entrada (botÃ³n)

	int i = 0;
	for (i = 0; i < 14; i++){
		gpio_reset_pin(outputs[i]);
		gpio_set_direction(outputs[i], GPIO_MODE_INPUT_OUTPUT);
		gpio_set_level(outputs[i], 0);
		}

	gpio_install_isr_service(ESP_INTR_FLAG_IRAM); //Registro de interrupciones GPIO

	//Registros para botones utilizados para medición y cambio de modos

	gpio_reset_pin(BTN_1); //Botón 1 (Cambio modo)
	gpio_set_direction(BTN_1, GPIO_MODE_INPUT);
	gpio_set_pull_mode(BTN_1, GPIO_PULLUP_ONLY);
	gpio_set_intr_type(BTN_1, GPIO_INTR_NEGEDGE);
	gpio_isr_handler_add(BTN_1, BTN_MODO, 0);

	gpio_reset_pin(BTN_2); //Botón 2 (Medición)
	gpio_set_direction(BTN_2, GPIO_MODE_INPUT);
	gpio_set_pull_mode(BTN_2, GPIO_PULLUP_ONLY);
	gpio_set_intr_type(BTN_2, GPIO_INTR_NEGEDGE);
	gpio_isr_handler_add(BTN_2, BTN_MEDICION, 0);

	// Configuración del timer
	const timer_config_t config = {
		.divider = PRESCALER,							// Configuración del prescaler del temporizador
		.counter_dir = TIMER_COUNT_UP,					// Configuramos el tipo de conteo del temporizador
		.counter_en = TIMER_START,						// Configuramos el temporizador para que al inicializarlo también inicia a contar
		.alarm_en = TIMER_ALARM_EN,						// Habilitamos la alarma
	//	.clk_src = TIMER_SRC_CLK_DEFAULT,				//(Si da error, cometar linea) Configuramos el reloj a utilizar para el temporizador
		.auto_reload = TIMER_AUTORELOAD_EN,				// Habilitamos el reinicio del timer cuando se active la alarma
	};

	// Configuración de alarma
	// La función recibe como parametro la cantidad de pulsos que debe de contar el temporizador para activar la alarma
	// En este caso se le estamos mandando los segundos * cantidad de pulsos en 1 segundo [segundos * (TIMER_BASE_CLK / PRESCALER)].
	// Donde TIMER_BASE_CLK y PRESCALER son constantes que definimos al inicio y al hacer la división obtenemos:
	// 	TIMER_BASE_CLK / PRESCALER = 80000000 / 80 = 1000000 <- Cantidad de pulsos en 1 segundo.

	//Botones
	timer_init(TIMER_GROUP_0, TIMER_0, &config);		// inicializacion el timer 0 del grupo 0
	timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);	// Reiniciamos valor del temporizador a 0
	timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, 0.01 * (TIMER_BASE_CLK / PRESCALER) );  // Alarma configurada cada 0.01 segundo
	timer_enable_intr(TIMER_GROUP_0,TIMER_0);// Habilitamos interrupciones
	timer_isr_handle_t G0T0_handler;// Declaramos handler para las interrupciones del timer
	printf("Interrupcion G0 T0: %d \n", timer_isr_register(TIMER_GROUP_0, TIMER_0, &TMR_ISR, NULL, ESP_INTR_FLAG_IRAM, &G0T0_handler));
	timer_pause(TIMER_GROUP_0, TIMER_0);// Pausamos el temporizador

	//Displays
	timer_init(TIMER_GROUP_0, TIMER_1, &config);		// inicializacion el timer 1 del grupo 0
	timer_set_counter_value(TIMER_GROUP_0, TIMER_1, 0);	// Reiniciamos valor del temporizador a 0
	timer_set_alarm_value(TIMER_GROUP_0, TIMER_1, 0.001 * (TIMER_BASE_CLK / PRESCALER) ); // Alarma configurada cada 0.001 segundos
	timer_isr_handle_t G0T1_handler;// Declaramos handler para las interrupciones del timer
	timer_enable_intr(TIMER_GROUP_0,TIMER_1);// Habilitamos interrupciones
	printf("Interrupcion G0 T1: %d \n", timer_isr_register(TIMER_GROUP_0, TIMER_1, &TMR_ISR1, NULL, ESP_INTR_FLAG_IRAM, &G0T1_handler));
	//timer_pause(TIMER_GROUP_0, TIMER_1);

	//Modo manual
	timer_init(TIMER_GROUP_1, TIMER_0, &config);		// inicializacion el timer 0 del grupo 1
	timer_set_counter_value(TIMER_GROUP_1, TIMER_0, 0);	// Reiniciamos valor del temporizador a 0
	timer_set_alarm_value(TIMER_GROUP_1, TIMER_0, 10 * (TIMER_BASE_CLK / PRESCALER) ); 	  // Alarma configurada cada 10 segundos
	timer_isr_handle_t G1T0_handler;// Declaramos handler para las interrupciones del timer
	timer_enable_intr(TIMER_GROUP_1,TIMER_0);// Habilitamos interrupciones
	printf("Interrupcion G1 T0: %d \n", timer_isr_register(TIMER_GROUP_1, TIMER_0, &TMR_ISR2, NULL, ESP_INTR_FLAG_IRAM, &G1T0_handler));
	timer_pause(TIMER_GROUP_1, TIMER_0);// Pausamos el temporizador

	//Modo automático
	timer_init(TIMER_GROUP_1, TIMER_1, &config);		// inicializacion el timer 1 del grupo 1
	timer_set_counter_value(TIMER_GROUP_1, TIMER_1, 0);	// Reiniciamos valor del temporizador a 0
	timer_set_alarm_value(TIMER_GROUP_1, TIMER_1, 60 * (TIMER_BASE_CLK / PRESCALER) ); 	  // Alarma configurada cada 60 segundos (1 minuto)
	timer_isr_handle_t G1T1_handler; // Declaramos handler para las interrupciones del timer
	timer_enable_intr(TIMER_GROUP_1,TIMER_1);// Habilitamos interrupciones
	printf("Interrupcion G1 T1: %d \n", timer_isr_register(TIMER_GROUP_1, TIMER_1, &TMR_ISR3, NULL, ESP_INTR_FLAG_IRAM, &G1T1_handler));
	timer_pause(TIMER_GROUP_1, TIMER_1);// Pausamos el temporizador

	adc1_config_width(ADC_WIDTH_BIT_12);						// Selección de bits para ADC
	adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);	// Atenuación de lectura ADC

	dac_output_enable(DAC_CHANNEL_1);							// Habilitamos salida DAC

	printf("Configurando PWM LED R \n");
	ledc_channel_config_t channel_config;				// Declaración de estructura de configuraciones del canal a usar para señal PWM
	channel_config.gpio_num = LED_R;					// Indicamos que GPIO será utilizado
	channel_config.speed_mode = LEDC_HIGH_SPEED_MODE;	// Fuente de reloj a utilizar
	channel_config.channel = LEDC_CHANNEL_0;			// Canal a utilizar
	channel_config.intr_type = LEDC_INTR_DISABLE;		// Deshabilitamos las interrupciones
	channel_config.timer_sel = LEDC_TIMER_0;			// Reloj utilizado para la generación de pulsos de la señal
	channel_config.hpoint = 0;							// Valor en el que la señal cambia de 0 a 1
	channel_config.duty = duty_led_r*RES_PWM/100;		// Ciclo de trabajo en el que inicia la señal
	ledc_channel_config(&channel_config);				// Se asocian las configuraciones al canal

	printf("Configurando PWM LED G \n");
	channel_config.gpio_num = LED_G;					// Indicamos que GPIO será utilizado
	channel_config.channel = LEDC_CHANNEL_1;			// Canal a utilizar
	channel_config.timer_sel = LEDC_TIMER_0;			// Reloj utilizado para la generación de pulsos de la señal
	channel_config.hpoint = 0;							// Valor en el que la señal cambia de 0 a 1
	channel_config.duty = duty_led_g*RES_PWM/100;		// Ciclo de trabajo en el que inicia la señal
	ledc_channel_config(&channel_config);				// Se asocian las configuraciones al canal

	printf("Configurando PWM LED B \n");
	channel_config.gpio_num = LED_B;					// Indicamos que GPIO será utilizado
	channel_config.channel = LEDC_CHANNEL_2;			// Canal a utilizar
	channel_config.timer_sel = LEDC_TIMER_0;			// Reloj utilizado para la generación de pulsos de la señal
	channel_config.hpoint = 0;							// Valor en el que la señal cambia de 0 a 1
	channel_config.duty = duty_led_b*RES_PWM/100;		// Ciclo de trabajo en el que inicia la señal
	ledc_channel_config(&channel_config);				// Se asocian las configuraciones al canal

	printf("Configurando PWM servo \n");
	channel_config.gpio_num = SERVO;					// Indicamos que GPIO será utilizado
	channel_config.channel = LEDC_CHANNEL_3;			// Canal a utilizar
	channel_config.timer_sel = LEDC_TIMER_0;			// Reloj utilizado para la generación de pulsos de la señal
	channel_config.hpoint = 0;
	channel_config.duty = duty_servo;					// Ciclo de trabajo en el que inicia la señal
	ledc_channel_config(&channel_config);				// Se asocian las configuraciones al canal

	printf("Configurando timer PWM \n");
	ledc_timer_config_t timer_config;					// Declaración de estructura de configuración del temporizador de la señal PWM
	timer_config.speed_mode = LEDC_HIGH_SPEED_MODE;		// Fuente de reloj a utilizar
	timer_config.duty_resolution = LEDC_TIMER_12_BIT;	// Resolución del temporizador (Valor máximo al que llega el contador)
	timer_config.timer_num = LEDC_TIMER_0;				// Reloj utilizado para la generación de pulsos de la señal
	timer_config.freq_hz = 50;							// Frecuencia de la señal PWM
	timer_config.clk_cfg = LEDC_AUTO_CLK;
	ledc_timer_config(&timer_config);
	printf("Configuracion terminada \n");				// Se Asocian las configuraciones al timer

	return;
}

// Configuracion del modulo Wi-Fi
void wifi_connection()
{

    esp_netif_init();                    	// InicializaciÃ³n de TCP/IP
    esp_event_loop_create_default();     	// Handler de eventos del Wi-Fi
    esp_netif_create_default_wifi_sta(); 	// Interfaz de red para el Wi-Fi funcionando en modo estaciÃ³n
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();	// Configuraciones por defecto del Wi-Fi
    esp_wifi_init(&wifi_initiation);		// Inicializacian del modulo Wi-Fi
    esp_wifi_set_mode(WIFI_MODE_STA);		// Configuracian en modo estaciÃ³n

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    wifi_config_t wifi_configuration = {
        .sta = {
            .ssid = SSID,					// Identificador de la red Wi-Fi
            .password = PASS				// Contaseña de la red Wi-Fi
        }
    };
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);	// Configuramos el modulo con las credenciales
    esp_wifi_start();						// Iniciamos el modulo
    esp_wifi_connect();						// Establecemos la conexion
}

// Funcion para capturar los eventos al hacer un POST via HTTP
esp_err_t client_event_post_handler(esp_http_client_event_handle_t evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        printf("HTTP_EVENT_ON_DATA: %.*s\n", evt->data_len, (char *)evt->data);
        break;

    default:
        break;
    }
    return ESP_OK;
}

// Función para hacer un POST via HTTP
//Funciones para enviar los valores convertidos de la lectura de la entrada analógica (para porcentaje) a la nube

void post_contador_temperatura(float contador) //Contador temperatura
{
	char url[100];//Tipo de dato char para caracteres de 100
	// Configuraciones del cliente HTTP
	sprintf(url, "https://io.adafruit.com/api/v2/%s/feeds/%s/data", IO_USERNAME, FEED_KEY_TEMPERATURA);//URL de bloque de IO.Adafruit con lectura de sensor de temperatura
	printf("%s \n", url);
    esp_http_client_config_t config_post = {//Estructura de cliente HTTP
        .url = url,//auntentitación con URL
        .method = HTTP_METHOD_POST,//Se establece el método de solicitud http
        .cert_pem = (const char *)certificate_pem_start,//Certificación de servidor SSL, formato PEM como cadena, si cliente desea verificar servidor
        .event_handler = client_event_post_handler //Identificador de evento
    };

    esp_http_client_handle_t client = esp_http_client_init(&config_post);	// InicializaciÃ³n de cliente HTTP
    char post_data[20];//Tipo de dato char para caracteres de 20
    sprintf(post_data, "{\"value\": %f}", contador);						// Definicion de datos a enviar en la solicitud
    esp_http_client_set_post_field(client, post_data, strlen(post_data));	// Configuracion de datos a enviar
    esp_http_client_set_header(client, "Content-Type", "application/json");	// Configuracion de encazados: Tipo de datos a enviar
    esp_http_client_set_header(client, "X-AIO-Key", IO_KEY);				// Configuracion de encazados: Credenciales de la solicitud

    esp_http_client_perform(client);// Enviado de solicitud
    esp_http_client_cleanup(client);//Se cierra la conexión cliente-servidor										// Cerramos comunicaciÃ³n cliente-servidor
}


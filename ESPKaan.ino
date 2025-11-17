/*
 * Código del ESP32-wroom-32D para Kaan, proyecto de integración mecatrónica
 *
 * Hardware:
 * - LCD 20x4 (I2C)
 * - Encoder KY-040 (CLK, DT, SW)
 * - 1x Botón externo (para "Back")
 *
 * Logica de Navegacion:
 * - Encoder (Giro): Mueve el cursor o ajusta valores.
 * - Boton SW (OK): Aceptar / Siguiente.
 * - Boton Externo (BACK): Atras / Cancelar.
 *
 */

//////////////////Librerías/////////////////////////
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "esp_timer.h"
#include "DHT.h" 
#include <MPU6050.h>            //Para el acelerómetro y giroscopio
#include <WiFi.h>
#include <HTTPClient.h>   //Para conectarme al FireBase, por HTTPClient, no esp firebase
#include <ArduinoJson.h>  //Para manipular json's
#include "time.h"         //Para obtener el tiempo local

#define DHTPIN 4                //Pin del sensor de temperatura y humedad
#define DHTTYPE DHT11           //Tipo del sensor (Porque hay como FHT11, DHT12, etc)

MPU6050 mpu;

/////////ssid y password del wifi
//const char* ssid = "OTOÑO25";       
//const char* password = "Ib3r02025ui@"; 

const char* ssid = "INFINITUMABD2_2.4";       
const char* password = "2GJ98hx27P"; 

String firebaseURL = "https://pruebaesp32kaan-default-rtdb.firebaseio.com/";

//////////////////Pines/////////////////////////

// Pines del LCD I2C (Por defecto en la ESP32)
// SDA = GPIO 21
// SCL = GPIO 22

// Pines del Encoder KY-040
#define ENCODER_CLK_PIN 19
#define ENCODER_DT_PIN  18
#define ENCODER_SW_PIN  5  // Botón OK (Switch del encoder)

// Pin del Botón Externo
#define BOTON_BACK_PIN 23 // Botón ATRÁS

// Configuración del LCD
LiquidCrystal_I2C lcd(0x27, 20, 4);

//////////////////Estados de la máquina de estados/////////////////////////
enum State {
  STATE_SPLASH,
  STATE_HOME,
  STATE_NO_SESSION,
  STATE_MENU_MAIN,
  STATE_MENU_MODIFY,
  STATE_EDIT_DIA,
  STATE_NEW_WARN,
  NEW_DIA,
  NEW_MIN_TEMP,
  NEW_MIN_HUMD,
  STATE_NEW_CONFIRM,
  NEW_MAX_TEMP,
  NEW_MAX_HUMD,
  EDIT_TEMP_MIN,
  EDIT_TEMP_MAX,
  EDIT_HUMD_MIN,
  EDIT_HUMD_MAX,
  STATE_LIMIT
};
State currentState;

//Defino los chars especiales (á, é, í, ó, ú, ñ, Í, º)
                            //(0, 1, 2, 3, 4, 5, 6, 7)
//Tengo que definir por bits
//Sólo puedo definir 8 (0 - 7)

byte a_tilde[8] = {
  B00110,
  B00000,
  B01110,
  B00001,
  B01111,
  B10001,
  B01111,
  B00000};
byte e_tilde[8] = {
  B00110,
  B00000,
  B01110,
  B10001,
  B11111,
  B10000,
  B01110,
  B00000};
byte i_tilde[8] = {
  B00111,
  B00000,
  B01100,
  B00100,
  B00100,
  B00100,
  B01110,
  B00000};

byte o_tilde[8] = {
  B00110,
  B00000,
  B01110,
  B10001,
  B10001,
  B10001,
  B01110,
  B00000};

byte u_tilde[8] = {
  B00110,
  B00000,
  B10001,
  B10001,
  B10001,
  B10011,
  B01101,
  B00000};

byte enie[8] = {
  B01110,
  B00000,
  B10110,
  B11001,
  B10001,
  B10001,
  B10001,
  B00000};

byte I_tilde[8] = {
  B01110,
  B00000,
  B01110,
  B00100,
  B00100,
  B00100,
  B01110,
  B00000};

//////////////////Varaibles de navegación/////////////////////////
// Para el Encoder
volatile long encoderPos = 0; // Posición actual del encoder (modificada por ISR)
long lastEncoderPos = 0;      // Última posición leída
int encoderIncrement = 0;     // Cambio detectado (-1, 0, o 1)

// Para los Botones (Debounce)
bool okPressed = false;
bool backPressed = false;
unsigned long lastOkPressTime = 0;
unsigned long lastBackPressTime = 0;
const long debounceDelay = 300; // 50ms de anti-rebote
bool lastOkState = HIGH; // Para detectar flanco de bajada (HIGH por PULLUP)
bool lastBackState = HIGH; // Para detectar flanco de bajada

// Variables de Estado del Menú
int menuSelection = 0; // Selector de menú actual
int editableValue = 1; // Para editar números
bool sessionActive = false; // Flag para saber si hay una sesión

//ID de la caja
String idCaja = "";

// Variables del limites y tiempo
int tiempo = 7;
int tempInf = 30;
int humdInf = 60;
int humdSup = 90;
int tempSup = 60;

//Variables de auxilio, para que se pueda seguir monitoreando mientras se cambian los límites
int auxTimepo = 0;
int auxTempInf = 30;
int auxHumdInf = 60;
int auxHumdSup = 90;
int auxTempSup = 60;

//Para temperatura y humedad
float h = 0.0, t = 0.0;
float prevH = 0.0, prevT = 0.0;   //Es la medición previa, para determinar si necesito mandar los datos a actualizar o no
#define UMBRAL 0.2                //Si el cambio pasa este umbral, manda a actualizar los datos
DHT dht(DHTPIN, DHTTYPE);

//Para movimiento
const float UMBRAL_MOVIMIENTO = 40000.0;
int16_t AccelX, AccelY, AccelZ;
int16_t PrevAccelX = 0, PrevAccelY = 0, PrevAccelZ = 0;
volatile bool sens_mov = false;
esp_timer_handle_t timer_mov;  
volatile bool movActive = false;
volatile bool movDetected = false, prevMovDetected = false;

////////////Banderas para los timers///////////////
volatile bool update_lcd = false;
volatile bool upload_firebase = false;
esp_timer_handle_t timer_lcd;  
esp_timer_handle_t timer_firebase;    
volatile bool timerLCDactive = false;
volatile bool timerFIREBASEactive = false;

///////////Para el contador de tiempo/////////
volatile unsigned long segundos_restantes =  86400UL;  //Este sería un día
esp_timer_handle_t contador_timer;
volatile bool timerDiasactive = false;
unsigned int dias, horas, minutos, segundos;
unsigned long restante;
volatile bool dias_lcd = false;
volatile bool dias_end = false;
volatile bool parpadea = false;

// Callback que se ejecuta cada segundo
void IRAM_ATTR contador_callback(void* arg) {
  
  if (segundos_restantes > 0) {   //Si no ha acabado...
    segundos_restantes--;

    //Fórmulas para cambiar el tiempo restante en días, horas, minutos y segundos
    restante = segundos_restantes;
    dias = restante / 86400UL;
    restante %= 86400UL;
    horas = restante / 3600UL;
    restante %= 3600UL;
    minutos = restante / 60UL;
    restante %= 60UL;
    segundos = restante;

    //Para disparar la función de actaulizar el lcd
    dias_lcd = true;
    
  } else {    //Si ya acabó
    parpadea = !parpadea;   //Variable que intercala entre true y false, para que el tiempo parpadee cuando haya acabado
    dias_end = true;        //Dispara la función para indicar que acabó el tiempo
  }
}

//////////////////Interrupción del encoder/////////////////////////
// Esta función se llama CADA vez que el pin CLK cambia
void IRAM_ATTR readEncoderISR() {
  // Lee el pin DT. Si es diferente a CLK, giramos en un sentido.
  if (digitalRead(ENCODER_DT_PIN) != digitalRead(ENCODER_CLK_PIN)) {
    encoderPos++;
  } else {
    encoderPos--;
  }
}

//Interrupción de los timers
//ES MEJOR NO PONER CÓDIGO TAN PESADO
void IRAM_ATTR timer_callback(void* arg) {
  int id = (int)arg;

  switch(id){   //Levanta la bandera adecuada, dependiendo del arg
    case 1: update_lcd = true; break;
    case 2: upload_firebase = true; break; 
    case 3: sens_mov = true; break;
  }

}

//////////Para poner en el núcleo 1 (no el 0), las tareas del firebase, no trabará la línea principal)
//Struct que se enviará a la cola. Tiene los datos para hacer el patch, put, etc
struct HttpTask {
  String endpoint;  //como el link
  String payload;   //los datos a parchear o postear o put, etc
  String method;    //patch, post, put
};
//Variable de la cola
QueueHandle_t colaHTTP;

//////////////////Setup/////////////////////////
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22); 
  // Configurar Pines de Botones
  // INPUT_PULLUP la resistencia interna.
  // El pin HIGH en defecto.
  // Al presionar el botón (conectado a GND), el pin leerá LOW.
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
  pinMode(BOTON_BACK_PIN, INPUT_PULLUP);

  // Configurar Pines del Encoder
  pinMode(ENCODER_CLK_PIN, INPUT);
  pinMode(ENCODER_DT_PIN, INPUT);
  
  // Activar la interrupción en el pin CLK
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK_PIN), readEncoderISR, CHANGE);

  /////////////////////Setup del núcleo 1////////////////////
  //Puede poner a la cola hasta 10 peticiones
  //sizeof, es el tamaño de lo que recibirá la cola (apuntador del struct)
  colaHTTP = xQueueCreate(10, sizeof(HttpTask*));

  xTaskCreatePinnedToCore(
    httpTask,       //la función que ejecuta la tarea
    "HTTP Task",    //nombre de la tarea (solo para debug)
    10000,          //tamaño de stack (en palabras, NO bytes)
    NULL,           //parámetro enviado a la tarea
    1,              //prioridad (0 baja, 3 media, 5 alta, etc)
    NULL,           //handle de la tarea (no lo ocupo)
    1               //núcleo donde se ejecutará (0 o 1)
  );

  /////////////////////Setup de los timers//////////////////////////////
  // Configura el timer periódico
  const esp_timer_create_args_t args_lcd = {
    .callback = &timer_callback,        //Apuntador de la función
    .arg = (void*)1,                        //Argumento que recibirá la función
    .dispatch_method = ESP_TIMER_TASK,  //Método de llamado.  ESP_TIMER_TASK se ejecuta en la tarea de esp_timer
    .name = "timer_lcd"
  };
  esp_timer_create_args_t args_firebase = {
    .callback = &timer_callback,
    .arg = (void*)2,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "t2"
  };

  esp_timer_create_args_t args_mov = {
    .callback = &timer_callback,
    .arg = (void*)3,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "flag_mov"
  };

  // Configurar el timer
  const esp_timer_create_args_t timer_args = {
    .callback = &contador_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "contador_timer"
  };
              
  esp_timer_create(&args_lcd, &timer_lcd);  
  esp_timer_create(&timer_args, &contador_timer);
  esp_timer_create(&args_firebase, &timer_firebase);
  esp_timer_create(&args_mov, &timer_mov);      

  // Iniciar LCD
  lcd.init();
  lcd.backlight();
  
  //Agregar los chars especialicados que creé
  lcd.createChar(0, a_tilde);
  lcd.createChar(1, e_tilde);
  lcd.createChar(2, i_tilde);
  lcd.createChar(3, o_tilde);
  lcd.createChar(4, u_tilde);
  lcd.createChar(5, enie);
  lcd.createChar(6, I_tilde);

  //////////////////Pantalla de inicio/////////////////////////
  currentState = STATE_SPLASH;
  printInit();
  delay(3000); 

  mpu.initialize();
  if (mpu.testConnection()){
    Serial.println("Sensor iniciado correctamente");
    mpu.getAcceleration(&PrevAccelX, &PrevAccelY, &PrevAccelZ);
  } else {
    Serial.println("Error al iniciar el sensor");
    showErrorLCD(1);
    while(true);
  }

  setup_wifi(true);
  initTimeLocal(true);

  printSearchSesiones();
  idCaja = obtenerCajaActiva();

  if(!idCaja.length()){ //Si está vacío el string, no encontró cajas
    printSesionOn();
    delay(3000);
    idCaja = "1_ID";
    sessionActive = false;
  } else {
    printSesionOff();
    leerInfoGeneral();
    delay(3000);
    sessionActive = true;
  }

  //Decidir el estado inicial después del inicio
  //Si es que podemos cargar memoria
  if (sessionActive) {
    segundos_restantes = (unsigned long) tiempo * 86400UL;
    currentState = STATE_HOME;
    startTimerDias();
    startTimerFirebase();
    startTimerLCD();
    startTimerMov();
  } else {
    currentState = STATE_NO_SESSION;
  }
  
  //Dibujar la primera pantalla
  drawScreen(); 
}

//////////////////Loop/////////////////////////
void loop() {
  //Revisa los inputs (botones y encoder)
  handleInputs();

  //Si un input causó un cambio, la bandera estará activa
  if (okPressed || backPressed || encoderIncrement != 0) {
    //Procesa el input según el estado actual
    handleStateLogic();
    
    //Limpia las banderas
    okPressed = false;
    backPressed = false;
    encoderIncrement = 0;
  }

  //////Para contar los días
  if (dias_lcd) {
    dias_lcd = false;
    switch(currentState){
      case STATE_HOME: showTiempoRes(); break;
    }
  }

  ///////Para mostrar que acabó el contador
  if(dias_end){
    dias_end = false;
    switch(currentState){
      case STATE_HOME: parpadea ? showDiasCero() : showDiasClear(); break;
    }
  }

  ////////Para detectar el movimiento, no requiero que lo haga en cada loop/////////
  if(movActive){
    mpu.getAcceleration(&AccelX, &AccelY, &AccelZ);

    float deltaX = (float)abs(AccelX - PrevAccelX);
    float deltaY = (float)abs(AccelY - PrevAccelY);
    float deltaZ = (float)abs(AccelZ - PrevAccelZ);

    float totalDelta = sqrt(deltaX*deltaX + deltaY*deltaY + deltaZ*deltaZ);

    prevMovDetected = movDetected;

    if(UMBRAL_MOVIMIENTO < totalDelta){
      movDetected = true;
    } else {
      movDetected = false;
    }

    if(prevMovDetected != movDetected){
      if (movDetected){
        Serial.println("Se movió");
      } else {
        Serial.println("Se dejó de mover xd");
      }
      
    }

    PrevAccelX = AccelX;
    PrevAccelY = AccelY;
    PrevAccelZ = AccelZ;
  }

  //////Para actualizar el lcd////////////
  if (update_lcd) {
    update_lcd = false;
    calcTempHum();
    switch(currentState){
      case STATE_HOME: showTempHum(); break;
    }
    Serial.printf("T: %.2f ºC, H: %.2f%%\n", t, h);
    if (!isnan(t) && !isnan(h)){
      if (abs(prevT - t) >= UMBRAL || abs(prevH - h) >= UMBRAL) {
        Serial.println("Cambio detectado, cambiando en firebase");
        FiBaUpdateCurrData(t, h);
      }
    } 
  }

  //////Para subir datos a firebase////////////
  if (upload_firebase) {
    upload_firebase = false;
    Serial.println("Upload a firebase");
    if (!isnan(t) && !isnan(h)){
      
    } 
  }

}

//////////////////Manejo de inputs/////////////////////////
void handleInputs() {
  // --- Encoder ---
  // Lee la posición actual del encoder (variable volatil)
  long currentEncoderPos = encoderPos;

  if (currentEncoderPos != lastEncoderPos) {
    // Calcula la diferencia. El usuario pide 2 pulsos.
    if (currentEncoderPos >= lastEncoderPos + 2) {
      encoderIncrement = 1;
      lastEncoderPos = currentEncoderPos; // Actualiza la base solo al cumplir
    } else if (currentEncoderPos <= lastEncoderPos - 2) {
      encoderIncrement = -1;
      lastEncoderPos = currentEncoderPos; // Actualiza la base solo al cumplir
    }
    // Si la diferencia es solo 1, se ignora, esperando el segundo pulso.
  }

  // --- Botón OK (SW) ---
  // Detecta el flanco de bajada (cuando se PRESIONA el botón)
  bool currentOkState = digitalRead(ENCODER_SW_PIN);
  if (currentOkState == LOW && lastOkState == HIGH) {
    if ((millis() - lastOkPressTime) > debounceDelay) {
      okPressed = true; // Activa la bandera SOLO UNA VEZ por presion
      lastOkPressTime = millis(); // Resetea el tiempo de debounce
    }
  }
  lastOkState = currentOkState; // Actualiza el estado anterior

  // --- Botón BACK ---
  // Detecta el flanco de bajada
  bool currentBackState = digitalRead(BOTON_BACK_PIN);
  if (currentBackState == LOW && lastBackState == HIGH) {
    if ((millis() - lastBackPressTime) > debounceDelay) {
      backPressed = true; // Activa la bandera SOLO UNA VEZ por presion
      lastBackPressTime = millis(); // Resetea el tiempo de debounce
    }
  }
  lastBackState = currentBackState; // Actualiza el estado anterior
}

///////////Lógica de transición de la máquina de estados///////////////
void handleStateLogic() {
  bool needsRedraw = false; // Solo redibuja si algo cambia

  switch (currentState) {
    
    case STATE_NO_SESSION:    //Estado de inicio, sin medición
      if (okPressed) {
        currentState = NEW_DIA;   //Ok, iniciar nueva medición e ir a NEW_DIA
        menuSelection = tiempo;   //Setear igual el tiempo y menuSelection
        needsRedraw = true;
      }
      break;

    case STATE_HOME:        //Menú principal, muestra temp, humd y el tiempo restante
      if (okPressed) {
        currentState = STATE_MENU_MAIN;
        menuSelection = 0;
        needsRedraw = true;
      }

      if (backPressed) {
        currentState = STATE_LIMIT;
        menuSelection = 0;
        needsRedraw = true;
      }
      break;

    case STATE_LIMIT:        //Menú que muestra los límites
      if (okPressed) {
        currentState = STATE_MENU_MAIN;
        menuSelection = 0;
        needsRedraw = true;
      }

      if (backPressed) {
        currentState = STATE_HOME;
        menuSelection = 0;
        needsRedraw = true;
      }
      break;


    case STATE_MENU_MAIN:   //Menú de opciones de modificar límites o iniciar nueva medición
      if (encoderIncrement != 0) {
        menuSelection += encoderIncrement;
        if (menuSelection > 1) menuSelection = 0;
        if (menuSelection < 0) menuSelection = 1;
        needsRedraw = false;
      }
      // OK selecciona
      if (okPressed) {
        if (menuSelection == 0) { 
          currentState = STATE_MENU_MODIFY;
          menuSelection = 0; 
        } else {
          currentState = STATE_NEW_WARN;
          menuSelection = 0; 
        }
        needsRedraw = true;
      }

      if (backPressed) {
        currentState = STATE_HOME;
        needsRedraw = true;
      }
      break;

    case STATE_MENU_MODIFY:   //Menú de selección de cambio de límites o tiempo (nueva meidción de tiempo)
      if (encoderIncrement != 0) {
        menuSelection += encoderIncrement;
        if (menuSelection > 2) menuSelection = 0; 
        if (menuSelection < 0) menuSelection = 2;
        needsRedraw = false;
      }
      if (okPressed) {
        if (menuSelection == 0) { 
          currentState = STATE_EDIT_DIA;
          editableValue = tiempo; 
          menuSelection = 0;
        }
        else if (menuSelection == 1) { 
          currentState = EDIT_TEMP_MIN;
          auxTempInf = tempInf;
          editableValue = auxTempInf;
          menuSelection = 0;
        } else {
          currentState = EDIT_HUMD_MIN;
          auxHumdInf = humdInf;
          editableValue = auxHumdInf;
          menuSelection = 0;
        }
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = STATE_MENU_MAIN;
        needsRedraw = true;
        menuSelection = 0;
      }
      break;

    case EDIT_TEMP_MIN:     //Editar el límite de temp mínima
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < 0) editableValue = 0;
        if (editableValue > 99) editableValue = 99;
        needsRedraw = false;
      }
      if (okPressed) {
        auxTempInf = editableValue; 
        currentState = EDIT_TEMP_MAX;
        auxTempSup = auxTempInf + 1;
        editableValue = auxTempSup;
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = STATE_MENU_MODIFY;
        menuSelection = 0;
        needsRedraw = true;
      }
      break;

    case EDIT_TEMP_MAX:     //Editar el límite de temperatura máxima
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < auxTempInf + 1) editableValue = auxTempInf + 1;
        if (editableValue > 100) editableValue = 100;
        needsRedraw = false;
      }
      if (okPressed) {
        auxTempSup = editableValue; 
        currentState = STATE_HOME;
        menuSelection = 0;
        tempInf = auxTempInf;
        tempSup = auxTempSup;
        needsRedraw = true;
        FiBaSetTemp(tempInf, tempSup);
      }
      if (backPressed) {
        currentState = EDIT_TEMP_MIN;
        editableValue = auxTempInf;
        needsRedraw = true;
      }
      break;

    case EDIT_HUMD_MIN:     //Editar el límite de humedad máxima
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < 0) editableValue = 0;
        if (editableValue > 99) editableValue = 99;
        needsRedraw = false;
      }
      if (okPressed) {
        auxHumdInf = editableValue; 
        currentState = EDIT_HUMD_MAX;
        auxHumdSup = auxHumdInf + 1;
        editableValue = auxHumdSup;
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = STATE_MENU_MODIFY;
        menuSelection = 0;
        needsRedraw = true;
      }
      break;

    case EDIT_HUMD_MAX:     //Editar el límite de humedad máxima
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < auxHumdInf + 1) editableValue = auxHumdInf + 1;
        if (editableValue > 100) editableValue = 100;
        needsRedraw = false;
      }
      if (okPressed) {
        auxHumdSup = editableValue; 
        currentState = STATE_HOME;
        menuSelection = 0;
        humdInf = auxHumdInf;
        humdSup = auxHumdSup;
        needsRedraw = true;
        FiBaSetHumd(humdInf, humdSup);
      }
      if (backPressed) {
        currentState = EDIT_HUMD_MIN;
        editableValue = auxHumdInf;
        needsRedraw = true;
      }
      break;

    case STATE_EDIT_DIA:      //Editar el tiempo, esto inicia un nuevo timer con el tiempo (días) indicado
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < 1) editableValue = 1; 
        needsRedraw = false;
      }
      if (okPressed) {
        tiempo = editableValue;
        stopTimerDias();                            //86400
        segundos_restantes = (unsigned long) tiempo * 86400UL;
        startTimerDias();
        FiBaSetDias(tiempo);
        currentState = STATE_HOME;
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = STATE_MENU_MODIFY;
        needsRedraw = true;
      }
      break;

    case STATE_NEW_WARN:    //Aviso de que, se iniciará una nueva medición, borrando la anterior
      if (encoderIncrement != 0) {
        menuSelection = !menuSelection; 
        needsRedraw = false;
      }
      if (okPressed) {
        if (menuSelection == 1) {
          currentState = NEW_DIA;
          editableValue = tiempo; 
          stopTimerLCD();
          stopTimerDias();
          stopTimerFirebase();
          stopTimerMov();
          idCaja = siguienteID(idCaja); //Calcular la nueva id
        } else { 
          currentState = STATE_MENU_MAIN;
        }
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = STATE_MENU_MAIN;
        needsRedraw = true;
      }
      break;

    case NEW_DIA:     //Introducir el día de la nueva medición
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < 1) editableValue = 1;
        needsRedraw = false;
      }
      if (okPressed) {
        tiempo = editableValue; 
        currentState = NEW_MIN_TEMP;
        editableValue = tempInf; 
        needsRedraw = true;
      }
      break;

    case NEW_MIN_TEMP:    //Límite de temp inferior para la nueva medición
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < 0) editableValue = 0;
        if (editableValue > 99) editableValue = 99;
        needsRedraw = false;
      }
      if (okPressed) {
        tempInf = editableValue; 
        currentState = NEW_MAX_TEMP;
        tempSup = tempInf + 1;
        editableValue = tempSup;
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = NEW_DIA;
        editableValue = tiempo;
        needsRedraw = true;
      }
      break;

    case NEW_MAX_TEMP:   //Límite de temp superior para la nueva medición
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < tempInf + 1) editableValue = tempInf + 1;
        if (editableValue > 100) editableValue = 100;
        needsRedraw = false;
      }
      if (okPressed) {
        tempSup = editableValue; 
        currentState = NEW_MIN_HUMD;
        editableValue = humdInf;
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = NEW_MIN_TEMP;
        editableValue = tempInf;
        needsRedraw = true;
      }
      break;

    case NEW_MIN_HUMD:  //Límite de humedad inferior para la nueva medición
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < 0) editableValue = 0;
        if (editableValue > 99) editableValue = 99;
        needsRedraw = false;
      }
      if (okPressed) {
        humdInf = editableValue;
        currentState = NEW_MAX_HUMD;
        humdSup = humdInf + 1;
        editableValue = humdSup;
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = NEW_MAX_TEMP;
        editableValue = tempSup;
        needsRedraw = true;
      }
      break;

    case NEW_MAX_HUMD:    //Límite de humedad superior para la nueva medición
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < humdInf + 1) editableValue = humdInf + 1;
        if (editableValue > 100) editableValue = 100;
        needsRedraw = false;
      }
      if (okPressed) {
        humdSup = editableValue;
        currentState = STATE_NEW_CONFIRM;
        menuSelection = 0;
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = NEW_MIN_HUMD;
        editableValue = humdInf;
        needsRedraw = true;
      }
      break;

    case STATE_NEW_CONFIRM:   //Confirmar la nueva medición con sus límites ya fijados
      if (encoderIncrement != 0) {
        menuSelection = !menuSelection;
        needsRedraw = false;
      }
      if (okPressed) {
        if (menuSelection == 0) { 
          Serial.println("--- NUEVA SESION INICIADA ---");
          Serial.printf("Dias: %d, TempInf: %d, TempSup : %d, HumdInf: %d, HumdSup: %d\n", tiempo, tempInf, tempSup, humdInf, humdSup);
          sessionActive = true;
          currentState = STATE_HOME;
          segundos_restantes = (unsigned long) tiempo * 86400UL;
          startTimerLCD();
          startTimerDias();
          startTimerFirebase();
          startTimerMov();
          subirInfoGeneral(tempInf, tempSup, humdInf, humdSup, tiempo);
        } else { 
          currentState = NEW_MAX_HUMD;
        }
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = NEW_MAX_HUMD;
        needsRedraw = true;
      }
      break;

  }

  
  if (needsRedraw) {  //Si cambia el estado, imprime toda la pantalla para ese estado
    drawScreen();
    printEstado();
  } else {            //Si no cambió el estado, sólamente actualiza los datos o flechas
    changeValor();
  }
}

///////////////Imprime la pantalla inicial de cada estado///////////////////////////////
void drawScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);

  switch (currentState) {
    
    case STATE_NO_SESSION:
      
      lcd.print("== SIN MEDICI");
      lcd.write(3);
      lcd.print("N ==");
      lcd.setCursor(0, 1);
      lcd.print("  No hay datos para");
      lcd.setCursor(0, 2);
      lcd.print("  mostrar.");
      lcd.setCursor(0, 3);
      lcd.print("> Iniciar Medici");
      lcd.write(3);
      lcd.print("n"); 
         
      /*
      lcd.write(0);
      lcd.write(1);
      lcd.write(2);
      lcd.write(3);
      lcd.write(4);
      lcd.write(5);

      lcd.setCursor(0, 1);
      lcd.print("aeiounAEIOUN");
      */
    
      break;

    
    case STATE_HOME:  //Pantalla que muestra los datos leídos
      lcd.print("CAJA ");
      lcd.print(idCaja);
      lcd.print(" [ACTIVA]");
      lcd.setCursor(0, 1);
      lcd.print("T: ---.- ");
      lcd.write(223);
      lcd.print("C  H: ---%");
      lcd.setCursor(0, 2);
      lcd.print("Tiempo restante:");
      lcd.setCursor(0, 3);
      lcd.print("--D --H --M --S");
      break;

    
    case STATE_LIMIT: //Pantalla que muestra los límites
      lcd.print("===== L");
      lcd.write(6);
      lcd.print("MITES ======");
      lcd.setCursor(0, 1);
      lcd.print("Temp(");
      lcd.write(223);
      lcd.print("C): ");
      lcd.print(tempInf);
      lcd.print(" - ");
      lcd.print(tempSup);
      lcd.setCursor(0, 2);
      lcd.print("Humd(%):  ");
      lcd.print(humdInf);
      lcd.print(" - ");
      lcd.print(humdSup);
      break;

    case STATE_MENU_MAIN:
      lcd.print("== MEN");
      lcd.write(4);
      lcd.print(" PRINCIPAL ==");
      lcd.setCursor(0, 1);
      lcd.print("> Modificar l");
      lcd.write(2);
      lcd.print("mites");
      lcd.setCursor(0, 2);
      lcd.print("  Nueva medici");
      lcd.write(3);
      lcd.print("n");
      break;

    case STATE_MENU_MODIFY:
      lcd.print(" MODIFICAR L");
      lcd.write(6);
      lcd.print("MITES ==");
      lcd.setCursor(0, 1);
      lcd.print("> L");
      lcd.write(2);
      lcd.print("mite de tiempo");
      lcd.setCursor(0, 2);
      lcd.print("  L");
      lcd.write(2);
      lcd.print("mite de temp");
      lcd.setCursor(0, 3);
      lcd.print("  L");
      lcd.write(2);
      lcd.print("mite de humedad"); 
      Serial.printf("Dias: %d, TempInf: %d, TempSup : %d, HumdInf: %d, HumdSup: %d\n", tiempo, tempInf, tempSup, humdInf, humdSup);
      break;

    case STATE_EDIT_DIA:
      lcd.print("Modif tiempo (d");
      lcd.write(2);
      lcd.print("as)");
      lcd.setCursor(0, 1);
      lcd.print("L");
      lcd.write(2);
      lcd.print("mite actual: ");
      lcd.print(tiempo); 
      lcd.setCursor(0, 2);
      lcd.print("Nuevo l");
      lcd.write(2);
      lcd.print("mite: [");
      lcd.print(editableValue);
      lcd.print("]");
      break;

    case EDIT_TEMP_MIN:
      lcd.print("Modif temp m");
      lcd.write(2);
      lcd.print("n (");
      lcd.write(223);
      lcd.print("C)");
      lcd.setCursor(0, 1);
      lcd.print("L");
      lcd.write(2);
      lcd.print("mite actual: ");
      lcd.print(tempInf); 
      lcd.setCursor(0, 2);
      lcd.print("Nuevo l");
      lcd.write(2);
      lcd.print("mite: [");
      lcd.print(editableValue);
      lcd.print("]");
      break;

    case EDIT_TEMP_MAX:
      lcd.print("Modif temp m");
      lcd.write(0);
      lcd.print("x (");
      lcd.write(223);
      lcd.print("C)");
      lcd.setCursor(0, 1);
      lcd.print("L");
      lcd.write(2);
      lcd.print("mite actual: ");
      lcd.print(tempSup); 
      lcd.setCursor(0, 2);
      lcd.print("Nuevo l");
      lcd.write(2);
      lcd.print("mite: [");
      lcd.print(editableValue);
      lcd.print("]");
      break;

    case EDIT_HUMD_MIN:
      lcd.print("Modif humd m");
      lcd.write(2);
      lcd.print("n (%)");
      lcd.setCursor(0, 1);
      lcd.print("L");
      lcd.write(2);
      lcd.print("mite actual: ");
      lcd.print(humdInf); 
      lcd.setCursor(0, 2);
      lcd.print("Nuevo l");
      lcd.write(2);
      lcd.print("mite: [");
      lcd.print(editableValue);
      lcd.print("]");
      break;

    case EDIT_HUMD_MAX:
      lcd.print("Modif humd m");
      lcd.write(0);
      lcd.print("x (%)");
      lcd.setCursor(0, 1);
      lcd.print("L");
      lcd.write(2);
      lcd.print("mite actual: ");
      lcd.print(humdSup); 
      lcd.setCursor(0, 2);
      lcd.print("Nuevo l");
      lcd.write(2);
      lcd.print("mite: [");
      lcd.print(editableValue);
      lcd.print("]");
      break;

    case STATE_NEW_WARN:
      lcd.setCursor(0, 0);
      lcd.print("   !! ATENCI");
      lcd.write(3);
      lcd.print("N !!");
      lcd.setCursor(0, 1);
      lcd.print("  Borrar");
      lcd.write(0);
      lcd.print(" la sesi");
      lcd.write(3);
      lcd.print("n");
      lcd.setCursor(0, 2);
      lcd.print("  actual. Continuar?");
      lcd.setCursor(0, 3);
      lcd.print("> No");
      lcd.setCursor(10, 3);
      lcd.print("  S");
      lcd.write(2);
      break;

    case NEW_DIA:
      lcd.print("NUEVA SESI");
      lcd.write(3);
      lcd.print("N (1/5)");
      lcd.setCursor(0, 1);
      lcd.print("Tiempo l");
      lcd.write(2);
      lcd.print("mite (d");
      lcd.write(2);
      lcd.print("as)");
      lcd.setCursor(0, 2);
      lcd.print("D");
      lcd.write(2);
      lcd.print("as: [");
      lcd.print(editableValue);
      lcd.print("]");
      break;

      
    case NEW_MIN_TEMP:
      lcd.print("NUEVA SESI");
      lcd.write(3);
      lcd.print("N (2/5)");
      lcd.setCursor(0, 1);
      lcd.print("L");
      lcd.write(2);
      lcd.print("mite temp M");
      lcd.write(2);
      lcd.print("n (");
      lcd.write(223);
      lcd.print("C)");
      lcd.setCursor(0, 2);
      lcd.print("Temp: [");
      lcd.print(editableValue);
      lcd.print("]");
      break;

    case NEW_MAX_TEMP:
      lcd.print("NUEVA SESI");
      lcd.write(3);
      lcd.print("N (3/5)");
      lcd.setCursor(0, 1);
      lcd.print("L");
      lcd.write(2);
      lcd.print("mite temp M");
      lcd.write(0);
      lcd.print("x (");
      lcd.write(223);
      lcd.print("C)");
      lcd.setCursor(0, 2);
      lcd.print("Temp: [");
      lcd.print(editableValue);
      lcd.print("]");
      break;

    case NEW_MAX_HUMD:
      lcd.print("NUEVA SESI");
      lcd.write(3);
      lcd.print("N (5/5)");
      lcd.setCursor(0, 1);
      lcd.print("L");
      lcd.write(2);
      lcd.print("mite humd M");
      lcd.write(0);
      lcd.print("x (%)");
      lcd.setCursor(0, 2);
      lcd.print("Humd: [");
      lcd.print(editableValue);
      lcd.print("]");
      break;

    case NEW_MIN_HUMD:
      lcd.print("NUEVA SESI");
      lcd.write(3);
      lcd.print("N (4/5)");
      lcd.setCursor(0, 1);
      lcd.print("L");
      lcd.write(2);
      lcd.print("mite humd M");
      lcd.write(2);
      lcd.print("n (%)");
      lcd.setCursor(0, 2);
      lcd.print("Humd: [");
      lcd.print(editableValue);
      lcd.print("]");
      break;

    case STATE_NEW_CONFIRM:
      lcd.print("NUEVA SESI");
      lcd.write(3);
      lcd.print("N");
      lcd.setCursor(0, 1);
      lcd.print("Iniciar monitoreo");
      lcd.setCursor(0, 2);
      lcd.print("Caja ");
      lcd.print(idCaja);
      lcd.print("?"); 
      lcd.setCursor(0, 3);
      lcd.print("> Iniciar");
      lcd.setCursor(10, 3);
      lcd.print("  Cancelar");
      break;
  }
}

//Función para sólo cambiar el valor variable en el display
void changeValor(){


  switch (currentState) {

    case NEW_DIA:
      lcd.setCursor(7, 2);
      lcd.print(editableValue);
      lcd.print("] ");
      break;

    case NEW_MIN_TEMP:
      lcd.setCursor(7, 2);
      lcd.print(editableValue);
      lcd.print("] ");
      break;

    case NEW_MAX_TEMP:
      lcd.setCursor(7, 2);
      lcd.print(editableValue);
      lcd.print("] ");
      break;

    case NEW_MIN_HUMD:
      lcd.setCursor(7, 2);
      lcd.print(editableValue);
      lcd.print("] ");
      break;

    case NEW_MAX_HUMD:
      lcd.setCursor(7, 2);
      lcd.print(editableValue);
      lcd.print("] ");
      break;

    case STATE_NEW_WARN:
      
      switch (menuSelection){
        case 0:
          lcd.setCursor(0, 3);
          lcd.print(">");
          lcd.setCursor(10, 3);
          lcd.print(" ");
          break;

        case 1:
          lcd.setCursor(0, 3);
          lcd.print(" ");
          lcd.setCursor(10, 3);
          lcd.print(">");
          break;
      }
      break;

    case STATE_NEW_CONFIRM:
      switch (menuSelection){
        case 0:
          lcd.setCursor(0, 3);
          lcd.print(">");
          lcd.setCursor(10, 3);
          lcd.print(" ");
          break;

        case 1:
          lcd.setCursor(0, 3);
          lcd.print(" ");
          lcd.setCursor(10, 3);
          lcd.print(">");
          break;
      }
      break;

    case STATE_MENU_MAIN:
      
      switch (menuSelection){
        case 0:
          lcd.setCursor(0, 1);
          lcd.print(">");
          lcd.setCursor(0, 2);
          lcd.print(" ");
          break;

        case 1:
          lcd.setCursor(0, 1);
          lcd.print(" ");
          lcd.setCursor(0, 2);
          lcd.print(">");
          break;
      }
      break;
  
    case STATE_MENU_MODIFY:
      switch(menuSelection){
        case 0:
          lcd.setCursor(0, 1);
          lcd.print(">");
          lcd.setCursor(0, 2);
          lcd.print(" ");
          lcd.setCursor(0, 3);
          lcd.print(" ");
          break;

        case 1:
          lcd.setCursor(0, 1);
          lcd.print(" ");
          lcd.setCursor(0, 2);
          lcd.print(">");
          lcd.setCursor(0, 3);
          lcd.print(" ");
          break;

        case 2:
          lcd.setCursor(0, 1);
          lcd.print(" ");
          lcd.setCursor(0, 2);
          lcd.print(" ");
          lcd.setCursor(0, 3);
          lcd.print(">");
          break;
      }
      
      break;

      case STATE_EDIT_DIA:
        lcd.setCursor(15, 2);
        lcd.print(editableValue);
        lcd.print("]  ");
        break;
  
  
      case EDIT_TEMP_MIN:
        lcd.setCursor(15, 2);
        lcd.print(editableValue);
        lcd.print("]  ");       
        break;

      case EDIT_TEMP_MAX:
        lcd.setCursor(15, 2);
        lcd.print(editableValue);
        lcd.print("]  ");       
        break;
  
      case EDIT_HUMD_MIN:
        lcd.setCursor(15, 2);
        lcd.print(editableValue);
        lcd.print("]  ");       
        break;

      case EDIT_HUMD_MAX:
        lcd.setCursor(15, 2);
        lcd.print(editableValue);
        lcd.print("]  ");       
        break;
  }

}


//Calcula la temperatura y humedad
void calcTempHum(){
  prevH = h;
  prevT = t;
  h = dht.readHumidity();
  t = dht.readTemperature();
}

//Muestra en el LCD la temperatura y humedad
void showTempHum(){
  char mensaje[16];
  // Comprueba si la lectura falló. 
  if (!isnan(t)) { 
  sprintf(mensaje, "%.1f \337C ", t);
  lcd.setCursor(3, 1);
  lcd.print(mensaje); 
  }

  if (!isnan(h)) {
  sprintf(mensaje, " %.0f%% ", h);
  lcd.setCursor(15, 1);
  lcd.print(mensaje); 
  }
}


void startTimerLCD() {
  if (!timerLCDactive) {
    esp_timer_start_periodic(timer_lcd, 1000000);
    timerLCDactive = true;
    Serial.println("Timer LCD activo");
  }
}
void stopTimerLCD() {
  if (timerLCDactive) {
    esp_timer_stop(timer_lcd);
    timerLCDactive = false;
    Serial.println("Timer LCD apagado");
  }
}
//Iniciar timer del lcd
void startTimerDias() {
  if (!timerDiasactive) {
    esp_timer_start_periodic(contador_timer, 1000000);
    timerDiasactive = true;
    Serial.println("Timer días activo");
  }
}
void stopTimerDias() {
  if (timerDiasactive) {
    esp_timer_stop(contador_timer);
    timerDiasactive = false;
    Serial.println("Timer días apagado");
  }
}

void startTimerFirebase() {
  if (!timerFIREBASEactive) {
    esp_timer_start_periodic(timer_firebase, 10000000);
    timerFIREBASEactive = true;
    Serial.println("Timer Firebase activo");
  }
}
void stopTimerFirebase() {
  if (timerFIREBASEactive) {
    esp_timer_stop(timer_firebase);
    timerFIREBASEactive = false;
    Serial.println("Timer Firebase apagado");
  }
}

void startTimerMov() {
  if (!movActive) {
    esp_timer_start_periodic(timer_mov, 100000);
    movActive = true;
    Serial.println("Timer mov activo");
  }
}
void stopTimerMov() {
  if (movActive) {
    esp_timer_stop(timer_mov);
    movActive = false;
    Serial.println("Timer mov apagado");
  }
}



//Para mostrar que no queda tiempo restante
void showDiasCero(){
  lcd.setCursor(0, 3);
  lcd.print("00D 00H 00M 00S");
} 

//Para no mostrar el tiempo, para que parpadese
void showDiasClear(){
  lcd.setCursor(0, 3);
  lcd.print("                 ");
}

//Muestra el tiempo restante del contador
void showTiempoRes(){
  lcd.setCursor(0, 3);
  char mensaje[20];
  sprintf(mensaje, "%02luD %02luH %02luM %02luS", dias, horas, minutos, segundos);
  lcd.print(mensaje);
}

//Conectar al internet
//showLCD, para saber si necesita mostrar el aviso en LCD
void setup_wifi(bool showLCD) {
  Serial.println();
  Serial.print("Conectando a: ");
  Serial.println(ssid);

  if(showLCD) printConnectingWifi();

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n¡Conectado a Internet!");
}


//Para iniciar una nueva medición
void subirInfoGeneral(int newTempMin, int newTempMax, int newHumdMin, int newHumdMax, int newDias) {

  //Desactiva todos los monitoreos anteriores
  desactivarTodosLosMonitoreos();

  StaticJsonDocument<300> doc;

  doc["fecha_inicio"]     = generarFechaISO();
  doc["temp_limite_min"]  = newTempMin;
  doc["temp_limite_max"]  = newTempMax;
  doc["humd_limite_min"]  = newHumdMin;
  doc["humd_limite_max"]  = newHumdMax;
  doc["dias"]             = newDias;
  doc["activa"]           = true;  
  doc["temp"]             = -1; 
  doc["humd"]             = -1; 
  doc["mov"]              = false; 

  String json;
  serializeJson(doc, json);

  HttpTask *t = new HttpTask;

  t->endpoint = firebaseURL + "/monitoreos/" + idCaja + "/info_general.json";
  t->method   = "PUT";
  t->payload  = json;  

  xQueueSend(colaHTTP, &t, 0);
}


//Para obtener el json con todas las mediciones, necesito todos los id de las cajas para ponerlas en false
String obtenerTodosLosMonitoreos() {
  HTTPClient http;
  String url = firebaseURL + "/monitoreos.json";

  http.begin(url);
  int codigo = http.GET();

  if (codigo == 200) {
    String payload = http.getString();
    http.end();
    return payload;
  } else {
    Serial.println("Error al obtener monitoreos");
    http.end();
    return "";
  }
}

//Para obtener el json de los datos generales de una sesión ya iniciada/activa
String obtenerInfoGeneral() {
  HTTPClient http;

  String url = firebaseURL + "/monitoreos/" + idCaja + "/info_general.json";
  http.begin(url);

  int code = http.GET();
  if (code != 200) {
    Serial.print("Error GET info_general: ");
    Serial.println(code);
    http.end();
    return "";
  }

  String json = http.getString();
  http.end();
  return json;
}

void leerInfoGeneral() {
  String json = obtenerInfoGeneral();
  if (json == "") return;

  StaticJsonDocument<1000> doc;
  DeserializationError error = deserializeJson(doc, json);

  if (error) {
    Serial.println("Error parseando info_general");
    return;
  }

  JsonObject info = doc.as<JsonObject>();

  tiempo = info["dias"];
  tempInf = info["temp_limite_min"];
  tempSup = info["temp_limite_max"];
  humdInf = info["humd_limite_min"];
  humdSup = info["humd_limite_max"];
}

//Desactiva todas las mediciones activas. Después de esto, se debe de iniciar otra para que haya una activa
void desactivarTodosLosMonitoreos() {
  String json = obtenerTodosLosMonitoreos();
  if (json == "") return;

  //Reservar 5000 bytes para usarlos en el json
  StaticJsonDocument<5000> doc; 
  deserializeJson(doc, json);     //Convierte un String en un objeto JSON

  //Recorrer todos los IDs
  for (JsonPair caja : doc.as<JsonObject>()) {
    String auxCaja = caja.key().c_str();
    //.key obtiene el identificador 
    //.c_str es como un toString()

    //Hace el PATCH con la ruta adecuada, incluyendo el id de la caja obtenida
    String url = firebaseURL + "/monitoreos/" + auxCaja + "/info_general.json";

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int codigo = http.PATCH("{\"activa\": false}");     //Solicita que para esa caja, haga false en "activa"
    http.end();
  }
}


void httpTask(void * parameter) {
  HttpTask *tarea;

  for (;;) {  //Es como un while(true)
    if (xQueueReceive(colaHTTP, &tarea, portMAX_DELAY) == pdTRUE) {

      HTTPClient http;
      http.begin(tarea->endpoint);
      http.addHeader("Content-Type", "application/json");

      int code = -1;
      if (tarea->method == "PATCH") code = http.PATCH(tarea->payload);
      else if (tarea->method == "PUT") code = http.PUT(tarea->payload);
      else if (tarea->method == "POST") code = http.POST(tarea->payload);
      http.end();

      delete tarea;   // <-- liberar memoria
    }
  }
}

//Cambia los límites de temp en la firebase de la caja activa
void FiBaSetTemp(int newTempInf, int newTempSup){
  HttpTask *t = new HttpTask; 

  t->endpoint = firebaseURL + "/monitoreos/" + idCaja + "/info_general.json";
  t->payload = "{\"temp_limite_min\": " + String(newTempInf) + ", \"temp_limite_max\": " + String(newTempSup) + "}";
  t->method = "PATCH";

  xQueueSend(colaHTTP, &t, 0); 
}

//Actualiza el dato de temperatura y humedad actual
void FiBaUpdateCurrData(int newTemp, int newHumd) {
  HttpTask *t = new HttpTask; 

  t->endpoint = firebaseURL + "/monitoreos/" + idCaja + "/info_general.json";
  t->payload = "{\"temp\": " + String(newTemp) + ", \"humd\": " + String(newHumd) + "}";
  t->method = "PATCH";

  xQueueSend(colaHTTP, &t, 0); 
}

//Cambia los límites de humd en la firebase de la caja activa
void FiBaSetHumd(int newHumdInf, int newHumdSup){
  HttpTask *t = new HttpTask; 

  t->endpoint = firebaseURL + "/monitoreos/" + idCaja + "/info_general.json";
  t->payload = "{\"humd_limite_min\": " + String(newHumdInf) + ", \"humd_limite_max\": " + String(newHumdSup) + "}";
  t->method = "PATCH";

  xQueueSend(colaHTTP, &t, 0); 
}

//Cambia los días en la firebase de la caja activa
void FiBaSetDias(int newDias){
  HttpTask *t = new HttpTask; 

  t->endpoint = firebaseURL + "/monitoreos/" + idCaja + "/info_general.json";
  t->payload = "{\"dias\": " + String(newDias) + "}";
  t->method = "PATCH";

  xQueueSend(colaHTTP, &t, 0);
}

void FiBaEnviarMedicion(float temperatura, float humedad) {

  // Construir JSON
  StaticJsonDocument<200> doc;
  doc["temperatura"] = temperatura;
  doc["humedad"] = humedad;

  String json;
  serializeJson(doc, json);

  // Crear la tarea en heap
  HttpTask *t = new HttpTask;
  t->endpoint = firebaseURL + "/monitoreos/" + idCaja +
                "/datos_mediciones/" + generarFechaISO() + ".json";

  t->method = "PUT";
  t->payload = json;   // <-- esto es totalmente válido

  // Enviar a la cola
  xQueueSend(colaHTTP, &t, 0);
}
//Obtiene el id de la caja activa actualmente 
String obtenerCajaActiva() {
    String json = obtenerTodosLosMonitoreos();
    if (json == "") return "";

    Serial.println(json);

    StaticJsonDocument<6000> doc;
    DeserializationError error = deserializeJson(doc, json);
    if (error) {
        Serial.println("Error al parsear JSON");
        return "";
    }

    for (JsonPair caja : doc.as<JsonObject>()) {
      String idCaja = caja.key().c_str();

      //Obtengo el "activa" de esa caja/medición
      bool activa = caja.value()["info_general"]["activa"];

      //Esa caja es la activa
      if (activa) return idCaja;
      //Asumo que sólo hay una caja activa, esa será la activa
    }

    // Si ninguno tenía true
    Serial.println("No hay caja activa");
    return "";
}

//Obtener un string con la fecha
String generarFechaISO() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "1970-01-01T00:00:00Z"; // fallback
  }

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buffer);
}

//Para poder sincronizar con el horario de cdmx
//showLCD, para saber si necesita mostrar el aviso en LCD
void initTimeLocal(bool showLCD){
  
  if(showLCD) printSincrFecha();
  
  configTime(-6 * 3600, 0, "pool.ntp.org");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Esperando hora NTP...");
    delay(500);
  }
}

//Obtiene el ID siguiente. El formato es x_ID, entonces no puedo sólo sumar y ya
//Esta función obtiene el siguiente ID
String siguienteID(String idActual) {
  int guion = idActual.indexOf('_');  //Obtener la posición del _
  if (guion == -1) return "";         //Si no existe _

  String numeroStr = idActual.substring(0, guion); //ibtiene String del número

  int numero = numeroStr.toInt();   //Casteo

  numero++; //incrementar
  return String(numero) + "_ID";
}


void printConnectingWifi(){
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" __________________");
  lcd.setCursor(0, 1);
  lcd.print("|   Conectando a   |");
  lcd.setCursor(0, 2);
  lcd.print("|     red WiFi     |");
  lcd.setCursor(0, 3);
  lcd.print("|__________________|");
}

//Imprimir el aviso del inicio
void printInit(){
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" __________________");
  lcd.setCursor(0, 1);
  lcd.print("|       Kaan       |");
  lcd.setCursor(0, 2);
  lcd.print("| Cuidar y guardar |");
  lcd.setCursor(0, 3);
  lcd.print("|__________________|");
}

void printSincrFecha(){
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" __________________");
  lcd.setCursor(0, 1);
  lcd.print("|  Sincronizando   |");
  lcd.setCursor(0, 2);
  lcd.print("|     fechas       |");
  lcd.setCursor(0, 3);
  lcd.print("|__________________|");
}

void printSearchSesiones(){
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" __________________");
  lcd.setCursor(0, 1);
  lcd.print("|Buscando  sesiones|");
  lcd.setCursor(0, 2);
  lcd.print("|     activas      |");
  lcd.setCursor(0, 3);
  lcd.print("|__________________|");
}

void printSesionOn(){
  lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(" __________________");
    lcd.setCursor(0, 1);
    lcd.print("|   Sin sesiones   |");
    lcd.setCursor(0, 2);
    lcd.print("|activas, iniciando|");
    lcd.setCursor(0, 3);
    lcd.print("|__________________|");
}

void printSesionOff(){
  lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(" __________________");
    lcd.setCursor(0, 1);
    lcd.print("|Sesi");
    lcd.write(3);
    lcd.print("n  encontrada|");
    lcd.setCursor(0, 2);
    lcd.print("|  reanudando...   |");
    lcd.setCursor(0, 3);
    lcd.print("|__________________|");
}

//Imprime el estado actual
void printEstado(){

  String estadoNom;

  switch (currentState) {
    case STATE_SPLASH: estadoNom = "STATE_SPLASH"; break;
    case STATE_HOME: estadoNom = "STATE_HOME"; break;
    case STATE_NO_SESSION: estadoNom = "STATE_NO_SESSION"; break;
    case STATE_MENU_MAIN: estadoNom = "STATE_MENU_MAIN"; break;
    case STATE_MENU_MODIFY: estadoNom = "STATE_MENU_MODIFY"; break;
    case STATE_EDIT_DIA: estadoNom = "STATE_EDIT_DIA"; break;
    case STATE_NEW_WARN: estadoNom = "STATE_NEW_WARN"; break;
    case NEW_DIA: estadoNom = "NEW_DIA"; break;
    case NEW_MIN_TEMP: estadoNom = "NEW_MIN_TEMP"; break;
    case NEW_MAX_TEMP: estadoNom = "NEW_MAX_TEMP"; break;
    case NEW_MIN_HUMD: estadoNom = "NEW_MIN_HUMD"; break;
    case NEW_MAX_HUMD: estadoNom = "NEW_MAX_HUMD"; break;
    case STATE_NEW_CONFIRM: estadoNom = "STATE_NEW_CONFIRM"; break;
    case EDIT_TEMP_MIN: estadoNom = "EDIT_TEMP_MIN"; break;
    case EDIT_TEMP_MAX: estadoNom = "EDIT_TEMP_MAX"; break;
    case EDIT_HUMD_MIN: estadoNom = "EDIT_HUMD_MIN"; break;
    case EDIT_HUMD_MAX: estadoNom = "EDIT_HUMD_MAX"; break;
    case STATE_LIMIT: estadoNom = "STATE_LIMIT"; break;
    default: estadoNom = "DESCONOCIDO"; break;
  }

  Serial.print("Estado Actual: ");
  Serial.println(estadoNom);

}

//Imprime errores en el LCD
//Como imprime un error, deja un while true para dejar el programa atorado aquí
void showErrorLCD(int id){

  lcd.clear();
  lcd.setCursor(0, 0);
  switch(id){
    case 0:
      lcd.print("ERROR EN EL SENSOR DE");
      lcd.setCursor(0, 1);
      lcd.print("TEMP Y HUMD.");
      lcd.print("ID ERROR: 0");
      break;

      case 1:
      lcd.print("ERROR EN EL SENSOR ");
      lcd.setCursor(0, 1);
      lcd.print("ACELEROMETRO Y GIROS");
      lcd.print("ID ERROR: 1");
      break;

      default: return; break;
  }

  while(true);

}

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
//Falta la del MPU

#define DHTPIN 4                //Pin del sensor de temperatura y humedad
#define DHTTYPE DHT11           //Tipo del sensor (Porque hay como FHT11, DHT12, etc)

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
float h, t;
DHT dht(DHTPIN, DHTTYPE);


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
  
  if (segundos_restantes > 0) {
    segundos_restantes--;

    restante = segundos_restantes;
    dias = restante / 86400UL;
    restante %= 86400UL;
    horas = restante / 3600UL;
    restante %= 3600UL;
    minutos = restante / 60UL;
    restante %= 60UL;
    segundos = restante;

    dias_lcd = true;
    
  } else {
    parpadea = !parpadea;
    dias_end = true;
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

  switch(id){
    case 1: update_lcd = true; break;
    case 2: upload_firebase = true; break; 
  }

}

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
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" __________________");
  lcd.setCursor(0, 1);
  lcd.print("|       Kaan       |");
  lcd.setCursor(0, 2);
  lcd.print("| Cuidar y guardar |");
  lcd.setCursor(0, 3);
  lcd.print("|__________________|");
  delay(3000); 


  //Decidir el estado inicial después del inicio
  //Si es que podemos cargar memoria
  if (sessionActive) {
    currentState = STATE_HOME;
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
  
  //////Para actualizar el lcd////////////
  if (update_lcd) {
    update_lcd = false;
    calcTempHum();
    Serial.printf("T: %.2f ºC, H: %.2f%\n", t, h);

    switch(currentState){
      case STATE_HOME: showTempHum(); break;
    }
  }

  //////Para contar los días
  if (dias_lcd) {
    dias_lcd = false;
    switch(currentState){
      case STATE_HOME: showTiempoRes(); break;
    }
  }

  if(dias_end){
    dias_end = false;
    switch(currentState){
      case STATE_HOME: parpadea ? showDiasCero() : showDiasClear(); break;
    }
  }

  //////Para subir datos a firebase////////////
  if (upload_firebase) {
    upload_firebase = false;
    Serial.println("Upload a firebase");
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

    case STATE_LIMIT:        //Menú principal, muestra temp, humd y el tiempo restante
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


    case STATE_MENU_MAIN:
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

    case STATE_MENU_MODIFY:
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

    case EDIT_TEMP_MIN: 
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

    case EDIT_TEMP_MAX: 
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
      }
      if (backPressed) {
        currentState = EDIT_TEMP_MIN;
        editableValue = auxTempInf;
        needsRedraw = true;
      }
      break;

    case EDIT_HUMD_MIN: 
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

    case EDIT_HUMD_MAX: 
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
      }
      if (backPressed) {
        currentState = EDIT_HUMD_MIN;
        editableValue = auxHumdInf;
        needsRedraw = true;
      }
      break;

    case STATE_EDIT_DIA:
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
        currentState = STATE_HOME;
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = STATE_MENU_MODIFY;
        needsRedraw = true;
      }
      break;

    case STATE_NEW_WARN:
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

    case NEW_DIA: 
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

    case NEW_MIN_TEMP: 
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

    case NEW_MAX_TEMP: 
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

    case NEW_MIN_HUMD:
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

    case NEW_MAX_HUMD:
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

    case STATE_NEW_CONFIRM:
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
      lcd.print("CAJA 01 [ACTIVA]");
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
      lcd.print("Caja 02?"); 
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

//Calcula la temperatura y humedad
void calcTempHum(){
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

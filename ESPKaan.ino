/*
 * Maquina de Estados para Menu en LCD 20x4 con ESP32
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
 * REQUISITOS DEL USUARIO:
 * 1. Botones SIN resistencias externas -> Se usa INPUT_PULLUP.
 * 2. Splash screen de 3 segundos.
 * 3. Sin texto de navegación en la Línea 4.
 * 4. Sigue el flujo de menú proporcionado.
 */

// --- 1. LIBRERÍAS ---
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- 2. CONFIGURACIÓN HARDWARE ---

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

// --- 3. MAQUINA DE ESTADOS (States) ---
enum State {
  STATE_SPLASH,
  STATE_HOME,
  STATE_NO_SESSION,
  STATE_MENU_MAIN,
  STATE_MENU_MODIFY,
  STATE_EDIT_DIA,
  STATE_NEW_WARN,
  SET_DIA,
  SET_MIN_TEMP,
  SET_MIN_HUMD,
  STATE_NEW_CONFIRM,
  SET_MAX_TEMP,
  SET_MAX_HUMD
};
State currentState;

//Defino los chars especiales (á, é, í, ó, ú, ñ, Í)
                            //(0, 1, 2, 3, 4, 5, 6)
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
  B00000
};

byte e_tilde[8] = {
  B00110,
  B00000,
  B01110,
  B10001,
  B11111,
  B10000,
  B01110,
  B00000
};

byte i_tilde[8] = {
  B00111,
  B00000,
  B01100,
  B00100,
  B00100,
  B00100,
  B01110,
  B00000
};

byte o_tilde[8] = {
  B00110,
  B00000,
  B01110,
  B10001,
  B10001,
  B10001,
  B01110,
  B00000
};

byte u_tilde[8] = {
  B00110,
  B00000,
  B10001,
  B10001,
  B10001,
  B10011,
  B01101,
  B00000
};

byte enie[8] = {
  B01110,
  B00000,
  B10110,
  B11001,
  B10001,
  B10001,
  B10001,
  B00000
};

byte I_tilde[8] = {
  B01110,
  B00000,
  B01110,
  B00100,
  B00100,
  B00100,
  B01110,
  B00000
};

// --- 4. VARIABLES GLOBALES DE NAVEGACIÓN ---
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

// --- 5. INTERRUPCIÓN DEL ENCODER (ISR) ---
// Esta función se llama CADA vez que el pin CLK cambia
void IRAM_ATTR readEncoderISR() {
  // Lee el pin DT. Si es diferente a CLK, giramos en un sentido.
  if (digitalRead(ENCODER_DT_PIN) != digitalRead(ENCODER_CLK_PIN)) {
    encoderPos++;
  } else {
    encoderPos--;
  }
}

// --- 6. SETUP ---
void setup() {
  Serial.begin(115200);

  // Configurar Pines de Botones (¡IMPORTANTE!)
  // Usamos INPUT_PULLUP para que la ESP32 active una resistencia interna.
  // El pin estará en HIGH (1) por defecto.
  // Al presionar el botón (conectado a GND), el pin leerá LOW (0).
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
  pinMode(BOTON_BACK_PIN, INPUT_PULLUP);

  // Configurar Pines del Encoder
  pinMode(ENCODER_CLK_PIN, INPUT);
  pinMode(ENCODER_DT_PIN, INPUT);
  
  // Activar la interrupción en el pin CLK
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK_PIN), readEncoderISR, CHANGE);

  // Iniciar LCD
  lcd.init();
  lcd.backlight();
  
  lcd.createChar(0, a_tilde);
  lcd.createChar(1, e_tilde);
  lcd.createChar(2, i_tilde);
  lcd.createChar(3, o_tilde);
  lcd.createChar(4, u_tilde);
  lcd.createChar(5, enie);
  lcd.createChar(6, I_tilde);

  // ----- INICIO: SPLASH SCREEN -----
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
  delay(3000); // Pausa de 3 segundos
  // ----- FIN: SPLASH SCREEN -----

  // Decidir el estado inicial después del splash
  // (Aquí iría tu lógica para ver si hay una sesión guardada en memoria)
  if (sessionActive) {
    currentState = STATE_HOME;
  } else {
    currentState = STATE_NO_SESSION;
  }
  
  // Dibujar la primera pantalla
  drawScreen(); 
}

// --- 7. LOOP PRINCIPAL ---
void loop() {
  // 1. Revisa los inputs (botones y encoder)
  handleInputs();

  // 2. Si un input causó un cambio, la bandera estará activa
  if (okPressed || backPressed || encoderIncrement != 0) {
    // 3. Procesa el input según el estado actual
    handleStateLogic();
    
    // 4. Limpia las banderas
    okPressed = false;
    backPressed = false;
    encoderIncrement = 0;
  }
  
  // (El loop se repite muy rápido, pero solo redibuja la pantalla
  // cuando la lógica de `handleStateLogic` lo ordena)
}

// --- 8. MANEJO DE INPUTS (Lectura y Debounce) ---
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

// --- 9. MANEJO DE LÓGICA (El cerebro de la máquina de estados) ---
void handleStateLogic() {
  bool needsRedraw = false; // Solo redibuja si algo cambia

  switch (currentState) {
    
    case STATE_NO_SESSION:
      if (okPressed) {
        currentState = SET_DIA;
        menuSelection = tiempo;
        needsRedraw = true;
      }
      break;

    case STATE_HOME:
      if (okPressed) {
        currentState = STATE_MENU_MAIN;
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
          editableValue = 14; 
          menuSelection = 0;
        }
        else if (menuSelection == 1) { 
          currentState = STATE_MENU_MAIN;
          menuSelection = 0;
        } else {
          currentState = STATE_MENU_MAIN;
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

    case STATE_EDIT_DIA:
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < 1) editableValue = 1; 
        needsRedraw = false;
      }
      if (okPressed) {
        Serial.print("Nuevo limite de tiempo guardado: ");
        Serial.println(editableValue);
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
          currentState = SET_DIA;
          editableValue = tiempo; 
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

    case SET_DIA: 
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < 1) editableValue = 1;
        needsRedraw = false;
      }
      if (okPressed) {
        tiempo = editableValue; 
        currentState = SET_MIN_TEMP;
        editableValue = tempInf; 
        needsRedraw = true;
      }
      break;

    case SET_MIN_TEMP: 
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < 0) editableValue = 0;
        if (editableValue > 99) editableValue = 99;
        needsRedraw = false;
      }
      if (okPressed) {
        tempInf = editableValue; 
        currentState = SET_MAX_TEMP;
        tempSup = tempInf + 1;
        editableValue = tempSup;
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = SET_DIA;
        editableValue = tiempo;
        needsRedraw = true;
      }
      break;

    case SET_MAX_TEMP: 
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < tempInf + 1) editableValue = tempInf + 1;
        if (editableValue > 100) editableValue = 100;
        needsRedraw = false;
      }
      if (okPressed) {
        tempSup = editableValue; 
        currentState = SET_MIN_HUMD;
        editableValue = humdInf;
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = SET_MIN_TEMP;
        editableValue = tempInf;
        needsRedraw = true;
      }
      break;

    case SET_MIN_HUMD:
      if (encoderIncrement != 0) {
        editableValue += encoderIncrement;
        if (editableValue < 0) editableValue = 0;
        if (editableValue > 99) editableValue = 99;
        needsRedraw = false;
      }
      if (okPressed) {
        humdInf = editableValue;
        currentState = SET_MAX_HUMD;
        humdSup = humdInf + 1;
        editableValue = humdSup;
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = SET_MAX_TEMP;
        editableValue = tempSup;
        needsRedraw = true;
      }
      break;

    case SET_MAX_HUMD:
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
        currentState = SET_MIN_HUMD;
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
        } else { 
          currentState = SET_MAX_HUMD;
        }
        needsRedraw = true;
      }
      if (backPressed) {
        currentState = SET_MAX_HUMD;
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

    case STATE_HOME:
      // (Aquí pones los datos reales de tus sensores)
      lcd.print("CAJA 01 [ACTIVA]");
      lcd.setCursor(0, 1);
      lcd.print(" T: 25.1 C  H: 45%");
      lcd.setCursor(0, 2);
      lcd.print("Rest.: 13d:04h:20m");
      // L4 queda vacía
      break;

    case STATE_MENU_MAIN:
      lcd.print("== MEN");
      lcd.write(4);
      lcd.print(" PRINCIPAL ==");
      lcd.setCursor(0, 1);
      lcd.print("> Modificar L");
      lcd.write(2);
      lcd.print("mites");
      lcd.setCursor(0, 2);
      lcd.print("  Iniciar Medici");
      lcd.write(3);
      lcd.print("n");
      break;

    case STATE_MENU_MODIFY:
      lcd.print("== MODIFICAR L");
      lcd.write(6);
      lcd.print("MITES ==");
      lcd.setCursor(0, 1);
      lcd.print("> L");
      lcd.write(2);
      lcd.print("mite de Tiempo");
      lcd.setCursor(0, 2);
      lcd.print("  L");
      lcd.write(2);
      lcd.print("mite de Temp/hum");
      lcd.setCursor(0, 3);
      lcd.print("  < Atr");
      lcd.write(0);
      lcd.print("s"); 
      break;

    case STATE_EDIT_DIA:
      lcd.print("Modif Tiempo (d");
      lcd.write(2);
      lcd.print("as)");
      lcd.setCursor(0, 1);
      lcd.print("L");
      lcd.write(2);
      lcd.print("mite Actual: 14d"); // (Deberías usar el valor real)
      lcd.setCursor(0, 2);
      lcd.print("Nuevo L");
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

    case SET_DIA:
      lcd.print("NUEVA SESI");
      lcd.write(3);
      lcd.print("N (1/5)");
      lcd.setCursor(0, 1);
      lcd.print("Tiempo L");
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

      
    case SET_MIN_TEMP:
      lcd.print("NUEVA SESI");
      lcd.write(3);
      lcd.print("N (2/5)");
      lcd.setCursor(0, 1);
      lcd.print("L");
      lcd.write(2);
      lcd.print("mite Temp. M");
      lcd.write(2);
      lcd.print("n (C)");
      lcd.setCursor(0, 2);
      lcd.print("Temp: [");
      lcd.print(editableValue);
      lcd.print("]");
      break;

    case SET_MAX_TEMP:
      lcd.print("NUEVA SESI");
      lcd.write(3);
      lcd.print("N (3/5)");
      lcd.setCursor(0, 1);
      lcd.print("L");
      lcd.write(2);
      lcd.print("mite Temp. M");
      lcd.write(0);
      lcd.print("x (C)");
      lcd.setCursor(0, 2);
      lcd.print("Temp: [");
      lcd.print(editableValue);
      lcd.print("]");
      break;

    case SET_MAX_HUMD:
      lcd.print("NUEVA SESI");
      lcd.write(3);
      lcd.print("N (5/5)");
      lcd.setCursor(0, 1);
      lcd.print("L");
      lcd.write(2);
      lcd.print("mite Hum. M");
      lcd.write(0);
      lcd.print("x (%)");
      lcd.setCursor(0, 2);
      lcd.print("Humd: [");
      lcd.print(editableValue);
      lcd.print("]");
      break;

    case SET_MIN_HUMD:
      lcd.print("NUEVA SESI");
      lcd.write(3);
      lcd.print("N (5/5)");
      lcd.setCursor(0, 1);
      lcd.print("L");
      lcd.write(2);
      lcd.print("mite Hum. M");
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

    case SET_DIA:
      lcd.setCursor(7, 2);
      lcd.print(editableValue);
      lcd.print("] ");
      break;

    case SET_MIN_TEMP:
      lcd.setCursor(7, 2);
      lcd.print(editableValue);
      lcd.print("] ");
      break;

    case SET_MAX_TEMP:
      lcd.setCursor(7, 2);
      lcd.print(editableValue);
      lcd.print("] ");
      break;

    case SET_MIN_HUMD:
      lcd.setCursor(7, 2);
      lcd.print(editableValue);
      lcd.print("] ");
      break;

    case SET_MAX_HUMD:
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
          lcd.print("  < Atr");
          lcd.write(0);
          lcd.print("s");
          break;

        case 1:
          lcd.setCursor(0, 1);
          lcd.print(" ");
          lcd.setCursor(0, 2);
          lcd.print(">");
          lcd.setCursor(0, 3);
          lcd.print("  < Atr");
          lcd.write(0);
          lcd.print("s");
          break;

        case 2:
          lcd.setCursor(0, 1);
          lcd.print(" ");
          lcd.setCursor(0, 2);
          lcd.print(" ");
          lcd.setCursor(0, 3);
          lcd.print("> Atr");
          lcd.write(0);
          lcd.print("s     "); 
          break;
      }
      
      break;

      case STATE_EDIT_DIA:
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
    case SET_DIA: estadoNom = "SET_DIA"; break;
    case SET_MIN_TEMP: estadoNom = "SET_MIN_TEMP"; break;
    case SET_MAX_TEMP: estadoNom = "SET_MAX_TEMP"; break;
    case SET_MIN_HUMD: estadoNom = "SET_MIN_HUMD"; break;
    case SET_MAX_HUMD: estadoNom = "SET_MAX_HUMD"; break;
    case STATE_NEW_CONFIRM: estadoNom = "STATE_NEW_CONFIRM"; break;
    default: estadoNom = "DESCONOCIDO"; break;
  }

  Serial.print("Estado Actual: ");
  Serial.println(estadoNom);

}


/* Codigo perteneciente al Arduino Uno */
// Esclavo, porque se encarga de recolectar todos los datos para mandárselos al ESP32

#include <DHT.h>

// --- 1. DEFINICIONES DE PINES Y SENSORES ---
#define DHTPIN 8          // Pin conectado al DHT11
#define DHTTYPE DHT11     // Tipo de sensor
#define LM35_PIN A1       // Pin analógico conectado al LM35

#define RELAY_PIN 4       // Pin conectado a la señal (IN) del Módulo Relé
#define LED_HOT_PIN 7     // Pin conectado al led rojo (ambiente caliente)
#define LED_OK_PIN 13     // LED que indica temperatura bajo el umbral

// --- 2. CONFIGURACIÓN DEL RELÉ ---
// Conexión COM/NO: Necesita LOW en la señal para encender.
#define RELAY_ON LOW      // Señal para ENCENDER el ventilador (Pin 4 bajo)
#define RELAY_OFF HIGH    // Señal para APAGAR el ventilador (Pin 4 alto)

// --- 3. UMBRAL DE CONTROL Y CONVERSIONES ---
#define UMBRAL_TEMPERATURA 32.0          // Umbral de temperatura: 32.0 °C
#define VREF_CONVERSION_FACTOR (500.0 / 1024.0)  // Conversión LM35

// --- 4. OBJETOS ---
DHT dht(DHTPIN, DHTTYPE); 

// --- 5. ESTADO DE CONTROL REMOTO ---
bool overrideRemoto = false;       // Si true, el ESP32 manda
bool overrideVentiladorOn = false; // Estado forzado del ventilador

// --- 6. FILTRO PARA LM35 ---
int ultimaLecturaCrudaLM35 = -1;

// --- 7. FUNCIONES AUXILIARES ---

// Aplica el estado físico del ventilador y de los LEDs
void aplicarEstadoSalida(bool ventiladorEncendido) {
  // Relé del ventilador
  digitalWrite(RELAY_PIN, ventiladorEncendido ? RELAY_ON : RELAY_OFF);

  // LED rojo: ambiente caliente -> se enciende cuando el ventilador está encendido
  digitalWrite(LED_HOT_PIN, ventiladorEncendido ? HIGH : LOW);

  // LED OK: ambiente bajo umbral -> encendido cuando el ventilador está apagado
  digitalWrite(LED_OK_PIN, ventiladorEncendido ? LOW : HIGH);
}

// Procesa comandos de texto enviados por el ESP32 a través de Serial
void procesarComandosDesdeESP32() {
  while (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.length() == 0) {
      return;
    }

    if (cmd.equalsIgnoreCase("VENT_ON")) {
      overrideRemoto = true;
      overrideVentiladorOn = true;
    } 
    else if (cmd.equalsIgnoreCase("VENT_OFF")) {
      overrideRemoto = true;
      overrideVentiladorOn = false;
    } 
    else if (cmd.equalsIgnoreCase("VENT_AUTO")) {
      overrideRemoto = false;  // Vuelve a modo automático
    }
  }
}

void setup() {
  Serial.begin(9600);      // Comunicación serie con el ESP32 o PC
  dht.begin();             // Inicialización del DHT11

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_HOT_PIN, OUTPUT);
  pinMode(LED_OK_PIN, OUTPUT);

  // Estado inicial: ventilador apagado, LED OK encendido
  aplicarEstadoSalida(false);

  Serial.println("---- INICIO SISTEMA TEMP ----");
}

void loop() {
  // --- A. PROCESAR POSIBLES COMANDOS DEL ESP32 ---
  procesarComandosDesdeESP32();

  // --- B. LECTURA Y CONVERSIÓN DE SENSORES ---
  // LM35
  int lecturaCrudaLM35 = analogRead(LM35_PIN); 

  if (lecturaCrudaLM35 == 0 && ultimaLecturaCrudaLM35 > 0) {
    lecturaCrudaLM35 = ultimaLecturaCrudaLM35;
  } else if (lecturaCrudaLM35 > 0) {
    ultimaLecturaCrudaLM35 = lecturaCrudaLM35;
  }

  float tempLM35 = lecturaCrudaLM35 * VREF_CONVERSION_FACTOR; 

  // DHT11
  float humDHT  = dht.readHumidity();     
  float tempDHT = dht.readTemperature(); 
  bool dht_valido = !isnan(tempDHT) && !isnan(humDHT);

  // DEBUG DEL DHT
  if (!dht_valido) {
    Serial.println("DHT ERROR: lectura invalida (NaN)");
  } else {
    Serial.print("DHT OK -> Temp: ");
    Serial.print(tempDHT);
    Serial.print(" °C  | Humedad: ");
    Serial.print(humDHT);
    Serial.println(" %");
  }

  // --- C. LÓGICA DE CONTROL DEL RELÉ / LEDS ---
  bool ventiladorEncendido = false;

  if (overrideRemoto) {
    // MODO REMOTO: el ESP32 manda
    ventiladorEncendido = overrideVentiladorOn;
  } else {
    // MODO AUTOMÁTICO: según temperaturas medidas
    bool tempAltaLM35 = (tempLM35 > UMBRAL_TEMPERATURA);
    bool tempAltaDHT  = (dht_valido && (tempDHT > UMBRAL_TEMPERATURA));

    ventiladorEncendido = (tempAltaLM35 || tempAltaDHT);
  }

  aplicarEstadoSalida(ventiladorEncendido);


  delay(2000);  // DHT11 necesita ~2s entre lecturas
}

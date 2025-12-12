/* Codigo perteneciente al Arduino Uno */
// Esclavo, porque se encarga de recolectar todos los datos para mandárselos al ESP32

#include <DHT.h>

// --- DEFINICION DE PINES CON SUS SENSORES ---
#define DHTPIN 8          // Sensor de temperatura y humeda DHT11
#define DHTTYPE DHT11     // Tipo de sensor
#define LM35_PIN A1       // Sensor de temperatura LM35 [Pin Analogico]

#define RELAY_PIN 4       // Señal del Relé
#define LED_HOT_PIN 13    // LED indicador de temperaturas altas (Amarillo)
#define LED_OK_PIN 7      // LED indicador de temperaturas bajas/normales (Azul)

// --- CONFIGURACIÓN DEL RELE ---
// Conexion usada del Rele COM/NO (siempre cerrado)
#define RELAY_ON LOW      // Señal para ENCENDER el ventilador y LED Amarillo
#define RELAY_OFF HIGH    // Señal para APAGAR el ventilador y LED Azul

// --- UMBRAL DE TEMPERATURA Y FORMULA DE TEMPERATURA ---
// Umbral de temperatura: 32.0 °C
#define UMBRAL_TEMPERATURA 32.0
#define FORMULA_LM35 (500.0 / 1024.0)  // Conversión LM35

// --- OBJETOS ---
DHT dht(DHTPIN, DHTTYPE); 

// --- CONTROL REMOTO VENTILADOR ---
// Si overrideRemoto es true, el ESP32 está forzando el estado del ventilador.
bool overrideRemoto = false;
bool overrideVentiladorOn = false;

// Para filtrar lecturas 0 del LM35
int ultimaLecturaCrudaLM35 = -1;

// --- FUNCIONES AUXILIARES ---

// Aplica el estado físico del ventilador y de los LEDs
void aplicarEstadoSalida(bool ventiladorEncendido) {
  // Relé del ventilador (activo en LOW)
  digitalWrite(RELAY_PIN, ventiladorEncendido ? RELAY_ON : RELAY_OFF);

  // LED de ambiente caliente: encendido cuando el ventilador está encendido
  digitalWrite(LED_HOT_PIN, ventiladorEncendido ? HIGH : LOW);

  // LED de ambiente frío/OK: encendido cuando el ventilador está apagado
  digitalWrite(LED_OK_PIN, ventiladorEncendido ? LOW : HIGH);
}

// Procesa comandos de texto enviados por el ESP32 a través de Serial:
//  - "VENT_ON"   -> forzar ventilador encendido
//  - "VENT_OFF"  -> forzar ventilador apagado
//  - "VENT_AUTO" -> volver a modo automático (según temperatura)

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
      overrideRemoto = false;  // Vuelve al control automático por temperatura
    }
  }
}

void setup() {
  Serial.begin(9600);// <================ SERIAL 9600
  dht.begin();

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_HOT_PIN, OUTPUT);
  pinMode(LED_OK_PIN, OUTPUT);

  aplicarEstadoSalida(false);
}

void loop() {
  // --- PROCESAR POSIBLES COMANDOS DEL ESP32 ---
  procesarComandosDesdeESP32();

  // --- LECTURA Y CONVERSIÓN DE SENSORES ---
  int lecturaCrudaLM35 = analogRead(LM35_PIN); 

  // Filtro para eliminar los ceros en lecturas del LM35
  if (lecturaCrudaLM35 == 0 && ultimaLecturaCrudaLM35 > 0) {
    lecturaCrudaLM35 = ultimaLecturaCrudaLM35;
  } else if (lecturaCrudaLM35 > 0) {
    ultimaLecturaCrudaLM35 = lecturaCrudaLM35;
  }

  float tempLM35 = lecturaCrudaLM35 * FORMULA_LM35; 
  
  float tempDHT = dht.readTemperature(); 
  float humDHT = dht.readHumidity();     
  bool dht_valido = !isnan(tempDHT);

  // --- LoGICA DE CONTROL DEL RELÉ / LEDS ---
  bool ventiladorEncendido = false;

  if (overrideRemoto) {
    // MODO REMOTO: el ESP32 manda
    ventiladorEncendido = overrideVentiladorOn;
  } else {
    // MODO AUTOMÁTICO: según temperaturas medidas
    bool tempAltaLM35 = (tempLM35 > UMBRAL_TEMPERATURA);
    bool tempAltaDHT  = (dht_valido && (tempDHT > UMBRAL_TEMPERATURA));

    // El ventilador se enciende si:
    // 1. Alguna de las dos temperaturas supera el umbral de 32 °C.
    ventiladorEncendido = (tempAltaLM35 || tempAltaDHT);
  }

  // Aplicar el estado calculado al relé y LEDs
  aplicarEstadoSalida(ventiladorEncendido);

  // --- ENVÍO DE DATOS LIMPIOS AL ESP32 ---
  // Formato: tempDHT,humedad,tempLM35
  Serial.print(tempDHT, 2); 
  Serial.print(",");
  Serial.print(humDHT, 2);  
  Serial.print(",");
  Serial.println(tempLM35, 2); 


  // // --- Seccion de depuracion de datos ---
  // Serial.println("---------------------");
  // Serial.println("DHT11 [°C]:  ");
  // Serial.println(tempDHT, 2);
  // Serial.println("Humedad [%]:     ");
  // Serial.println(humDHT, 2);
  // Serial.println("LM35 [°C]:   ");
  // Serial.println(tempLM35, 2);
  // Serial.println("-------[CRUDO]--------");
  // Serial.println("LM35 [°C]:   ");
  // Serial.println(lecturaCrudaLM35, 2);
  // Serial.println("---------------------");

  delay(2000); 
}

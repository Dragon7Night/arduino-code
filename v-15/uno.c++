/* Codigo perteneciente al Arduino Uno */
// Esclavo: se encarga de leer sensores y hablar con el ESP32

#include <DHT.h>
#include <SoftwareSerial.h>

// --- SERIAL CON EL ESP32 ---
// Pin 2 -> RX del UNO (recibe desde TXD2 del ESP32)
// Pin 3 -> TX del UNO (envía hacia RXD2 del ESP32)
SoftwareSerial espSerial(2, 3); // RX, TX

// --- DEFINICION DE PINES CON SUS SENSORES ---
#define DHTPIN    8        // Sensor de temperatura y humedad DHT11
#define DHTTYPE   DHT11
#define LM35_PIN  A1       // Sensor de temperatura LM35 (analógico)

#define RELAY_PIN    4     // Relé del ventilador
#define LED_HOT_PIN  13    // LED: ambiente caliente (ventilador encendido)
#define LED_OK_PIN   7     // LED: ambiente normal (ventilador apagado)

// Buzzer para avisar cambio de estado del ventilador
#define BUZZER_PIN   12

// --- CONFIGURACIÓN DEL RELÉ ---
// En TU cableado actual: HIGH = ventilador ENCENDIDO, LOW = APAGADO
#define RELAY_ON   HIGH
#define RELAY_OFF  LOW

// --- UMBRAL DE TEMPERATURA Y FORMULA DEL LM35 ---
#define UMBRAL_TEMPERATURA  32.0
#define FORMULA_LM35        (500.0 / 1024.0)

// --- OBJETOS ---
DHT dht(DHTPIN, DHTTYPE);

// --- MODO DEL VENTILADOR ---
//  - AUTO: decide por temperatura
//  - REMOTO_ON: forzado encendido (desde la app)
//  - REMOTO_OFF: forzado apagado (desde la app)
enum ModoVentilador {
  MODO_AUTO = 0,
  MODO_REMOTO_ON,
  MODO_REMOTO_OFF
};

ModoVentilador modoVentilador = MODO_AUTO;

// Para filtrar lecturas 0 del LM35
int  ultimaLecturaCrudaLM35 = -1;

// Para detectar cambios de estado y hacer "bip"
bool estadoVentiladorAnterior = false;

// Último comando recibido (debug)
String ultimoComandoRecibido = "";

// --------------------------------------------------
// Enciende/apaga ventilador y LEDs según el estado
// --------------------------------------------------
void aplicarEstadoSalida(bool ventiladorEncendido) {
  // Relé
  digitalWrite(RELAY_PIN, ventiladorEncendido ? RELAY_ON : RELAY_OFF);

  // LED ambiente caliente (ventilador encendido)
  digitalWrite(LED_HOT_PIN, ventiladorEncendido ? LOW : HIGH);

  // LED ambiente OK (ventilador apagado)
  digitalWrite(LED_OK_PIN, ventiladorEncendido ? HIGH : LOW);
}

// --------------------------------------------------
// Pequeño "chime" con el buzzer cuando cambia de estado
// --------------------------------------------------
void beepCambioEstado() {
  const uint16_t notes[] = { 988, 1319, 1568, 2093, 1568, 1319, 988 };
  const uint16_t dur[]   = {  70,   70,   80,   130,  70,   80,  170 };

  for (uint8_t i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
    tone(BUZZER_PIN, notes[i], dur[i]);
    delay((int)(dur[i] * 1.3)); // pequeño espacio entre notas
    noTone(BUZZER_PIN);
  }
}

// --------------------------------------------------
// Devuelve el modo actual como texto (para debug)
// --------------------------------------------------
const char* textoModo(ModoVentilador m) {
  switch (m) {
    case MODO_AUTO:        return "AUTO";
    case MODO_REMOTO_ON:   return "REMOTO_ON";
    case MODO_REMOTO_OFF:  return "REMOTO_OFF";
    default:               return "DESCONOCIDO";
  }
}

// --------------------------------------------------
// Procesa comandos enviados por el ESP32 (VENT_ON/OFF/AUTO)
// --------------------------------------------------
void procesarComandosDesdeESP32() {
  if (espSerial.available()) {
    String cmd = espSerial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    ultimoComandoRecibido = cmd;

    Serial.print("[CMD] Recibido desde ESP32: '");
    Serial.print(cmd);
    Serial.println("'");

    cmd.toUpperCase();

    if (cmd == "VENT_ON") {
      modoVentilador = MODO_REMOTO_ON;
    }
    else if (cmd == "VENT_OFF") {
      modoVentilador = MODO_REMOTO_OFF;
    }
    else if (cmd == "VENT_AUTO") {
      modoVentilador = MODO_AUTO;
    }
    else {
      Serial.println("[CMD] Comando no reconocido, se ignora.");
    }

    Serial.print("[CMD] Modo actual ahora: ");
    Serial.println(textoModo(modoVentilador));
  }
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------
void setup() {
  // Serial para debug por USB
  Serial.begin(9600);

  // Serial para hablar con el ESP32 (pines 2 y 3)
  espSerial.begin(9600);

  dht.begin();

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_HOT_PIN, OUTPUT);
  pinMode(LED_OK_PIN, OUTPUT);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  aplicarEstadoSalida(false);
  estadoVentiladorAnterior = false;
  modoVentilador = MODO_AUTO;  // arranca en automático

  Serial.println("=== Arduino listo. Modo inicial: AUTO ===");
}

// --------------------------------------------------
// LOOP PRINCIPAL
// --------------------------------------------------
void loop() {
  // 1) Leer comandos del ESP32 (si los hay)
  procesarComandosDesdeESP32();

  // 2) Leer sensores
  int lecturaCrudaLM35 = analogRead(LM35_PIN);

  // Filtro para lecturas 0 raras del LM35
  if (lecturaCrudaLM35 == 0 && ultimaLecturaCrudaLM35 > 0) {
    lecturaCrudaLM35 = ultimaLecturaCrudaLM35;
  } else if (lecturaCrudaLM35 > 0) {
    ultimaLecturaCrudaLM35 = lecturaCrudaLM35;
  }

  float tempLM35 = lecturaCrudaLM35 * FORMULA_LM35;
  float tempDHT  = dht.readTemperature();
  float humDHT   = dht.readHumidity();
  bool  dht_valido = !isnan(tempDHT);

  // 3) Decidir si el ventilador debe ir encendido/apagado
  bool ventiladorEncendido = false;

  // El modo remoto SIEMPRE tiene prioridad sobre la temperatura
  if (modoVentilador == MODO_REMOTO_ON) {
    ventiladorEncendido = true;   // forzado encendido
  }
  else if (modoVentilador == MODO_REMOTO_OFF) {
    ventiladorEncendido = false;  // forzado apagado
  }
  else {
    // MODO_AUTO: decide por temperatura
    bool tempAltaLM35 = (tempLM35 > UMBRAL_TEMPERATURA);
    bool tempAltaDHT  = (dht_valido && (tempDHT > UMBRAL_TEMPERATURA));
    ventiladorEncendido = (tempAltaLM35 || tempAltaDHT);
  }

  // 4) Si cambió el estado del ventilador, "bip" + log
  if (ventiladorEncendido != estadoVentiladorAnterior) {
    beepCambioEstado();
    estadoVentiladorAnterior = ventiladorEncendido;

    Serial.print("[ESTADO] Ventilador ahora: ");
    Serial.print(ventiladorEncendido ? "ENCENDIDO" : "APAGADO");
    Serial.print(" | Modo: ");
    Serial.println(textoModo(modoVentilador));
  }

  // 5) Aplicar cambios al relé y LEDs
  aplicarEstadoSalida(ventiladorEncendido);

  // 6) Enviar datos al ESP32 (por SoftwareSerial, NO por USB)
  // Formato: tempDHT,humedad,tempLM35
  espSerial.print(tempDHT, 2);
  espSerial.print(",");
  espSerial.print(humDHT, 2);
  espSerial.print(",");
  espSerial.println(tempLM35, 2);

  // Debug local de sensores
  Serial.print("[DATA] DHT=");
  Serial.print(tempDHT, 2);
  Serial.print("C, HUM=");
  Serial.print(humDHT, 2);
  Serial.print("%, LM35=");
  Serial.print(tempLM35, 2);
  Serial.println("C");

  delay(2000);
}

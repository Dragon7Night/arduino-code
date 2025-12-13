/* Codigo perteneciente al ESP32 */
// Maestro, porque mantiene toda la comunicacion con el entorno de manera centralizada
// Es como la puerta entre la parte física y digital que existe

#include <WiFi.h>
#include <Firebase_ESP_Client.h> 
#include <time.h>   // <- Para manejar fecha/hora de las lecturas

// --- 1. CONFIGURACIÓN SERIAL Y WIFI ---
// PINES
#define RXD2 16
#define TXD2 17

// CREDENCIALES WIFI
const char* ssid = "arduino"; 
const char* password = "qwerty777"; 

// --- 2. CREDENCIALES DE FIREBASE (RTDB) ---
// Link de la RealTime DataBase perteneciente a FireBase
#define FIREBASE_HOST "https://tempcooldb-default-rtdb.firebaseio.com/"
// Token de seguridad necesario para hacer la autentificacion con la DataBase
#define FIREBASE_AUTH "EX1Zj6RRzGtekegHWkjnjYBh0EgGlzlqIxXb3yk3"

// --- CONTROL DE REINTENTOS WIFI ---
unsigned long ultimoIntentoWifi = 0;
const unsigned long INTERVALO_REINTENTO_WIFI = 10000; // 10 segundos entre intentos

// --- ZONA HORARIA CHILE (UTC-3) ---
const long GMT_OFFSET_CHILE       = -3 * 3600;  // Chile continental (UTC-3)
const int  DAYLIGHT_OFFSET_CHILE  = 0;          // Sin horario de verano automático

// LIBRERIAS NECESARIAS
FirebaseData fbdo; 
FirebaseAuth auth; 
FirebaseConfig config; 

bool firebaseInitialized = false;

// Para evitar reenviar el mismo comando muchas veces
String ultimoComandoVentilador = "";

// --- ESTRUCTURA PARA LOG OFFLINE ---
// Guarda lecturas cuando no hay conexión para subirlas después
struct LecturaOffline {
  float tempDHT;
  float humDHT;
  float tempLM35;
  time_t timestamp;   // Fecha/hora en la que se tomó la lectura
};

const int MAX_LOG_LECTURAS = 50;
LecturaOffline logLecturas[MAX_LOG_LECTURAS];
int logLecturasCount = 0;

// --- FUNCIONES AUXILIARES PARA TIEMPO Y LOG ---

// Formatea un time_t a texto "YYYY-MM-DD HH:MM:SS"
String formatearTimestamp(time_t t) {
  if (t == 0) return "sin_fecha";
  struct tm timeinfo;
  localtime_r(&t, &timeinfo);
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

// Configura la hora NTP usando zona horaria de Chile
void configurarHoraChile() {
  // Configuración de zona horaria (Chile continental UTC-3)
  configTime(GMT_OFFSET_CHILE, DAYLIGHT_OFFSET_CHILE,
             "pool.ntp.org", "time.nist.gov");

  Serial.println("[NTP] Solicitando hora para Chile (UTC-3)...");
  time_t now = 0;
  int reintentos = 0;

  // Esperar hasta 15 seg a que llegue una hora razonable (~año 2023 en adelante)
  do {
    delay(1000);
    now = time(nullptr);
    Serial.print(".");
    reintentos++;
  } while (now < 1700000000 && reintentos < 15);

  Serial.println();

  if (now >= 1700000000) {
    Serial.print("[NTP] Hora sincronizada: ");
    Serial.println(formatearTimestamp(now));
  } else {
    Serial.println("[NTP] No se pudo obtener la hora todavía. Se seguirá intentando si es necesario.");
  }
}

// Guarda una lectura en el log offline cuando no se puede subir a Firebase
void guardarLogOffline(float tempDHT, float humDHT, float tempLM35, time_t ts) {
  if (logLecturasCount >= MAX_LOG_LECTURAS) {
    // Si el log está lleno, se elimina la más antigua (posición 0) y se corre todo
    for (int i = 1; i < MAX_LOG_LECTURAS; i++) {
      logLecturas[i - 1] = logLecturas[i];
    }
    logLecturasCount = MAX_LOG_LECTURAS - 1;
  }

  logLecturas[logLecturasCount].tempDHT = tempDHT;
  logLecturas[logLecturasCount].humDHT = humDHT;
  logLecturas[logLecturasCount].tempLM35 = tempLM35;
  logLecturas[logLecturasCount].timestamp = ts;
  logLecturasCount++;

  Serial.print("[LOG] Lectura almacenada offline. Total del log: ");
  Serial.println(logLecturasCount);
}

// Envía una lectura concreta a Firebase (/lecturas + /historial)
bool enviarLecturaAFirebase(float tempDHT, float humDHT, float tempLM35, time_t ts) {
  if (!firebaseInitialized || !Firebase.ready()) {
    Serial.println("[RTDB] Firebase NO iniciada");
    return false;
  }

  String timestampStr = formatearTimestamp(ts);

  Serial.println("\n[RTDB] Enviando datos...");

  // Actualizar nodo /lecturas (datos en tiempo real)
  bool okLecturas =
    Firebase.RTDB.setFloat(&fbdo, "/lecturas/temp_dht", tempDHT) &&
    Firebase.RTDB.setFloat(&fbdo, "/lecturas/humedad", humDHT) &&
    Firebase.RTDB.setFloat(&fbdo, "/lecturas/temp_lm35", tempLM35) &&
    Firebase.RTDB.setString(&fbdo, "/lecturas/ultima_actualizacion", timestampStr);

  // Registrar nodo en /historial con push automático
  FirebaseJson json;
  json.set("temp_dht", tempDHT);
  json.set("humedad", humDHT);
  json.set("temp_lm35", tempLM35);
  json.set("timestamp", timestampStr);

  bool okHist = Firebase.RTDB.pushJSON(&fbdo, "/historial", &json);

  if (okLecturas && okHist) {
    Serial.println("[RTDB] Log de datos enviado");
    return true;
  } else {
    Serial.print("[RTDB] ERROR al enviar datos: ");
    Serial.println(fbdo.errorReason());
    return false;
  }
}

// Intenta vaciar el log offline cuando Firebase está disponible
void intentarEnviarLogHistorial() {
  if (!firebaseInitialized || !Firebase.ready()) return;
  if (logLecturasCount == 0) return;

  Serial.print("[log] Intentando enviar ");
  Serial.print(logLecturasCount);
  Serial.println(" lecturas pendientes...");

  int i = 0;
  while (i < logLecturasCount) {
    LecturaOffline &lec = logLecturas[i];

    bool exito = enviarLecturaAFirebase(
      lec.tempDHT,
      lec.humDHT,
      lec.tempLM35,
      lec.timestamp
    );

    if (exito) {
      // Si se envió bien, eliminamos esa lectura del log
      for (int j = i + 1; j < logLecturasCount; j++) {
        logLecturas[j - 1] = logLecturas[j];
      }
      logLecturasCount--;
    } else {
      Serial.println("[log] Fallo al enviar una lectura del buffer, se descarta para no bloquear.");
      // La descartamos igual para que el buffer no quede pegado
      for (int j = i + 1; j < logLecturasCount; j++) {
        logLecturas[j - 1] = logLecturas[j];
      }
      logLecturasCount--;
      // No incrementamos i porque ya corrimos el array
    }
  }

  Serial.println("[log] Buffer offline vaciado.");
}


// Inicializa Firebase y la hora 
void inicializarFirebaseYHora() {
  Serial.println("[START] Intentando inicializar");

  // Configuracion de FireBase 🔥 (Realtime Database)
  config.database_url = FIREBASE_HOST;
  auth.token.uid = ""; 
  config.signer.tokens.legacy_token = FIREBASE_AUTH; 

  // Inicializar Firebase
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true); 

  delay(100); // pequeño margen

  if (Firebase.ready()) {
    firebaseInitialized = true;
    Serial.println("[FIREBASE] Inicialización Exitosa.");
    
    // Señal de vida
    Firebase.RTDB.setString(&fbdo, "/sensores/senal", "ESP32 conectado y listo");

    // Configurar hora por NTP con zona horaria de Chile
    configurarHoraChile();

  } else {
    firebaseInitialized = false;
    Serial.println("[FIREBASE] ERROR en la inicialización o autenticación.");
    Serial.println(fbdo.errorReason()); 
  }
}

// --- SETUP ---
void setup() {
  Serial.begin(115200); 
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2); 

  Serial.println("Intentando conectar a WiFi...");

  // Conexión WiFi inicial
  WiFi.begin(ssid, password);
  long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < 20000) { 
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[EXITO] Conectado al WiFi");

    // Inicializar Firebase + hora
    inicializarFirebaseYHora();

  } else {
    Serial.println("\n[FALLO] No se pudo conectar al WiFi");
  }
  
  Serial.println("Esperando datos del Arduino...");
}

// --- Función: leer comando desde Firebase y reenviarlo al Arduino ---
void manejarControlVentiladorDesdeRTDB() {
  const char* pathCmd = "/control/ventilador_cmd";

  // Leemos SIEMPRE el valor del nodo
  if (!Firebase.RTDB.getString(&fbdo, pathCmd)) {
    Serial.print("[CTRL] Error al leer comando RTDB: ");
    Serial.println(fbdo.errorReason());
    return;
  }

  String cmd = fbdo.to<String>();
  cmd.trim();

  Serial.print("[CTRL] Valor leído en RTDB: '");
  Serial.print(cmd);
  Serial.println("'");

  if (cmd.length() == 0) {
    return;
  }

  // Solo reenviar si el comando cambió
  if (cmd != ultimoComandoVentilador) {
    Serial.print("[CTRL] Nuevo comando (enviado al Arduino): ");
    Serial.println(cmd);

    // El Arduino espera: "VENT_ON", "VENT_OFF" o "VENT_AUTO"
    Serial2.println(cmd);

    ultimoComandoVentilador = cmd;
  }
}

void loop() {
  // --- Verificación básica de conectividad ---
  wl_status_t wifiStatus = WiFi.status();
  bool wifiOK = (wifiStatus == WL_CONNECTED);

  // Reintento controlado de WiFi (cada 10 segundos)
  static bool wifiEstabaOK = false;

  if (!wifiOK) {
    if (millis() - ultimoIntentoWifi > INTERVALO_REINTENTO_WIFI) {
      Serial.print("[WiFi] Estado actual: ");
      Serial.println((int)wifiStatus);
      Serial.println("[WiFi] Conexion perdida, intentando reconectar...");

      WiFi.disconnect();              // Cortar intento anterior
      WiFi.begin(ssid, password);     // Volver a iniciar conexión
      ultimoIntentoWifi = millis();
    }
  } else {
    // Detectar momento en que vuelve la conexión
    if (!wifiEstabaOK) {
      Serial.println("[WiFi] Reconectado correctamente.");

      // Si vuelve el WiFi y Firebase aún no está inicializado, lo inicializamos aquí
      if (!firebaseInitialized) {
        inicializarFirebaseYHora();
      }
    }
  }
  wifiEstabaOK = wifiOK;

  bool firebaseOK = firebaseInitialized && Firebase.ready();

  // Leer posible comando de control desde RTDB
  if (firebaseOK) {
    manejarControlVentiladorDesdeRTDB();
  }

  // Procesamiento de datos recibidos del Arduino 
  if (Serial2.available()) {

    String datos = Serial2.readStringUntil('\n');
    datos.trim(); 

    if (datos.length() > 0) {
      int idx1 = datos.indexOf(',');
      int idx2 = datos.indexOf(',', idx1 + 1);

      if (idx1 == -1 || idx2 == -1) {
        Serial.print("[ERROR] Línea mal formada: ");
        Serial.println(datos);
      } else {

        String tempDHTStr = datos.substring(0, idx1);
        String humStr = datos.substring(idx1 + 1, idx2);
        String tempLM35Str = datos.substring(idx2 + 1);

        float tempDHT = tempDHTStr.toFloat();
        float humDHT = humStr.toFloat();
        float tempLM35 = tempLM35Str.toFloat();

        // Timestamp actual (se basa en la hora NTP ya sincronizada)
        time_t ahora = time(nullptr);

        if (ahora < 1700000000) {
          // Hora aún no sincronizada correctamente
          Serial.println("[NTP] Advertencia: hora no sincronizada. Se usará 'sin_fecha'.");
          ahora = 0;
        }

        // --- IMPRIMIR DATOS (para verificación local) ---
        Serial.println("\n-------------------------------------------");
        Serial.println("📍 Lectura de Sensores Recibida:");
        Serial.printf("🌡 Temp DHT11:  %.2f °C\n", tempDHT); 
        Serial.printf("💧 Humedad:     %.2f %%\n", humDHT); 
        Serial.printf("🌡 Temp LM35:   %.2f °C\n", tempLM35);
        Serial.print("🕒 Timestamp:   ");
        Serial.println(formatearTimestamp(ahora));
        Serial.println("-------------------------------------------");

        // 3) Decidir qué hacer con la lectura: enviar o guardar offline
        if (firebaseOK) {
          bool exito = enviarLecturaAFirebase(tempDHT, humDHT, tempLM35, ahora);
          if (!exito) {
            guardarLogOffline(tempDHT, humDHT, tempLM35, ahora);
          }
        } else {
          // Sin conexión o sin Firebase: se almacena en el log para enviarlo más tarde
          guardarLogOffline(tempDHT, humDHT, tempLM35, ahora);
        }
      }
    }
  }

  // 4) Si Firebase está OK, intentar vaciar el log de lecturas pendientes
  if (firebaseOK && logLecturasCount > 0) {
    intentarEnviarLogHistorial();
  }

  delay(1000); 
}

/* Codigo perteneciente al ESP32 */
// Maestro, porque mantiene toda la comunicacion con el entorno de manera centralizada
// Es como la puerta entre la parte física y digital que existe

#include <WiFi.h>

#include <Firebase_ESP_Client.h> 
#include <time.h>   // <- Para manejar fecha/hora de las lecturas

// Enlace al Arduino UNO
#define RXD2 16
#define TXD2 17

// CREDENCIALES de  red LAN
const char* ssid = "arduino"; 
const char* password = "qwerty777"; 

// --- CREDENCIALES DE FIREBASE (RTDB) ---
#define FIREBASE_HOST "https://tempcooldb-default-rtdb.firebaseio.com/"
#define FIREBASE_AUTH "EX1Zj6RRzGtekegHWkjnjYBh0EgGlzlqIxXb3yk3"

// --- CONTROL DE REINTENTOS WIFI ---
unsigned long ultimoIntentoWifi = 0;
const unsigned long INTERVALO_REINTENTO_WIFI = 10000; // 10 segundos entre intentos

// --- ZONA HORARIA ---
const long GMT_OFFSET_CHILE = -3 * 3600; 
const int  DAYLIGHT_OFFSET_CHILE = 0;

// LIBRERIAS NECESARIAS 
FirebaseData fbdo; 
FirebaseAuth auth; 
FirebaseConfig config; 

bool firebaseInitialized = false;

// Para evitar reenviar el mismo comando muchas veces
String ultimoComandoVentilador = "";

// Log OffLine, para cuando no hay conexion
struct LecturaOffline {
  float tempDHT;
  float humDHT;
  float tempLM35;
  time_t timestamp;
};

const int MAX_LOG_LECTURAS = 50;
LecturaOffline logLecturas[MAX_LOG_LECTURAS];
int logLecturasCount = 0;

// --- FUNCIONES AUXILIARES PARA TIEMPO Y LOG ---
// Formatea un time_t a texto "YYYY-MM-DD HH:MM"
String formatearTimestamp(time_t t) {
  if (t == 0) return "sin_fecha";
  struct tm localTime;
  localtime_r(&t, &localTime);
  char time[20];
  strftime(time, sizeof(time), "%Y-%m-%d %H:%M", &localTime);
  return String(time);
}

// Configura la hora TIME usando zona horaria de Chile
void configurarHoraChile() {
  // Configuración de zona horaria (Chile continental UTC-3)
  configTime(GMT_OFFSET_CHILE, DAYLIGHT_OFFSET_CHILE,
             "pool.ntp.org", "time.nist.gov");

  Serial.println("[TIME] Solicitando hora para Chile...");
  time_t now = 0;
  int reintentos = 0;

  // Esperar hasta 15 seg a que llegue una hora
  do {
    delay(1000);
    now = time(nullptr);
    Serial.print(".");
    reintentos++;
  } while (now < 1735689600 && reintentos < 15);

  Serial.println();

  if (now >= 1735689600) {
    Serial.print("[TIME] Hora sincronizada: ");
    Serial.println(formatearTimestamp(now));
  } else {
    Serial.println("[TIME] No se pudo obtener la hora");
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

  Serial.print("[LOG] Lectura almacenada offline, Cant logs: ");
  Serial.println(logLecturasCount);
}

// Envía una lectura concreta a Firebase (/lecturas + /historial)
bool enviarLecturaAFirebase(float tempDHT, float humDHT, float tempLM35, time_t ts) {

  // Validacion del estado de la FB
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

  // Registrar nodo en /historial con push automatico
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

// Intenta vaciar el log offline cuando Firebase esta disponible
void intentarEnviarLogHistorial() {
  if (!firebaseInitialized || !Firebase.ready()) return;
  if (logLecturasCount == 0) return;

  Serial.print("[log] Intentando enviar ");
  Serial.print(logLecturasCount);
  Serial.println(" lecturas pendientes...");

  int indiceLog = 0;
  while (indiceLog < logLecturasCount) {
    LecturaOffline &lec = logLecturas[indiceLog];

    bool exito = enviarLecturaAFirebase(
      lec.tempDHT,
      lec.humDHT,
      lec.tempLM35,
      lec.timestamp
    );

    if (exito) {
      // Si se envio, se eliminan los logs
      for (int indice = indiceLog + 1; indice < logLecturasCount; indice++) {
        logLecturas[indice - 1] = logLecturas[indice];
      }
      logLecturasCount--;
    } else {
      Serial.println("[log] Fallo al enviar una lectura del log");
      // La descartamos igual para que el buffer no quede pegado
      for (int indice = indiceLog + 1; indice < logLecturasCount; indice++) {
        logLecturas[indice - 1] = logLecturas[indice];
      }
      logLecturasCount--;
    }
  }

}

// Realiza el inicio de FB
void inicializarFB() {
  Serial.println("[START] Intentando inicializar");

  // Configuracion de RealTime DataBase
  config.database_url = FIREBASE_HOST;
  auth.token.uid = ""; 
  config.signer.tokens.legacy_token = FIREBASE_AUTH; 

  // Inicializar Firebase
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true); 

  delay(100);

  if (Firebase.ready()) {
    firebaseInitialized = true;
    Serial.println("[FB] Conexion exitosa");
    
    // Señal de vida
    Firebase.RTDB.setString(&fbdo, "/sensores/senal", "ESP32 conectado y listo");

    // Configurar hora por TIME con zona horaria de Chile
    configurarHoraChile();

  } else {
    firebaseInitialized = false;
    Serial.println("[FB] ERROR.. en la conexion o autenticacion.");
    Serial.println(fbdo.errorReason()); 
  }
}

void setup() {
  Serial.begin(115200); 
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2); 

  Serial.println("Intentando conectar al WiFi");

  // Conexión WiFi inicial
  WiFi.begin(ssid, password);
  long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < 20000) { 
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[EXITO] Conectado al WiFi");

    inicializarFB();

  } else {
    Serial.println("\n[FALLO] No se pudo conectar al WiFi");
  }
  
  Serial.println("Esperando datos del Arduino");
}

// --- CONTROL leer comando desde Firebase y mandarlo al Arduino ---
void manejarControlVentiladorDesdeRTDB() {
  const char* pathCmd = "/control/ventilador_cmd";

  // Leer SIEMPRE el valor del nodo de control
  if (!Firebase.RTDB.getString(&fbdo, pathCmd)) {
    Serial.print("[CTRL] Error al leer comando RTDB: ");
    Serial.println(fbdo.errorReason());
    return;
  }

  String cmd = fbdo.to<String>();
  cmd.trim();

  Serial.print("[CTRL] Valor leido en RTDB: '");
  Serial.print(cmd);
  Serial.println("'");

  if (cmd.length() == 0) {
    return;
  }

  // Solo reenviar si el comando cambió
  if (cmd != ultimoComandoVentilador) {
    Serial.print("[CTRL] Nuevo comando ");
    Serial.println(cmd);

    // El Arduino espera: "VENT_ON", "VENT_OFF" o "VENT_AUTO"
    Serial2.println(cmd);

    ultimoComandoVentilador = cmd;
  }
}

void loop() {
  // Verificacion de conexion [WiFi]
  wl_status_t wifiStatus = WiFi.status();
  bool wifiOK = (wifiStatus == WL_CONNECTED);

  static bool wifiEstabaOK = false;

  if (!wifiOK) {
    if (millis() - ultimoIntentoWifi > INTERVALO_REINTENTO_WIFI) {
      Serial.print("[WiFi] Estado actual: ");
      Serial.println((int)wifiStatus);
      Serial.println("[WiFi] Conexion perdida, intentando nuevamente");

      WiFi.disconnect();
      WiFi.begin(ssid, password);
      ultimoIntentoWifi = millis();
    }
  } else {

    if (!wifiEstabaOK) {
      Serial.println("[WiFi] Reconectado correctamente.");

      if (!firebaseInitialized) {
        inicializarFB();
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

        // Timestamp actual
        time_t ahora = time(nullptr);

        if (ahora < 1735689600) {
          // Hora aún no sincronizada correctamente
          Serial.println("[TIME] Advertencia: hora no sincronizada. Se usará 'sin_fecha'.");
          ahora = 0;
        }

        // --- Impresion de datos ---
        Serial.println("\n-------------------------------------------");
        Serial.println("📍 Lectura de Sensores Recibida:");
        Serial.printf("🌡 Temp DHT11:  %.2f °C\n", tempDHT); 
        Serial.printf("💧 Humedad:     %.2f %%\n", humDHT); 
        Serial.printf("🌡 Temp LM35:   %.2f °C\n", tempLM35);
        Serial.print("🕒 Timestamp:   ");
        Serial.println(formatearTimestamp(ahora));
        Serial.println("-------------------------------------------");

        if (firebaseOK) {
          bool exito = enviarLecturaAFirebase(tempDHT, humDHT, tempLM35, ahora);
          if (!exito) {
            guardarLogOffline(tempDHT, humDHT, tempLM35, ahora);
          }
        } else {
          // Si no hay conexion con FB se almacena en el log
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

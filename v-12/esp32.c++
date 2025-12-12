/* Codigo perteneciente al ESP32 */
// Maestro, porque mantiene toda la comunicacion con el entorno de manera centralizada
// Es como la puerta entre la parte física y digital que existe

#include <WiFi.h>
#include <Firebase_ESP_Client.h> 

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

// LIBRERIAS NECESARIAS
FirebaseData fbdo; 
FirebaseAuth auth; 
FirebaseConfig config; 

bool firebaseInitialized = false;

// Para evitar reenviar el mismo comando muchas veces
String ultimoComandoVentilador = "";

void setup() {
    Serial.begin(115200); 
    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2); 
    
    Serial.println("\n--- ESP32 Gateway IoT Iniciado (Maestro de comunicaciones) ---");
    Serial.println("Intentando conectar a WiFi...");

    // Conexión WiFi
    WiFi.begin(ssid, password);
    long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < 20000) { 
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[EXITO] Conectado al WiFi");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());

        // --- Configuracion de FireBase 🔥 (Realtime Database) ---
        config.database_url = FIREBASE_HOST;
        auth.token.uid = ""; 
        config.signer.tokens.legacy_token = FIREBASE_AUTH; 

        // Inicializar Firebase
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true); 
        
        // Verificar inicialización
        if (Firebase.ready()) {
            firebaseInitialized = true;
            Serial.println("[FIREBASE] Inicialización Exitosa.");
            
            // Señal de vida
            Firebase.RTDB.setString(&fbdo, "/sensores/heartbeat", "ESP32 Ready");
        } else {
            Serial.println("[FIREBASE] ERROR en la inicialización o autenticación.");
            Serial.println(fbdo.errorReason()); 
        }

    } else {
        Serial.println("\n[FALLO] No se pudo conectar al WiFi. Revise SSID/Pass.");
    }
    
    Serial.println("Esperando datos del Arduino...");
}

// --- Función: leer comando desde Firebase y reenviarlo al Arduino ---
void manejarControlVentiladorDesdeRTDB() {
    // Ruta propuesta para el comando de ventilador
    const char* pathCmd = "/control/ventilador_cmd";

    if (Firebase.RTDB.getString(&fbdo, pathCmd)) {
        String cmd = fbdo.to<String>();
        cmd.trim();

        if (cmd.length() == 0) {
            return;
        }

        // Solo reenviar si el comando cambió
        if (cmd != ultimoComandoVentilador) {
            Serial.print("[CTRL] Nuevo comando desde RTDB: ");
            Serial.println(cmd);

            // Reenvía el comando al Arduino Uno por Serial2
            // El Arduino espera: "VENT_ON", "VENT_OFF" o "VENT_AUTO"
            Serial2.println(cmd);

            ultimoComandoVentilador = cmd;
        }
    } else {
        // Si quieres ver el error, descomenta estas líneas:
        // Serial.print("[CTRL] Error al leer comando: ");
        // Serial.println(fbdo.errorReason());
    }
}

void loop() {
    // Verificación básica de conectividad
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        delay(1000); 
        return; 
    }
    
    if (!Firebase.ready() || !firebaseInitialized) {
        Serial.print("Firebase NO LISTO. Razón: ");
        Serial.println(fbdo.errorReason());
        delay(1000);
        return;
    }

    // 1) Leer posible comando de control desde la RTDB y enviarlo al Arduino
    manejarControlVentiladorDesdeRTDB();

    // 2) Leer datos del Arduino y subirlos a la RealTime Database
    if (Serial2.available()) {
        // Lógica de lectura y extracción de datos
        String datos = Serial2.readStringUntil('\n');
        datos.trim(); 

        if (datos.length() == 0) return; 

        int idx1 = datos.indexOf(',');
        int idx2 = datos.indexOf(',', idx1 + 1);

        if (idx1 == -1 || idx2 == -1) {
            Serial.print("[ERROR] Línea mal formada: ");
            Serial.println(datos);
            return;
        }

        String tempDHTStr = datos.substring(0, idx1);
        String humStr = datos.substring(idx1 + 1, idx2);
        String tempLM35Str = datos.substring(idx2 + 1);

        float tempDHT = tempDHTStr.toFloat();
        float humDHT = humStr.toFloat();
        float tempLM35 = tempLM35Str.toFloat();

        // --- ENVIAR DATOS A REALTIME DATABASE (RTDB) ---
        Serial.println("\n[RTDB] Enviando datos...");

        if (
            Firebase.RTDB.setFloat(&fbdo, "/lecturas/temp_dht", tempDHT) &&
            Firebase.RTDB.setFloat(&fbdo, "/lecturas/humedad", humDHT) &&
            Firebase.RTDB.setFloat(&fbdo, "/lecturas/temp_lm35", tempLM35)
        ) {
            Serial.println("[RTDB] Datos de sensores enviados OK.");
        } else {
            Serial.print("[RTDB] ERROR al enviar datos: ");
            Serial.println(fbdo.errorReason());
        }
        
        // --- IMPRIMIR DATOS (para verificación local) ---
        Serial.println("\n-------------------------------------------");
        Serial.println("📍 Lectura de Sensores Recibida:");
        Serial.printf("🌡 Temp DHT11:  %.2f °C\n", tempDHT); 
        Serial.printf("💧 Humedad:     %.2f %%\n", humDHT); 
        Serial.printf("🌡 Temp LM35:   %.2f °C\n", tempLM35);
        Serial.println("-------------------------------------------");
    }
    
    delay(1000); 
}

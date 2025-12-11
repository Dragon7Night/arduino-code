/* Codigo perteneciente al ESP32 */
// Maestro, por que mantiene toda la comunicacion con el entorno de manera centralizada
// es como la puerta entre la parte fisica y digital que existe

#include <WiFi.h>
#include <Firebase_ESP_Client.h> 

// --- 1. CONFIGURACIÓN SERIAL Y WIFI ---
// PINES
#define RXD2 16
#define TXD2 17

// CREDENCIALES WIFI
const char* ssid = "ARDUINO"; 
const char* password = "12345678"; 

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
        

        // 3. Verificar inicialización
        if (Firebase.ready()) {
            firebaseInitialized = true;
            Serial.println("[FIREBASE] Inicialización Exitosa.");
            
            // ⬇ CORRECCIÓN: Uso de Firebase.RTDB.setString
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

void loop() {
    
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
        
        // ⬇ CORRECCIÓN: Uso de Firebase.RTDB.setFloat para los envíos
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
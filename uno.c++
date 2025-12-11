/* Codigo perteneciente al Arduino Uno */
// Esclavo, por que se encarga de recolectar todos los datos para mandarselos al ESP32


#include <DHT.h>

// --- 1. DEFINICIONES DE PINES Y SENSORES ---
#define DHTPIN 8        // Pin conectado al DHT11
#define DHTTYPE DHT11   // Tipo de sensor
#define RELAY_PIN 4     // Pin conectado a la señal (IN) del Módulo Relé

// --- 2. CONFIGURACIÓN DEL RELÉ ---
// Conexión COM/NO: Necesita LOW en la señal para encender.
#define RELAY_ON LOW    // Señal para ENCENDER el ventilador (Pin 4 bajo)
#define RELAY_OFF HIGH  // Señal para APAGAR el ventilador (Pin 4 alto)

// --- 3. UMBRAL DE CONTROL Y CONVERSIONES ---
// 🚨 CAMBIO DE REQUISITO: Umbral ajustado a 26.0 °C
#define UMBRAL_TEMPERATURA 26.0 
#define VREF_CONVERSION_FACTOR (500.0 / 1024.0) 

// --- 4. OBJETOS ---
DHT dht(DHTPIN, DHTTYPE); 


void setup() {
  Serial.begin(9600);   
  dht.begin();          
  pinMode(RELAY_PIN, OUTPUT);
  
  // Aseguramos que el ventilador esté APAGADO al iniciar
  digitalWrite(RELAY_PIN, RELAY_OFF); 
}


void loop() {
  // --- A. LECTURA Y CONVERSIÓN DE SENSORES ---
  int lecturaCrudaLM35 = analogRead(A0); 
  float tempLM35 = lecturaCrudaLM35 * VREF_CONVERSION_FACTOR; 
  
  float tempDHT = dht.readTemperature(); 
  float humDHT = dht.readHumidity();     
  bool dht_valido = !isnan(tempDHT);
  
  // --- B. LÓGICA DE CONTROL DEL RELÉ (Condición OR) ---
  
  // El ventilador se enciende si:
  // 1. La lectura del DHT es válida, Y
  // 2. La temperatura del DHT O la temperatura del LM35 superan el umbral.
  if (dht_valido && ((tempDHT > UMBRAL_TEMPERATURA) || (tempLM35 > UMBRAL_TEMPERATURA))) {
    digitalWrite(RELAY_PIN, RELAY_ON); // ENCIENDE el ventilador
  } 
  // Si la condición de encendido no se cumple (ambas temperaturas están frías o DHT falló), se apaga.
  else {
    digitalWrite(RELAY_PIN, RELAY_OFF); // APAGA el ventilador
  }
  
  // --- C. ENVÍO DE DATOS LIMPIOS AL ESP32 ---
  Serial.print(tempDHT, 2); 
  Serial.print(",");
  Serial.print(humDHT, 2);  
  Serial.print(",");
  Serial.println(tempLM35, 2); 

  delay(2000); 
}
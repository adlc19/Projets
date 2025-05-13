#include <SoftwareSerial.h>
#include <Wire.h>
#include "Adafruit_AM2320.h"
#include <Servo.h>

// --- Communication ESP8266 ---
SoftwareSerial esp8266(8, 9); // RX, TX

// --- Capteur AM2320 ---
Adafruit_AM2320 am2320 = Adafruit_AM2320();
const int capteurPowerPin = 7; // Alimentation capteur

// --- Servo moteur pour le toit ---
Servo servoToit;
const int pinServo = 2;
bool toitOuvert = false;

// --- Capteur d'humidité du sol et pompe ---
const int soilMoisturePin = A1;
const int pumpPin = 3;
const int SEUIL_HUMIDITE = 30; // %
const int DUREE_POMPE = 5000;  // ms
const int dryValue = 560;
const int wetValue = 300;

// --- Capteur de niveau d'eau ---
const int pinCapteurEau = A2;
const int valeurSec = 350;
const int valeurPlein = 680;

// --- Connexion WiFi ---
const char* ssid = "ProjetSerreConnectee";
const char* password = "ProjetSerreConnectee123";
const char* serverUrl = "192.168.10.100"; // IP serveur
bool debugESP = false;  // Activer/désactiver logs ESP
unsigned long lastSendTime = 0;
const unsigned long interval = 60UL * 60UL * 1000UL; // 60 minutes

void setup() {
  Serial.begin(115200);
  esp8266.begin(9600);

  // Capteur AM2320
  pinMode(capteurPowerPin, OUTPUT);
  digitalWrite(capteurPowerPin, HIGH);
  delay(500);
  Wire.begin();
  am2320.begin();

  // Servo toit
  servoToit.attach(pinServo);
  servoToit.write(180); // fermé
  toitOuvert = false;

  // Pompe
  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, LOW);

  // Capteur niveau d’eau
  pinMode(pinCapteurEau, INPUT);

  Serial.println(F("\n=== Connexion WiFi ESP8266 ==="));
  if (connectWiFi()) {
    Serial.println(F("✅ Connexion réussie au réseau WiFi"));
    Serial.print(F("📶 Réseau : ")); Serial.println(ssid);
  } else {
    Serial.println(F("❌ Échec de connexion au réseau WiFi"));
  }
  Serial.println(F("==============================\n"));

  Serial.println("Système prêt.");
}

void loop() {
  unsigned long currentTime = millis();

  // Vérifie si 60 minutes se sont écoulées
  if (currentTime - lastSendTime >= interval || lastSendTime == 0) {
    lastSendTime = currentTime;

    // --- Lecture capteurs ---
    float temperature = am2320.readTemperature();
    float humidity_air = am2320.readHumidity();

    int soilMoistureValue = analogRead(soilMoisturePin);
    int humidity_soil = map(soilMoistureValue, dryValue, wetValue, 0, 100);
    humidity_soil = constrain(humidity_soil, 0, 100);

    int valeurBruteEau = analogRead(pinCapteurEau);
    int niveau_eau = map(valeurBruteEau, valeurSec, valeurPlein, 0, 100);
    niveau_eau = constrain(niveau_eau, 0, 100);

    // --- Affichage ---
    Serial.println(F("\n=== Données Capteurs ==="));
    Serial.print(F("🌡️  Température de l'air : "));
    Serial.print(temperature, 1); Serial.println(F(" °C"));

    Serial.print(F("💧 Humidité de l'air     : "));
    Serial.print(humidity_air, 1); Serial.println(F(" %"));

    Serial.print(F("🌱 Humidité du sol       : "));
    Serial.print(humidity_soil); Serial.println(F(" %"));

    Serial.print(F("💧 Niveau d'eau          : "));
    Serial.print(niveau_eau); Serial.println(F(" %"));

    // --- Gestion pompe ---
    Serial.print(F("🚿 Pompe : "));
    if (humidity_soil < SEUIL_HUMIDITE) {
      Serial.println(F("ON"));
      digitalWrite(pumpPin, HIGH);
      delay(DUREE_POMPE);
      digitalWrite(pumpPin, LOW);
    } else {
      Serial.println(F("OFF"));
    }

    // --- Envoi des données au serveur ---
    sendDataToServer(temperature, (int)humidity_air, humidity_soil, niveau_eau);

    // --- Contrôle du toit ---
    if (temperature > 25 && !toitOuvert) {
      ouvrirToit();
      toitOuvert = true;
    } else if (temperature <= 25 && toitOuvert) {
      fermerToit();
      toitOuvert = false;
    }

    Serial.println(F("========================\n"));
  }

  // Petite pause pour ne pas saturer la boucle principale
  delay(500);
}

// --- Fonctions auxiliaires ---

void ouvrirToit() {
  for (int angle = 180; angle >= 50; angle--) {
    servoToit.write(angle);
    delay(10);
  }
  Serial.println(">> Toit ouvert");
}

void fermerToit() {
  for (int angle = 50; angle <= 180; angle++) {
    servoToit.write(angle);
    delay(10);
  }
  Serial.println(">> Toit fermé");
}

bool connectWiFi() {
  sendCommand("AT+RST", 5000);
  sendCommand("AT+CWMODE=1", 2000);
  String cmd = "AT+CWJAP=\"" + String(ssid) + "\",\"" + String(password) + "\"";
  return sendCommand(cmd.c_str(), 10000);
}

void sendDataToServer(float temp, int humAir, int humSoil, int niveauEau) {
  String request = "GET /insert_data.php?temperature=" + String(temp, 1) +
                   "&humidity_air=" + String(humAir) +
                   "&humidity_soil=" + String(humSoil) +
                   "&niveau_eau=" + String(niveauEau) +
                   " HTTP/1.1\r\nHost: " + String(serverUrl) + "\r\nConnection: close\r\n\r\n";

  if (sendCommand(("AT+CIPSTART=\"TCP\",\"" + String(serverUrl) + "\",80").c_str(), 5000)) {
    if (sendCommand(("AT+CIPSEND=" + String(request.length())).c_str(), 2000)) {
      sendCommand(request.c_str(), 5000);
    }
  }

  if (sendCommand("AT+CIPCLOSE", 2000)) {
    Serial.println(F("📤 Données envoyées avec succès au serveur !"));
  } else {
    Serial.println(F("❌ Échec de l'envoi des données."));
  }
}

bool sendCommand(const char* command, int timeout) {
  esp8266.println(command);
  long int time = millis();
  String fullResponse = "";
  bool warningPrinted = false;

  while ((millis() - time) < timeout) {
    while (esp8266.available()) {
      String line = esp8266.readStringUntil('\n');
      line.trim();

      if (line.length() == 0) continue;
      fullResponse += line + "\n";

      if (line.indexOf("OK") != -1 || line.indexOf("SEND OK") != -1 || line.indexOf("CONNECTED") != -1) {
        if (debugESP) Serial.println(F("✅ Réponse ESP : OK"));
        return true;
      }

      if (line.indexOf("link is not valid") != -1 && String(command).indexOf("AT+CIPSEND") != -1) {
        if (debugESP && !warningPrinted) {
          Serial.println(F("ℹ️  Info : lien non valide, on continue."));
          warningPrinted = true;
        }
        continue;
      }

      if (line.indexOf("ERROR") != -1 || line.indexOf("FAIL") != -1) {
        if (String(command).startsWith("AT+CIPCLOSE")) return true;
        if (debugESP) {
          Serial.print(F("⚠️  Erreur ESP : "));
          Serial.println(fullResponse);
        }
        return false;
      }
    }
  }

  if (debugESP) {
    Serial.print(F("⏱️  Timeout ESP8266 sur commande : "));
    Serial.println(command);
  }
  return false;
}

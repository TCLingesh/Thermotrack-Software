#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <HTTPClient.h>

#define ONE_WIRE_BUS 4
#define RELAY_PIN 26
#define BUZZER_PIN 13
#define CURRENT_PIN 34
#define LED_PIN 27

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// WiFi
const char* ssid = "dinlin_5G";
const char* password = "123456789";
const char* serverName = "http://thermotrack.pythonanywhere.com/update";

// --- Healing System ---
float tempHistory[5] = {0,0,0,0,0};
int historyCount = 0;
bool isHealing = false;
unsigned long healingStartTime = 0;
const unsigned long HEAL_DURATION = 10000;

// --- ACS712 Settings ---
float sensitivity = 0.185;   // 5A module

// -------- AC CURRENT FUNCTION ----------
float readACCurrent()
{
  int samples = 500;
  int maxValue = 0;
  int minValue = 4095;

  for(int i=0;i<samples;i++)
  {
    int sensorValue = analogRead(CURRENT_PIN);

    if(sensorValue > maxValue) maxValue = sensorValue;
    if(sensorValue < minValue) minValue = sensorValue;

    delayMicroseconds(200);
  }

  float Vpp = (maxValue - minValue) * (3.3 / 4095.0);
  float Vrms = (Vpp / 2.0) * 0.707;

  float current = Vrms / sensitivity;

  return current;
}

void setup()
{
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  sensors.begin();

  WiFi.begin(ssid, password);

  Serial.print("Connecting WiFi");

  while(WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
}

void loop()
{
  sensors.requestTemperatures();
  float temperature = sensors.getTempCByIndex(0);

  float actualCurrent = readACCurrent() ;
  // - 0.25;

  // if(actualCurrent < 0)
  // actualCurrent = 0;

  // -------- Temperature Trend ----------
  for(int i=0;i<4;i++)
    tempHistory[i] = tempHistory[i+1];

  tempHistory[4] = temperature;

  if(historyCount < 5)
    historyCount++;

  float tempChange = tempHistory[4] - tempHistory[0];

  bool coolingFailure =
  ((tempChange > 1.2) && historyCount == 5 && actualCurrent > 0.5);

  bool overCurrent = (actualCurrent > 4.5);

  String s = "Healthy";

  // -------- Healing State Machine --------
  if(isHealing)
  {
    if(millis() - healingStartTime >= HEAL_DURATION)
    {
      isHealing = false;
      Serial.println("HEALING COMPLETE: Restarting...");
    }
    else
    {
      s = "HEALING: System Resting";

      digitalWrite(RELAY_PIN, HIGH);
      digitalWrite(LED_PIN, HIGH);
    }
  }

  else
  {
    if((coolingFailure || temperature > 50) || overCurrent)
    {
      isHealing = true;
      healingStartTime = millis();

      if(overCurrent)
        s = "TRIP: Overcurrent";
      else
        s = "TRIP: Cooling Failure";

      Serial.println("FAULT DETECTED -> Healing Phase");
    }

    else
    {
      digitalWrite(RELAY_PIN, LOW);
      digitalWrite(LED_PIN, LOW);
      noTone(BUZZER_PIN);
    }
  }

  // -------- Alarm ----------
  if(isHealing)
    tone(BUZZER_PIN,2000);
  else
    noTone(BUZZER_PIN);

  // -------- Serial Output ----------
  Serial.print("Temperature: ");
  Serial.print(temperature);

  Serial.print(" | Current: ");
  Serial.print(actualCurrent);

  Serial.print(" | Status: ");
  Serial.println(s);

  // -------- Send to Server ----------
  if(WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;

    http.begin(serverName);
    http.addHeader("Content-Type","application/json");

    String httpRequestData =
    "{\"temp\":" + String(temperature) +
    ",\"current\":" + String(actualCurrent) +
    ",\"status\":\"" + s + "\"}";

    http.POST(httpRequestData);

    http.end();
  }

  delay(2000);
}

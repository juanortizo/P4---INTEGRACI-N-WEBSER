#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <stdlib.h>

#include "data.h"
#include "Settings.h"

#include <UbiConstants.h>
#include <UbidotsEsp32Mqtt.h>
#include <UbiTypes.h>

#include <TFT_eSPI.h>
#include <SPI.h>
#include "DHT.h"

#define DHTPIN 27
#define DHTTYPE DHT11
#define MI_ABS(x) ((x) < 0 ? -(x) : (x))

TFT_eSPI tft = TFT_eSPI();
DHT dht(DHTPIN, DHTTYPE);

#define BUTTON_LEFT 0        // btn activo en bajo
#define LONG_PRESS_TIME 3000 // 3000 milis = 3s

WebServer server(80);

Settings settings;
int lastState = LOW; // para el btn
int currentState;    // the current reading from the input pin
unsigned long pressedTime = 0;
unsigned long releasedTime = 0;

const char *UBIDOTS_TOKEN = "BBUS-XAorU8jJCxplTIudzCGCoYT9sTmhkV"; // Put here your Ubidots TOKEN
const char *DEVICE_LABEL = "esp32";                                // Put here your Device label to which data will be published
const char *VARIABLE_LABEL1 = "sw1";
const char *VARIABLE_LABEL2 = "sw2";
const char *VARIABLE_LABEL3 = "Temp"; // Temperatura
const char *VARIABLE_LABEL4 = "Hum";  // humedad

const int PUBLISH_FREQUENCY = 5000; // Update rate in milliseconds

unsigned long timer;

Ubidots ubidots(UBIDOTS_TOKEN);

int tamano;
int posicion;
char boton = '0';
char val = '0';

bool sw1State = false; // Estado inicial del sw1 (apagado)
bool sw2State = false; // Estado inicial del sw2 (apagado)

void load404();
void loadIndex();
void loadFunctionsJS();
void restartESP();
void saveSettings();
bool is_STA_mode();
void AP_mode_onRst();
void STA_mode_onRst();
void detect_long_press();
void callback(char *topic, byte *payload, unsigned int length);

void startAP()
{
  WiFi.disconnect();
  delay(19);
  Serial.println("Starting WiFi Access Point (AP)");
  WiFi.softAP("esp32_JD", "facil123");
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
}

void start_STA_client()
{
  WiFi.softAPdisconnect(true);
  WiFi.disconnect();
  delay(100);
  Serial.println("Starting WiFi Station Mode");
  WiFi.begin((const char *)settings.ssid.c_str(), (const char *)settings.password.c_str());
  WiFi.mode(WIFI_STA);

  int cnt = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    if (cnt == 100)
      AP_mode_onRst();
    cnt++;
    Serial.println("attempt # " + (String)cnt);
  }

  WiFi.setAutoReconnect(true);
  Serial.println(F("WiFi connected"));
  Serial.println(F("IP address: "));
  Serial.println(WiFi.localIP());
  pressedTime = millis();

  ubidots.setCallback(callback);
  ubidots.setup();
  ubidots.reconnect();

  float Hum = dht.readHumidity();
  float Temp = dht.readTemperature();
  tft.init();
  tft.fillScreen(TFT_WHITE);
  tft.setRotation(1);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawString("Realizado por:", 10, 5, 2);
  tft.setTextColor(TFT_GREEN, TFT_WHITE);
  tft.drawString("Juan Daniel", 50, 35, 4);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawString("Temperatura", 25, 70, 2);
  tft.drawString("Humedad", 150, 70, 2);
  if (isnan(Temp) || isnan(Hum))
  {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }

  tft.fillCircle(35, 100, 10, TFT_DARKGREY);
  tft.fillCircle(145, 100, 10, TFT_DARKGREY);

  ubidots.subscribeLastValue(DEVICE_LABEL, VARIABLE_LABEL1);
  ubidots.subscribeLastValue(DEVICE_LABEL, VARIABLE_LABEL2);
  Serial.println(F("DHTxx test!"));
  dht.begin();
  timer = millis();
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  EEPROM.begin(4096);
  pinMode(BUTTON_LEFT, INPUT_PULLUP);

  settings.load();
  settings.info();

  Serial.println("");
  Serial.println("starting...");

  if (is_STA_mode())
  {
    start_STA_client();
  }
  else
  {
    startAP();

    server.onNotFound(load404);
    server.on("/", loadIndex);
    server.on("/index.html", loadIndex);
    server.on("/functions.js", loadFunctionsJS);
    server.on("/settingsSave.json", saveSettings);
    server.on("/restartESP.json", restartESP);

    server.begin();
    Serial.println("HTTP server started");
  }
}

void loop()
{
  if (is_STA_mode())
  {
    if (!ubidots.connected())
    {
      ubidots.reconnect();
      ubidots.subscribeLastValue(DEVICE_LABEL, VARIABLE_LABEL1);
      ubidots.subscribeLastValue(DEVICE_LABEL, VARIABLE_LABEL2);
    }

    float Hum = dht.readHumidity();
    float Temp = dht.readTemperature();

    if ((MI_ABS(millis() - timer)) > PUBLISH_FREQUENCY)
    {
      Serial.print("Temperatura: ");
      Serial.print(Temp);
      Serial.print("Humedad: ");
      Serial.println(Hum);

      tft.drawString(String(Hum), 160, 100);
      tft.drawString(String(Temp), 50, 100);
      ubidots.add(VARIABLE_LABEL3, Temp);
      ubidots.add(VARIABLE_LABEL4, Hum);
      ubidots.publish(DEVICE_LABEL);
      timer = millis();
    }

    ubidots.loop();

    if (sw1State)
    {
      tft.fillCircle(35, 100, 10, TFT_GREEN);
    }
    else
    {
      tft.fillCircle(35, 100, 10, TFT_DARKGREY);
    }

    if (sw2State)
    {
      tft.fillCircle(145, 100, 10, TFT_GREEN);
    }
    else
    {
      tft.fillCircle(145, 100, 10, TFT_DARKGREY);
    }
  }
  else
  {
    server.handleClient();
  }

  delay(10);
  detect_long_press();
}

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  tamano = strlen(topic);
  posicion = tamano - 4;
  printf("switch: %c\n", topic[posicion]);
  boton = topic[posicion];
  val = payload[0];
  if (boton == '1')

    if ((char)payload[0] == '1')
    {
      sw1State = true;
      tft.fillCircle(35, 100, 10, TFT_GREEN);
    }
    else
    {
      sw1State = false;
      tft.fillCircle(35, 100, 10, TFT_DARKGREY);
    }
  if (boton == '2')
    if ((char)payload[0] == '1')
    {
      sw2State = true;
      tft.fillCircle(145, 100, 10, TFT_GREEN);
    }
    else
    {
      sw2State = false;
      tft.fillCircle(145, 100, 10, TFT_DARKGREY);
    }
}

void load404()
{
  server.send(200, "text/html", data_get404());
}

void loadIndex()
{
  server.send(200, "text/html", data_getIndexHTML());
}

void loadFunctionsJS()
{
  server.send(200, "text/javascript", data_getFunctionsJS());
}

void restartESP()
{
  server.send(200, "text/json", "true");
  ESP.restart();
}

void saveSettings()
{
  if (server.hasArg("ssid"))
    settings.ssid = server.arg("ssid");
  if (server.hasArg("password"))
    settings.password = server.arg("password");

  settings.save();
  server.send(200, "text/json", "true");
  STA_mode_onRst();
}

bool is_STA_mode()
{
  if (EEPROM.read(flagAdr))
    return true;
  else
    return false;
}

void AP_mode_onRst()
{
  EEPROM.write(flagAdr, 0);
  EEPROM.commit();
  delay(100);
  ESP.restart();
}

void STA_mode_onRst()
{
  EEPROM.write(flagAdr, 1);
  EEPROM.commit();
  delay(100);
  ESP.restart();
}

void detect_long_press()
{
  currentState = digitalRead(BUTTON_LEFT);

  if (lastState == HIGH && currentState == LOW)
    pressedTime = millis();
  else if (lastState == LOW && currentState == HIGH)
  {
    releasedTime = millis();

    long pressDuration = releasedTime - pressedTime;

    if (pressDuration > LONG_PRESS_TIME)
    {
      Serial.println("(Hard reset) returning to AP mode");
      delay(500);
      AP_mode_onRst();
    }
  }

  lastState = currentState;
}

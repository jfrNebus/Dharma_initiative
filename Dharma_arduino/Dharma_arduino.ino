#include <WiFi.h>
#include <string.h>
#include <MD_Parola.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW


// Validos -------------------------------------


// const char* ssid;
// const char* wifiPassword;

const int SECOND = 1000;
const int ARRAY_SIZE = 4;
const int NO_INDEX_FOUND = -1;

const int RELAY = 21;
const int STATE_LED = 17;
const int RESET_BUTTON = 18;
const int KEY = 16;
const int TAMPER = 22;
const int BINARY_PIN = 5;
const int NUMPIXELS = 13;
const int MAX_DEVICES = 5;
const int DATA_PIN = 15;
const int CS_PIN = 2;
const int CLK_PIN = 4;

// const int TIME_COUNTER_DEFAULT = 6539;
const int TIME_COUNTER_DEFAULT = 69;
const int FIRST_STAGE = 300;
const int SECOND_STAGE = 60;
const int THIRD_STAGE = 10;
const int END_STAGE = 0;
const int ALARM_MODIFIER_DEFAULT = 3;

int alarmModifier = ALARM_MODIFIER_DEFAULT;
int timeCounter = TIME_COUNTER_DEFAULT;
int previousCounter = timeCounter;
int currentTime = 0;
int previousTime = 0;
int missConnectionCounter = 0;
int connectionLost = 0;

bool exitLoop = false;
bool failureState = true;
bool resetValues = true;

String logText = "";
String octets[ARRAY_SIZE];
String pSsid;
String pNetworkPassword;
String ipChangePassword = "admin";
String stringHelp;


MD_Parola parola = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

char character[2];
textPosition_t positions[] = {
  PA_LEFT, PA_LEFT, PA_LEFT, PA_RIGHT, PA_RIGHT
};

MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, BINARY_PIN, NEO_GRB + NEO_KHZ800);

Preferences preferences;

WiFiServer server(80);

// ---------------------------------------------

void setup() {
  Serial.begin(115200);

  character[1] = '\0';

  parola.setIntensity(0);
  parola.begin(5);
  parola.setZone(0, 4, 4);
  parola.setZone(1, 3, 3);
  parola.setZone(2, 2, 2);
  parola.setZone(3, 1, 1);
  parola.setZone(4, 0, 0);
  for (int i = 0; i < MAX_DEVICES; i++) {
    parola.displayZoneText(i, character, positions[i], 0, 0, PA_PRINT, PA_NO_EFFECT);
  }

  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, 2);
  pixels.begin();
  preferences.begin("migration");
  pSsid = preferences.getString("ssid", "network");
  pNetworkPassword = preferences.getString("networkPassword", "network12345");
  preferences.end();

  Serial.println("----** Network **----");
  Serial.println(pSsid);
  Serial.println(pNetworkPassword);
  Serial.println("----*************----");

  pinMode(RELAY, OUTPUT);        //Action Led
  pinMode(STATE_LED, OUTPUT);    //Wifi conexion state
  pinMode(RESET_BUTTON, INPUT);  //Reset button
  pinMode(KEY, INPUT);           //Key switch
  pinMode(TAMPER, INPUT);        //Tamper

  digitalWrite(STATE_LED, HIGH);


  Serial.print("Connecting to " + String(pSsid) + "\n");
  preferencesIpConfig();
  //Se pone antes el bloque preferences para evitar que se inicie la red en formato DHCP en caso
  //de que se cumplan las concidiones necesarias.0
  tryConnection(pSsid, pNetworkPassword);

  digitalWrite(STATE_LED, LOW);

  setHelp();

  server.begin();
}

void loop() {

  // Triggers detection -------------------------------

  if (digitalRead(KEY)) {
    Serial.println("Inside KEY");
    parola.displayClear();
    if (digitalRead(RELAY)) {
      digitalWrite(RELAY, LOW);
    }
    // The next while loop blocks the whole code while the key is triggered
    while (digitalRead(KEY)) {
      // This part checks if there's any available client, in case there's it will
      // write the message "key_activated" which will let the client know that
      // the key was triggered so it can start the key menu. It will be running
      // while the key remains triggered.
      WiFiClient client = server.available();
      if (client) {
        client.write("key_activated");
      }
      if (digitalRead(RESET_BUTTON)) {
        while (digitalRead(RESET_BUTTON)) {}
        Serial.println("\nNetwork reset requested.");
        Serial.println("\n----** Network **----\nnetwork\nnetwork12345\n----*************----");
        saveIpPreferences(false);
        saveNetworkPreferences("network", "network12345");
        ESP.restart();
      }
    }
    delay(50);
  } else if (digitalRead(TAMPER)) {
    Serial.println("Tamper triggered.");
    systemFailureOn();
    // The next while loop blocks the whole code while the tamper is triggered
    while (digitalRead(TAMPER) == HIGH) {
      // This part checks if there's any available client, in case there's it will
      // write the message "stopSystem" which will let the client know that
      // the tamper is not triggered so it can start the tamper menu. It will be running
      // while the tamper remains untriggered.
      WiFiClient client = server.available();
      if (client) {
        client.write("stopSystem");
      }
    }
    systemFailureOff();
    delay(50);
  }

  // Else to key and tamper detection -----------------
  // End of triggers detection ------------------------

  else {


    WiFiClient client = server.available();

    if (client) {

      currentTime = millis();
      previousTime = currentTime;

      initiativeReset();


      Serial.println("New Client.");
      String currentLine = "";
      int bufferSize = 0;
      int keyRead = 0;
      int tamperRead = 0;

      while (exitLoop == false) {

        parola.displayAnimate();  // DOCUMENTAR ESTA MODIFICACIÓN

        // keyRead = digitalRead(KEY);
        // tamperRead = digitalRead(TAMPER);
        // if (keyRead == HIGH) {
        if (digitalRead(KEY)) {
          // Key detection -------------------------------
          Serial.println("Inside KEY");
          client.write("key_activated");
          parola.displayClear();
          digitalWrite(RELAY, LOW);
          break;
        } else if (digitalRead(TAMPER)) {
          client.write("stopSystem");
          Serial.println("Command sent: stopSystem");
          delay(10000);
          systemFailureOn();
          break;
        }


        bufferSize = client.available();
        char c;

        // Buffer reading ------------------------------

        for (int i = 0; i < bufferSize; i++) {
          c = client.read();
          currentLine += c;
        }

        // WebService V --------------------------------

        int carriageReturn = currentLine.indexOf("\r\n\r\nui=");

        if (currentLine.indexOf("GET /") > NO_INDEX_FOUND || carriageReturn > NO_INDEX_FOUND) {
          if (carriageReturn > NO_INDEX_FOUND) {
            currentLine = currentLine.substring(carriageReturn + 4);
            Serial.println(currentLine);
          }
          checkCommand(client, currentLine);
          webStartPage(client);
          exitLoop = true;
        } else if (!currentLine.startsWith("ui=")) {
          if (digitalRead(KEY) == HIGH) {
            // Key detection -------------------------------
            Serial.println("Inside KEY");
            client.write("key_activated");
            delay(2000);
            parola.displayClear();
            digitalWrite(RELAY, LOW);
            break;
          }
          if ((currentTime - previousTime) >= SECOND) {

            printTime();  // DOCUMENTAR ESTA MODIFICACIÓN

            Serial.println("5");

            // Temporal a modo testeo ----------------------


            // ELIMINAR ESTE BLOQUE CUANDO SE SAQUE A PRODUCCIÓN PARA EVITAR QUE SE REINICIE SOLO SIN INTRODUCIR LA CONTRASEÑA--


            if (timeCounter == 1) {
              timeCounter = 6539;
            }


            // ---------------------------------------------


            Serial.println("------------------------------");
            Serial.println("timeCounter: " + String(timeCounter));
            Serial.println("Minutes: " + String(getMinutes()) + "; seconds: " + String(getSeconds()));
            Serial.println("Time to print = " + getTimeString());

            binaryUpdateTime(getMinutes(), getSeconds());
            sendCommands(client, missConnectionCounter);
            if (missConnectionCounter >= 5) {
              Serial.println("8");
              setExitLoop(true);
              setResetValues(false);
              setConnectionLost(getConnectionLost() + 1);
              Serial.println("\nConnectionCounter reached max amount of attempts.\nLeaving the loop.\nWaiting for client connection to be back.");
            }
            Serial.println("Recovered connection counter: " + String(getConnectionLost()));
            Serial.println("------------------------------");
            currentTime = millis();
            previousTime = currentTime;
            timeCounter--;
          } else {
            currentTime = millis();
          }
        }
        clientCommand(currentLine);
        // Clear buffer --------------------------------
        if (bufferSize > 0) {
          for (int i = 0; i < bufferSize; i++) {
            client.read();
          }
        }
        currentLine = "";
      }
      client.stop();



      Serial.println("9");
      Serial.println("Client Disconnected.\n");
    }

    connectionStateLed();
    if (getExitLoop() == true) {
      Serial.println("11");
      exitLoop = false;
      binaryClose();
    }
  }
}


// Getter and setter --------------

int getMissConnectionCounter() {
  return missConnectionCounter;
}

void setMissConnectionCounter(int newMissConnectionCounter) {
  missConnectionCounter = newMissConnectionCounter;
}

int getAlarmModifier() {
  return alarmModifier;
}

void setAlarmModifier(int newAlarmModifier) {
  alarmModifier = newAlarmModifier;
}

int getConnectionLost() {
  return connectionLost;
}

void setConnectionLost(int newConnectionLost) {
  connectionLost = newConnectionLost;
}

bool getResetValues() {
  return resetValues;
}

void setResetValues(bool newResetValues) {
  resetValues = newResetValues;
}

bool getFailureState() {
  return failureState;
}

void setFailureState(bool newFailureState) {
  failureState = newFailureState;
}

bool getExitLoop() {
  return exitLoop;
}

void setExitLoop(bool newExitLoop) {
  exitLoop = newExitLoop;
}

String getLogText() {
  return logText;
}

void setLogText(String newlogText) {
  logText = newlogText;
}

String getIpChangePassword() {
  return ipChangePassword;
}

void setIpChangePassword(String newIpChangePassword) {
  ipChangePassword = newIpChangePassword;
}

int getMinutes() {
  return int(timeCounter / 60);
}

int getSeconds() {
  return timeCounter - (getMinutes() * 60);
}

String getTimeString() {

  String stringSeconds = "00";
  String stringMinutes = "";
  String stringToPrint = "";

  int minutesCount = getMinutes();
  int secondsCount = getSeconds();

  int minTriggerRange = 5;

  if (minutesCount < minTriggerRange) {
    if (secondsCount < 10) {
      stringSeconds = "0" + String(secondsCount);
    } else {
      stringSeconds = String(secondsCount);
    }
  }
  if (minutesCount < 10) {
    stringMinutes = "00" + String(minutesCount);
  } else if (minutesCount < 100) {
    stringMinutes = "0" + String(minutesCount);
  } else {
    stringMinutes = String(minutesCount);
  }

  stringToPrint = stringMinutes + stringSeconds;

  return stringToPrint;
}

void setHelp() {
  stringHelp = R"=====(

<br>~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~ <br>
<br>&ensp;Type the keyword followed by a '-' character, and the right command, to perform an action.<br>
<br> &ensp;&ensp;* Password change: <br>&ensp;&ensp;&ensp;&ensp;- keyword = 'setpassword'; command = new password. <br>
<br> &ensp;&ensp;* Ip change: <br>&ensp;&ensp;&ensp;&ensp;- keyword = current password; command = new ip. <br>
<br> &ensp;&ensp;* System failure state: <br>&ensp;&ensp;&ensp;&ensp;- keyword = 'systemfailure'; command = on - off. <br>
<br> &ensp;&ensp;* Network set up: <br>&ensp;&ensp;&ensp;&ensp;- keyword = 'changenetwork'; command = [network ssid]-[network password]. <br>
<br> &ensp;&ensp;* To clear the log: <br>&ensp;&ensp;&ensp;&ensp;- keyword = 'cls'. <br>
<br>&ensp;&ensp;Example: changenetwork-myhomenetwork-password1234 <br>
<br>&ensp;&ensp;Example: systemfailure-off <br>
<br> ~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~ <br>
<br>
<br>

)=====";
}

String getHelp() {
  return stringHelp;
}

// Ip -----------------------------

void connectionStateLed() {

  // Signal under 0 = nice signal
  // Signal over or equals to 0 = Not connected.

  int signal = WiFi.RSSI();

  currentTime = millis();
  if (digitalRead(STATE_LED) == HIGH && (signal < 0)) {
    Serial.println("10.1");
    digitalWrite(STATE_LED, LOW);
  } else if (signal < 0 && getResetValues() == false) {
    if ((currentTime - previousTime) >= 10000) {
      Serial.println("10.2.1");
      Serial.println("Testtest1.1");
      systemFailureOn();
      setResetValues(true);
    }
  } else if (signal >= 0) {
    Serial.println("10.3");
    digitalWrite(STATE_LED, HIGH);
    tryConnection(pSsid, pNetworkPassword);
  }
}


void initiativeReset() {
  Serial.println("2");
  if (getResetValues() && !(timeCounter == TIME_COUNTER_DEFAULT)) {
    Serial.println("2.1");
    timeCounter = TIME_COUNTER_DEFAULT;
    previousCounter = timeCounter;
    alarmModifier = ALARM_MODIFIER_DEFAULT;

    if (digitalRead(RELAY) == HIGH) {
      digitalWrite(RELAY, LOW);
      systemFailureOff();
    }

  } else if (getResetValues() == false) {
    Serial.println("2.2");
    setResetValues(true);
  }
  if (getMissConnectionCounter() > 0)
    setMissConnectionCounter(0);
}

// Python commands ----------------

void sendCommands(WiFiClient client, int connectionCounter) {
  Serial.println("6");
  Serial.println("previousCounter = " + String(previousCounter) + ", timeCounter = " + String(timeCounter));
  Serial.println("alarmModifier = " + String(alarmModifier));
  Serial.print("Command sent: ");

  if (timeCounter > FIRST_STAGE) {
    client.write("overFirstStage");
    Serial.println("overFirstStage");
  } else if (((previousCounter - timeCounter) >= alarmModifier) || timeCounter == FIRST_STAGE || timeCounter == SECOND_STAGE || timeCounter == THIRD_STAGE || timeCounter == END_STAGE) {
    if ((timeCounter <= SECOND_STAGE) && (timeCounter >= END_STAGE)) {
      // Block to set the audio as alarm when the time is under the second stage. At the same time it sets the
      // time between each sound to 1 second when the counter is between the end and the third stage.
      if (timeCounter <= THIRD_STAGE && alarmModifier == ALARM_MODIFIER_DEFAULT) {
        alarmModifier = 1;
        Serial.println("Alarm modifier set to = 1");
      }
      client.write("second");
      Serial.println("second");
    } else if (timeCounter < END_STAGE) {
      // It sends the stop command when the counter is under the end stage.
      client.write("stopSystem");
      binaryClose();
      Serial.println("Testtest3");
      systemFailureOn();
    } else {
      client.write("first");
      Serial.println("first");
    }
    previousCounter = timeCounter;
  } else {
    // Normal command to send when the counter is not at any specific state.
    client.write("underFirstStage");
    Serial.println("underFirstStage");
  }
  setMissConnectionCounter(getMissConnectionCounter() + 1);
  Serial.println("MissConnectionCounter incremented = " + String(getMissConnectionCounter()));
}

void clientCommand(String currentLine) {
  if (currentLine.startsWith("alive")) {
    Serial.println("7.1");
    setMissConnectionCounter(0);
  } else if (currentLine.startsWith("passwordReset")) {
    Serial.println("7.2");
    timeCounter = TIME_COUNTER_DEFAULT;
    previousCounter = timeCounter;
    currentTime = millis();
    previousTime = currentTime;
    if (getAlarmModifier() != ALARM_MODIFIER_DEFAULT) {
      setAlarmModifier(ALARM_MODIFIER_DEFAULT);
    }
  } else if (currentLine.startsWith("failureEnabled")) {
    Serial.println("7.3");
    setFailureState(true);
  } else if (currentLine.startsWith("failureDisabled")) {
    Serial.println("7.4");
    setFailureState(false);
  } else if (currentLine.startsWith("failureOn")) {
    Serial.println("7.5");
    setExitLoop(true);
    binaryClose();
    Serial.println("Testtest4");
    systemFailureOn();
  }
}

// Preferences saving -------------

void saveNetworkPreferences(const String newSsid, const String newNetworkPassword) {
  preferences.begin("migration");
  preferences.putString("ssid", newSsid);
  preferences.putString("networkPassword", newNetworkPassword);
  preferences.end();
}

void saveIpPreferences(bool anySavedIp) {
  preferences.begin("migration");
  preferences.putBool("anySavedIp", anySavedIp);
  if (anySavedIp) {

    // const int octetA = octets[0].toInt();
    // const int octetB = octets[1].toInt();
    // const int octetC = octets[2].toInt();
    // const int octetD = octets[3].toInt();

    preferences.putInt("firstOctec", octets[0].toInt());
    preferences.putInt("secondOctec", octets[1].toInt());
    preferences.putInt("thirdOctet", octets[2].toInt());
    preferences.putInt("fourthOctet", octets[3].toInt());
  }
  preferences.end();
}

// Visual related config ----------

void displayRolEffect() {
  mx.clear();

  byte fullLine = 0xFF;
  byte emptyLine = 0x00;

  int randomInt[MAX_DEVICES];
  randomInt[0] = random(0, 8);

  for (int i = 1; i < MAX_DEVICES;) {
    randomInt[i] = random(0, 8);
    for (int j = 0; i < MAX_DEVICES;) {
      if (i != j) {
        if (randomInt[i] != randomInt[j]) {
          if (j++ == MAX_DEVICES) {
            i++;
          }
        } else {
          break;
        }
      } else {
        j++;
      }
    }
  }

  for (int j = 0; j < 128; j++) {
    mx.setRow(4, 4, randomInt[0], fullLine);
    mx.setRow(3, 3, randomInt[1], fullLine);
    mx.setRow(2, 2, randomInt[2], fullLine);
    mx.setRow(1, 1, randomInt[3], fullLine);
    mx.setRow(0, 0, randomInt[4], fullLine);
    delay(15);
    mx.clear();

    for (int i = 0; i < MAX_DEVICES; i++) {
      if (randomInt[i] == 7) {
        randomInt[i] = 0;
      } else {
        randomInt[i]++;
      }
    }
  }
}

void systemFailureOn() {

  displayRolEffect();

  // Toma notas desde https://www.circuitgeeks.com/arduino-max7219-led-matrix-display/
  // Aquí para crear los sprite https://gurgleapps.com/tools/matrix

  // byte rowValues[8] = {B00000000000000000000000000000000, B00100001110000100111110001000000,
  // B01010010001001010001001000100000, B01010010101001010001010100010000, B01010010010001010001001010001110
  // B01000001000000100000100101001000, B01000000111011111001111110001000, B00000000000000000000000000000000};

  byte firstHieroglyph[8] = { 0x00, 0x10, 0x28, 0x28, 0x28, 0x20, 0x20, 0x00 };
  byte secondHieroglyph[8] = { 0x00, 0x30, 0x48, 0x50, 0x40, 0x20, 0x18, 0x00 };
  byte thirdHieroglyph[8] = { 0x00, 0x10, 0x28, 0x28, 0x28, 0x10, 0x7c, 0x00 };
  byte fourthHieroglyph[8] = { 0x00, 0x70, 0x28, 0x24, 0x2a, 0x15, 0x2e, 0x00 };
  byte fifthHieroglyph[8] = { 0x00, 0x40, 0x20, 0x10, 0x0e, 0x08, 0x08, 0x00 };

  for (int i = 0; i < 8; i++) {
    mx.setRow(4, 4, i, firstHieroglyph[i]);
    mx.setRow(3, 3, i, secondHieroglyph[i]);
    mx.setRow(2, 2, i, thirdHieroglyph[i]);
    mx.setRow(1, 1, i, fourthHieroglyph[i]);
    mx.setRow(0, 0, i, fifthHieroglyph[i]);
  }

  if (getFailureState() == true) {  //Revisa el por qué de este condicional. Respuesta: Está para impedir que se active el
    digitalWrite(RELAY, HIGH);      //relé cuando se lance el comando disableFailure. De esta forma solo se activarán los
  }                                 //gráficos, ni se apagará el pc ni se activará el relé.
  exitLoop = true;
  // Revisa si esta línea es redundante teniendo en cuenta que en el método
  // clientConnectionState se actualiza esta variable a true, y justo
  // después se llama a este mismo método, donde se vuelve a actualizar.
}

void printTime() {
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (parola.displayAnimate()) {
      if (parola.getZoneStatus(i)) {
        character[0] = getTimeString().charAt(i);
        parola.displayReset(i);
      }
    }
  }
  parola.displayAnimate();
}

void systemFailureOff() {
  parola.displayClear();
  digitalWrite(RELAY, LOW);
}

void binaryUpdateTime(int minutes, int seconds) {
  int reminder = 0, r = 0, g = 0, rr = 0, gg = 0,
      number = minutes, limit = 7;
  if (minutes > 4) {
    g = 50;
    gg = g - 45;
  } else {
    r = 50;
    rr = r - 45;
  }
  for (int i = 0; i < limit; i++) {
    reminder = int(number % 2);
    if (reminder == 1) {
      pixels.setPixelColor(i, r, g, 0);
    } else {
      pixels.setPixelColor(i, rr, gg, 0);
    }
    pixels.show();
    number /= 2;
    if (i == 6) {
      number = seconds;
      limit = 13;
    }
  }
}

void binaryClose() {
  for (int i = 0; i < 13; i++) {
    pixels.setPixelColor(i, 0, 0, 0);
  }
  pixels.show();
}

// Web service --------------------

void webStartPage(WiFiClient client) {
  // #08fcc3 green blue color
  // #00FF00 light green
  // #000000 black
  // #bdbcbc light gray

  String html = R"=====(

HTTP/1.1 200 OK
Content-type:text/html

<head>
  <title>Dharma Initiative's web service</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body {
      background-color:#000000;
      color: #bdbcbc; 
      font-family:courier
    }
    .customButton{
        background:#000000;
        color:#bdbcbc;
        padding:0;
        border:0;
        text-decoration: underline
    }
    .customTextBox{
        background:#000000;
        color:#bdbcbc;
        border-color:#bdbcbc
    }
    hr{
        color:#ababab;
    }
  </style>
</head>
<body>
  <p style="font-size:30px"> Dharma initiative project web service. Welcome.</p>
  <label>Low performance web service.</label>
  
  <hr>
  
  <form method="POST">
    <label for="getHelp">To get help enter 'help' in the text box, or click </label>
    <button class="customButton" name="ui" value="help">here</button>
    <label for="getHelp">.</label>
    <br>
  </form>
  
  <hr>
  
  <form method="POST">
    <label for="Click">Click </label>
    <button class="customButton" type="submit" name="ui" value="systemfailure-on">here</button>
    <label for="failureSystemOff"> to triguer the system failure mode on.</label>
    <br>
    <label for="Click">Click </label>
    <button class="customButton" type="submit" name="ui" value="systemfailure-off">here</button>
    <label for="failureSystemOff"> to triguer the system failure mode off.</label>
  </form>
  
  <hr >
  
  <form method="POST">
    <input class=customTextBox type="text" id="textbox" name="ui" placeholder="Enter your command" autofocus required>
    <input class="customButton" type="submit" value="Send">
  </form>
  
  <p align="left"> )=====";

  html += getLogText() + R"=====(
    
  </p>
</body>)=====";

  client.println(html);










  // HTTP/1.1 200 OK
  // Content-type:text/html

  // <head>
  //   <title>Dharma Initiative's web service</title>
  // </head>
  // <body style="background-color:#000000; color: #bdbcbc; font-family:courier">
  //   <p style="font-size:30px"> Dharma initiative project web service. Welcome. </p> Low performance web service.
  //   <hr color="#ababab">
  //   <form method="POST">
  //     <label for="getHelp">To get help enter 'help' in the text box, or click </label>
  //     <button style="background:#000000; color:#bdbcbc; padding:0; border:0; text-decoration: underline" name="ui" value="help">here</button>
  //     <label for="getHelp">.</label>
  //     <br>
  //   </form>
  //   <hr color="#ababab">
  //   <form method="POST">
  //     <label for="Click">Click </label>
  //     <button style="background:#000000; color:#bdbcbc; padding:0; border:0; text-decoration: underline" type="submit" name="ui" value="systemfailure-on">here</button>
  //     <label for="failureSystemOff"> to triguer the system failure mode on.</label>
  //     <br>
  //     <label for="Click">Click </label>
  //     <button style="background:#000000; color:#bdbcbc; padding:0; border:0; text-decoration: underline" type="submit" name="ui" value="systemfailure-off">here</button>
  //     <label for="failureSystemOff"> to triguer the system failure mode off.</label>
  //   </form>
  //   <hr color="#ababab">
  //   <form method="POST">
  //     <input type="text" name="ui" placeholder="Enter your command" autofocus required style="background:#000000; color:#bdbcbc; border-color:#bdbcbc">
  //     <input style="background:#000000; color:#bdbcbc; border:0; text-decoration: underline" type="submit" value="Send">
  //   </form>
  //   <p align="left"> )=====";

  //   html += getLogText() + R"=====(

  //   </p>
  // </body>
}

void checkCommand(WiFiClient client, String currentLine) {
  int currentLineLength = currentLine.length();
  if ((currentLineLength > 0) && (currentLine.startsWith("ui="))) {
    currentLine.replace("ui=", "");
    currentLine.replace(" HTTP/1.1", "");
    Serial.println("CurrentLine = " + currentLine);
    for (int i = 0; i < currentLineLength; i++) {
      if (currentLine.charAt(i) == '-') {
        options(currentLine, i);
        break;
      }
      if (i == currentLineLength - 1) {
        currentLine.toLowerCase();
        if (currentLine.equals("cls")) {
          setLogText("");
        } else if (currentLine.equals("help")) {
          setLogText(getHelp() + getLogText());
        } else {
          setLogText("Not valid input.<br><br>" + getLogText());
          Serial.println("Not valid input.");
        }
      }
    }
  }
}

void options(String currentLine, int dashPosition) {
  Serial.println("Inside options");
  String option, info;
  bool validOption = true;
  option = currentLine.substring(0, dashPosition);
  currentLine.remove(0, dashPosition + 1);
  int currentLineLength = currentLine.length();
  Serial.println("Option removed current line = " + currentLine);
  if (option.equals(getIpChangePassword())) {
    int octetsCount = 0;
    int digitsCount = 0;
    bool amountOfDigits = true;
    bool octetsRange = true;
    for (int i = 0; i < currentLineLength; i++) {
      if (isdigit(currentLine.charAt(i))) {
        digitsCount++;
        if (digitsCount > 3) {
          amountOfDigits = false;
          break;
        }
        octets[octetsCount] += currentLine.charAt(i);
        if (octets[octetsCount].toInt() > 255) {
          octetsRange = false;
          break;
        }
      } else if (currentLine.charAt(i) == '.') {
        digitsCount = 0;
        octetsCount++;
      } else {
        break;
      }
    }
    if (octetsCount == 3 && amountOfDigits && octetsRange) {
      newIpConfig();
      info = "Incoming ip = " + currentLine + "; Ip string lenght, (dots included) = "
             + currentLineLength + ".<br>New ip: " + WiFi.localIP().toString() + ".<br>Ip changed.<br>";
      Serial.println("*" + currentLine + "* " + currentLineLength + ". New ip: "
                     + WiFi.localIP().toString() + "\nIp changed.");

      saveIpPreferences(true);
    } else {
      for (int i = 0; i < ARRAY_SIZE; i++) {
        octets[i] = "";
      }
      info += "Not valid ip.<br>";
      Serial.println("Not valid ip.");
    }

  }

  else if (option.equals("setpassword")) {
    if (currentLineLength > 0) {
      setIpChangePassword(currentLine);
      info = "Password changed.<br>";  //---------------------
      Serial.println("Password Changed.");
    } else {
      info = "Password too short.<br>";
      Serial.println("Password too short.");
    }
  }

  else if (option.equals("systemfailure")) {
    if (currentLine == "on") {
      Serial.println("Testtest5");
      systemFailureOn();
      info = "System Failure on.<br>";
      Serial.println("System failure on.");
    } else if (currentLine == "off") {
      systemFailureOff();
      info = "System failure off.<br>";
      Serial.println("System failure off.");
    } else {
      info = "No valid option.<br>";
      Serial.println("No valid option.");
    }
  }

  else if (option.equals("changenetwork")) {
    char *cssid, cwifiPassword;
    Serial.println("Inside changenetwork");
    for (int i = 0; i < currentLine.length(); i++) {
      if (currentLine.charAt(i) == '-') {
        String ssidString = currentLine.substring(0, i);
        if (ssidString.equals(WiFi.SSID())) {
          info = "Already connected to the requested network.<br>";
        } else {
          int currentTime = millis(), previousTime = currentTime;
          String wifiPasswordString = currentLine.substring(i + 1);
          Serial.println("**");
          Serial.println(ssidString);
          Serial.println(wifiPasswordString);
          Serial.println("**");
          digitalWrite(STATE_LED, HIGH);

          //-------------------------------------------------------------------------

          tryConnection(ssidString, wifiPasswordString);
          saveIpPreferences(false);

          //-------------------------------------------------------------------------

          digitalWrite(STATE_LED, LOW);
          String newNetwork = WiFi.SSID();
          Serial.println("Network requested: " + ssidString);
          if (!(ssidString.equals(newNetwork))) {
            info = "The requested network connection was not established.<br>Conected to " + newNetwork + ".<br>";
          } else {
            info = "Conected to " + newNetwork + ".<br>";
          }
          Serial.println("Conected to " + newNetwork + ".\n");
          break;
        }
      }
    }
  }

  else {
    validOption = false;
    setLogText("Not valid input.<br><br>" + getLogText());
    Serial.println("Not valid input.");
  }
  if (validOption) {
    setLogText("Option = " + option + "; request = " + currentLine + "<br>" + info + "<br>" + getLogText());  //---------------------
  }
}

// Ip setup -----------------------

void tryConnection(String ssidString, String wifiPasswordString) {
  String states[] = { "WL_IDLE_STATUS", "WL_NO_SSID_AVAIL", "WL_SCAN_COMPLETED", "WL_CONNECTED", "WL_CONNECT_FAILED",
                      "WL_CONNECTION_LOST", "WL_DISCONNECTED" };
  int present = millis(), past = present, pastDot = present;
  bool iterateLoop = true;
  const char* ssidAttempt = ssidString.c_str();
  const char* wifiPasswordAttempt = wifiPasswordString.c_str();
  int networkState = WiFi.begin(ssidAttempt, wifiPasswordAttempt);
  bool firstAttempt = true;
  while (iterateLoop) {
    if (present - past >= 5000) {
      if (getResetValues() == false & firstAttempt == false) {
        Serial.println("Testtest2");
        systemFailureOn();
        setResetValues(true);
      }
      firstAttempt = false;

      present = millis();
      past = present;
      pastDot = present;
      networkState = WiFi.begin(ssidAttempt, wifiPasswordAttempt);
      Serial.print(":");
    } else {
      if (digitalRead(RESET_BUTTON)) {
        Serial.println("\nNetwork reset requested.");
        ssidString = "network";
        wifiPasswordString = "network12345";
        Serial.println("\n----** Network **----\n" + ssidString + "\n" + wifiPasswordString + "\n----*************----");
        saveIpPreferences(false);
        saveNetworkPreferences(ssidString.c_str(), wifiPasswordString.c_str());
        ESP.restart();
      }
      if (present - pastDot >= SECOND) {
        pastDot = present;
        Serial.print(".");
      }
      present = millis();
      networkState = WiFi.status();
      if ((networkState == WL_CONNECTED) && (WiFi.localIP().toString() != "0.0.0.0")) {
        String ip = WiFi.localIP().toString();

        //Se ha añadido  "&& !(ip.equals("192.168.1.138"))" al siguiente condicional.
        if (ip.startsWith("192.168.1.") && !(ip.equals("192.168.1.138"))) {
          Serial.println("\nDefault ip: " + ip);
          IPAddress staticIP(192, 168, 1, 138);
          IPAddress gateway(192, 168, 17, 254);
          IPAddress subnet(255, 255, 255, 0);
          WiFi.config(staticIP, gateway, subnet);
        }
        iterateLoop = false;
      }
    }
  }
  Serial.println("\nConexion status = " + states[WiFi.status()] + ".\nIP address: " + WiFi.localIP().toString() + "\nSsid = " + ssidAttempt + ".\n");
  saveNetworkPreferences(ssidAttempt, wifiPasswordAttempt);
}

void newIpConfig() {
  Serial.println("Ip change started.");
  IPAddress staticIP(octets[0].toInt(), octets[1].toInt(),
                     octets[2].toInt(), octets[3].toInt());
  IPAddress gateway(192, 168, 17, 254);
  IPAddress subnet(255, 255, 0, 0);

  WiFi.config(staticIP, gateway, subnet);
}

void preferencesIpConfig() {
  preferences.begin("migration");
  if (preferences.getBool("anySavedIp", false)) {
    IPAddress staticIP(preferences.getInt("firstOctec"),
                       preferences.getInt("secondOctec"),
                       preferences.getInt("thirdOctet"),
                       preferences.getInt("fourthOctet"));
    IPAddress gateway(192, 168, 17, 254);
    IPAddress subnet(255, 255, 0, 0);
    WiFi.config(staticIP, gateway, subnet);

    Serial.println("Loaded the ip configuration burnt in the preferences.\nNew ip addess: " + WiFi.localIP().toString() + ".");
  }
  preferences.end();
}

/*
 * bird feeder sketch
 * almost all libraries avalible in Arduino library manager Sketch -> Include Library -> Manage Libraries
 * one library you need to install manually is SimpleTimer
 * to install it please go to the arduino libraries directory (Documents\Arduino\libraries for Windows) and run "git clone https://github.com/jfturcot/SimpleTimer.git"
 */


// Select your modem:
#define TINY_GSM_MODEM_SIM800

#include <TinyGsmClient.h> //library to use sim800 with AT commands
#include <PubSubClient.h> //library for mqtt
#include <AccelStepper.h> //library for controlling the stepper motor
#include <SimpleTimer.h> //to make some async tasks
#include "LowPower.h" //helps arduino to save some power
#include <SoftwareSerial.h> // use Software Serial on Uno, Nano

// Motor pin definitions
#define motorPin1  5     // IN1 on the ULN2003 driver 1
#define motorPin2  6     // IN2 on the ULN2003 driver 1
#define motorPin3  7     // IN3 on the ULN2003 driver 1
#define motorPin4  8     // IN4 on the ULN2003 driver 1

#define STEPS_PER_TURN 2048  //steps to make full turn of motor
#define STEPPER_MAX_TURNS 20 

#define MODEM_RESET_PIN 4

//uncomment if you want to see the at commands dump in serial
//#define DUMP_AT_COMMANDS

#define SLEEP_PERIOD_SEC 300

#define ADC_PIN 3 //pin to read voltage on battery

// Initialize 4 FULL4WIRE with pin sequence IN1-IN3-IN2-IN4 for using the AccelStepper with 28BYJ-48
AccelStepper stepper(AccelStepper::FULL4WIRE, motorPin1, motorPin3, motorPin2, motorPin4);

// Your GPRS credentials
// Leave empty, if missing user or pass
// please use apn for your carrier 
const char apn[]  = "www.sample.net";
const char user[] = "";
const char pass[] = "";

// the timer object
SimpleTimer wait_publish;

//used to decribe the current state of the device
enum feeder_state {
  state_modem_sleep,
  state_arduino_sleep,
  state_mqtt,
  state_rotating
};

// used to store the state
feeder_state feeder_state = state_mqtt;

int sleep_time;
boolean is_data_received;

SoftwareSerial SerialAT(2, 3); // RX, TX

//helps to debug the modem
#ifdef DUMP_AT_COMMANDS
#define SerialMon Serial
#define TINY_GSM_DEBUG SerialMon
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

TinyGsmClient client(modem);
PubSubClient mqtt(client);

//fill this values
const char* broker = "www.cloudmqtt.com";
#define BROKER_PORT 1883
const char* mqtt_username = "username";
const char* mqtt_pass = "password";

const char* main_topic = "feeder";
const char* topic_response = "feeder_response";
const char* topic_wakeup = "feeder_wakeup";

long lastReconnectAttempt = 0;


/*
 * setup
 */
void setup() {
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, HIGH);
  // Set console baud rate
  Serial.begin(115200);

  // Set GSM module baud rate
  SerialAT.begin(9600);
  delay(1000);
  start_gsm_module();
  setup_stepper();
}

/*
 * start the gsm module for the first time
 * print some data about modem
 */
void start_gsm_module() {
  // Restart takes quite some time
  // To skip it, call init() instead of restart()
  Serial.println("Initializing modem...");
  modem.restart();

  String modemInfo = modem.getModemInfo();
  Serial.print("Modem: ");
  Serial.println(modemInfo);
  connect_modem();
}

/*
 * setup the settings of the stepper
 * disabling the outputs to prevent power loss
 */
void setup_stepper() {
  stepper.setMaxSpeed(100);
  stepper.setAcceleration(100);
  stepper.disableOutputs();
}

/*
 * begin the connection to the GPRS 
 */
void connect_modem() {
  // Unlock your SIM card with a PIN if it is locked
  //modem.simUnlock("1234");

  Serial.print("Waiting for network...");
  if (!modem.waitForNetwork()) {
    Serial.println(" fail");
    modem_sleep_mode();
    feeder_state = state_arduino_sleep;
    return;
  }
  Serial.println(" OK");

  Serial.print("Connecting to ");
  Serial.print(apn);
  if (!modem.gprsConnect(apn, user, pass)) {
    Serial.println(" fail");
    modem_sleep_mode();
    feeder_state = state_arduino_sleep;
    return;
  }
  Serial.println(" OK");

  // MQTT Broker setup
  mqtt.setServer(broker, BROKER_PORT);
  mqtt.setCallback(mqttCallback);
}

/*
 * make modem sleep to save some power
 */
void modem_sleep_mode() {
  Serial.println("making modem sleep");
  feeder_state = state_modem_sleep;
  mqtt.disconnect();

  Serial.println("mqtt disconnected");
  delay(50);
  modem.gprsDisconnect();

  Serial.println("gprs disconnected");
  delay(50);
  SerialAT.write("AT\r\n");
  delay(50);
  SerialAT.write("AT+CSCLK=2\r\n");
}


/*
 * wakeup modem from sleep
 * Sending some AT commands manually
 */
void modem_wake_up() {
  Serial.println("waking up the modem");
  //reset the modem by trigering the reset pin
  digitalWrite(MODEM_RESET_PIN, LOW);
  delay(50);
  digitalWrite(MODEM_RESET_PIN, HIGH);
  feeder_state = state_mqtt;
  SerialAT.write("AT\r\n");
  SerialAT.write("AT+CSCLK=0\r\n");
  connect_modem();
}

/*
 * make modem connect to the broker
 * supscribe on topic to receive the command
 */
boolean mqttConnect() {
  Serial.print("Connecting to ");
  Serial.print(broker);
  if (!mqtt.connect("birdfeeder_demo",mqtt_username,mqtt_pass)) {
    Serial.println(" fail");
    return false;
  }
  Serial.println(" OK");
  mqtt.subscribe(main_topic);
  return mqtt.connected();
}


/*
 * main loop
 * it executes different commands depending on the controller state stored in feeder_state enum
 */
void loop() {
  wait_publish.run();
  switch (feeder_state) {
    case state_mqtt:
      if (mqtt.connected()) {
        mqtt.loop();
      } else {
        if (mqttConnect()) {
          lastReconnectAttempt = millis();
          is_data_received = false;
          //check retained message received in one second
          mqtt.publish(topic_wakeup, String(analogRead(ADC_PIN)).c_str());
          wait_publish.setTimeout(1000, check_data_received);
        } else {
          modem_sleep_mode();
          feeder_state = state_arduino_sleep;
          return;
        }
      }
      break;
    case state_arduino_sleep:


      Serial.print("in sleep mode for ");
      Serial.print (sleep_time);
      Serial.println(" seconds");
      delay(20);
      //sleep for 8 sec
      LowPower.idle(SLEEP_8S, ADC_OFF, TIMER2_OFF, TIMER1_OFF, TIMER0_OFF,
                    SPI_OFF, USART0_OFF, TWI_OFF);
      delay(20);
      sleep_time += 8;
      if (sleep_time >= SLEEP_PERIOD_SEC) {
        sleep_time = 0;
        modem_wake_up();
      }
      break;
    case state_rotating:
      stepper.run();
      if (stepper.distanceToGo() == 0) {
        stepper.disableOutputs();
        feeder_state = state_arduino_sleep;
      }
      break;
  }
}


/*
 * simple timer callback
 * it executes after some time since modem is subscribed to topic and if retained message is not received device goes to sleep
 */
void check_data_received() {
  Serial.print("checking data received...");
  if (is_data_received) {
    Serial.println("true");
  } else {
    Serial.println("false");
    modem_sleep_mode();
    feeder_state = state_arduino_sleep;
  }
}


/*
 * mqtt callback
 * handling of received mqtt messanges
 */
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.write(payload, len);
  Serial.println();

  // Only proceed if incoming message's topic matches
  if (String(topic) == main_topic) {
    payload[len] = '\0';
    String s = String((char*)payload);
    int resultInt = s.toInt();
    Serial.print("received string data: ");
    Serial.println(s);
    Serial.print("received numeric data: ");
    Serial.println(resultInt);
    if (resultInt > 0 && resultInt <= STEPPER_MAX_TURNS) {
      is_data_received = true;
      mqtt.unsubscribe(main_topic);
      mqtt.publish(main_topic, "", 0, true);
      mqtt.publish(topic_response, "rotation command received", true);
      beginRotation(resultInt);
    }
  }
}

void beginRotation(int turns) {
  Serial.println("in beginRotation");
  modem_sleep_mode();
  Serial.print("waiting for modem to go to sleep... ");
  delay(6000);
  Serial.println("starting rotation");
  Serial.print(turns);
  Serial.println(" turns");
  Serial.print(STEPS_PER_TURN);
  Serial.println(" STEPS_PER_TURN");
  int steps = turns * STEPS_PER_TURN;
  Serial.print(steps);
  Serial.println(" steps");
  stepper.setCurrentPosition(0);
  stepper.enableOutputs();
  stepper.moveTo(steps);
  feeder_state = state_rotating;
}


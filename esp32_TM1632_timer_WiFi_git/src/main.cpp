#include <Arduino.h>
#include <TM1637Display.h>
#define LED_PIN   23
#define BUTTON_PIN 18
#define MODE_PIN 14
#define CLK 17
#define DIO 16

#include <WiFi.h>
#include <esp_sntp.h>  //for sntp_sync_status 

const char* ssid     = "**********";
const char* password = "**********";

const char* ntpServer1 = "ntp.nict.jp";
const char* ntpServer2 = "time.google.com";
const char* ntpServer3 = "ntp.jst.mfeed.ad.jp";
const long  gmtOffset_sec = 9 * 3600;
const int   daylightOffset_sec = 0;

bool timeset = 0;

hw_timer_t * timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

volatile uint32_t lastIsrAt = 0;
uint32_t resetTime = 0;
uint32_t isrTime = 0;

uint8_t mode = 0;
// mode = 0 時計
// mode = 1 タイマー 分秒
// mode = 2 タイマー 時分

uint8_t tmState = 0;
// state = 0 リセット中
// state = 1 計測中
// state = 2 停止中

uint8_t buttonSt = 0; //0ボタン押されてない
uint8_t modeSt = 0; //0ボタン押されてない

struct tm timeInfo;


// put function declarations here:
// 現在時刻表示
void showLocalTime()
{
  char str[256];
  static const char *wd[7] = { "日", "月", "火", "水", "木", "金", "土" };
  unsigned long m;

  // getLocalTimeを使用して現在時刻取得
  m = millis();
  getLocalTime(&timeInfo);  
  sprintf(str, "[getLocalTime  ] %04d/%02d/%02d(%s) %02d:%02d:%02d : %d (ms)", timeInfo.tm_year+1900, timeInfo.tm_mon+1, timeInfo.tm_mday, wd[timeInfo.tm_wday], timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec, millis()-m);
  Serial.println(str);
}

void timeavailable(struct timeval *t)
{
  Serial.println("Got time adjustment from NTP!");
  timeset = 1;
}

void ARDUINO_ISR_ATTR onTimer(){
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  //lastIsrAt = millis();  
  portEXIT_CRITICAL_ISR(&timerMux);
  // Give a semaphore that we can check in the loop
  xSemaphoreGiveFromISR(timerSemaphore, NULL);
  // It is safe to use digitalRead/Write here if you want to toggle an output
}

TM1637Display display(CLK, DIO);

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(100);
  Serial.printf("%s - run\n",__func__);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(MODE_PIN, INPUT_PULLUP);

  // Wifi接続処理
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  // NTP
  sntp_set_time_sync_notification_cb( timeavailable );
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);

  // 時刻設定後の時刻取得
  while( !timeset ) {
    delay(1000);
  }
  Serial.println("<<時刻設定後の時刻取得>>");
  for(int i=0;i<1;i++) {
    showLocalTime();
    delay(1000);
  }

  lastIsrAt = millis();
  resetTime = lastIsrAt;

  // Create semaphore to inform us when the timer has fired
  timerSemaphore = xSemaphoreCreateBinary();

  // Use 1st timer of 4 (counted from zero).
  // Set 80 divider for prescaler (see ESP32 Technical Reference Manual for more
  // info).
  timer = timerBegin(0, 80, true);

  // Attach onTimer function to our timer.
  timerAttachInterrupt(timer, &onTimer, true);

  // Set alarm to call onTimer function every second (value in microseconds).
  // Repeat the alarm (third parameter)
  timerAlarmWrite(timer, 1000000, true);

  // Start an alarm
  timerAlarmEnable(timer);

  display.setBrightness(0x0f);


}

void loop() {
  // put your main code here, to run repeatedly:

  uint32_t countsec;
  uint32_t countmin;
  uint32_t counthour;
  uint8_t sec;
  uint8_t min;
  uint8_t hour;


  if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE){
    // Read the interrupt count and time
    portENTER_CRITICAL(&timerMux);
    if(tmState == 1){
      isrTime += 1000;
    }else if(tmState == 0){
      isrTime = 0;  //reset
    }
    portEXIT_CRITICAL(&timerMux);  

    //tm.tm_hour, tm.tm_min, tm.tm_sec
    if(mode == 0){
      getLocalTime(&timeInfo);  
      display.showNumberDecEx((timeInfo.tm_hour*100)+timeInfo.tm_min, (0x80 >> 1), true); //Hour:Min  
    }else{  
      countsec = (isrTime) / 1000;
      sec = countsec % 60;
      countmin = (countsec - sec) /60;
      min = countmin % 60;
      counthour = (countmin-min)/60;
      hour = counthour % 24;
      if(mode == 1){
        display.showNumberDecEx((min*100)+sec, (0x80 >> 1), true); //Min:Sec
      }else if(mode == 2){
        display.showNumberDecEx((hour*100)+min, (0x80 >> 1), true); //Hour:Min
        digitalWrite(LED_PIN, HIGH);
        delay(50);
        digitalWrite(LED_PIN, LOW);
        delay(50);
      }
      digitalWrite(LED_PIN, HIGH);
      delay(50);
      digitalWrite(LED_PIN, LOW);
      }
  }

  if(mode !=0){
    if (digitalRead(BUTTON_PIN) == LOW) { // 18番ピンがLoの場合
      if(buttonSt == 0){
        //state ++;
        if(tmState >= 2){
          tmState = 1;
        }else if(tmState == 1){
          tmState = 2;
        }else if(tmState == 0){
          tmState = 1;
        }
        buttonSt = 1;
      }else{
        tmState = 0;
      }   
      delay(500);
    }else{
      buttonSt = 0;
    }
  } 

  if (digitalRead(MODE_PIN) == LOW) { // 14番ピンがLoの場合
    if(modeSt == 0){
      if(mode >= 2){
        mode = 1;
      }else if(mode == 1){
        mode = 2;
      }else if(mode == 0){
        mode = 1;
      }
      modeSt = 1;
    }else{
      mode = 0;
    }
    delay(500);
  }else{
    modeSt = 0;
  } 
  
}

// put function definitions here:

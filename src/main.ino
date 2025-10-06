/* main.ino
   ESP32 + 2x P10 RGB (HUB75, 64x16) Morphing Clock + WebUI (AP mode)
   RTC: DS3231
   Buzzer: pin 32 (optional beep on minute change)
   Brightness default: 75%
   WiFi AP SSID/password from build_flags
*/

#include <PxMatrix.h>
#include <Adafruit_GFX.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#ifndef WIFI_SSID
  #define WIFI_SSID "morphing clock"
#endif
#ifndef WIFI_PASS
  #define WIFI_PASS "12345678"
#endif

// HUB75 pin mapping (common adapter) — adjust if your adapter differs
#define P_LAT  26
#define P_A    21
#define P_B    22
#define P_C    23
#define P_D    -1   // not used for 16-row panels
#define P_OE   27
#define P_CLK  25

#define R1 16
#define G1 4
#define B1 17
#define R2 5
#define G2 18
#define B2 19

// Panel geometry
const uint16_t WIDTH = 64;
const uint16_t HEIGHT = 16;

PxMATRIX display(WIDTH, HEIGHT, R1, G1, B1, R2, G2, B2, P_LAT, P_OE, P_CLK, P_A, P_B, P_C, P_D);
RTC_DS3231 rtc;
WebServer server(80);
Preferences prefs;

int brightnessPct = 75;
bool morphingEnabled = true;
int colorMode = 0; // 0=multi,1=red,2=green,3=blue
bool showStateIsTime = true;
unsigned long lastStateSwitch = 0;
unsigned long switchInterval = 10000; // 10s per state
bool bufA[WIDTH][HEIGHT];
bool bufB[WIDTH][HEIGHT];

const int BUZZER_PIN = 32;
int lastMinute = -1;

uint16_t colorFromMode(int mode){
  if(mode==1) return display.color565(255,0,0);
  if(mode==2) return display.color565(0,255,0);
  if(mode==3) return display.color565(0,0,255);
  static uint32_t phase = 0; phase++;
  uint8_t r = (phase*7) & 255;
  uint8_t g = (phase*13) & 255;
  uint8_t b = (phase*19) & 255;
  return display.color565(r,g,b);
}

// Web UI
void handleRoot(){
  String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'/><title>Jam Morphing</title></head><body>";
  html += "<h3>Jam Morphing - Pengaturan</h3>";
  html += "<form action='/save' method='get'>";
  html += "Brightness: <input type='number' name='b' value='" + String(brightnessPct) + "' min='0' max='100'><br/>";
  html += "Morphing: <select name='m'><option value='1'>On</option><option value='0'>Off</option></select><br/>";
  html += "Color: <select name='c'><option value='0'>Multi</option><option value='1'>Red</option><option value='2'>Green</option><option value='3'>Blue</option></select><br/>";
  html += "Switch Interval (s): <input type='number' name='i' value='" + String(switchInterval/1000) + "' min='2' max='60'><br/>";
  html += "<input type='submit' value='Save'></form>";
  html += "<p>SSID: " + String(WIFI_SSID) + " (pass: " + String(WIFI_PASS) + ")</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSave(){
  if(server.hasArg("b")) brightnessPct = constrain(server.arg("b").toInt(), 0, 100);
  if(server.hasArg("m")) morphingEnabled = (server.arg("m").toInt() != 0);
  if(server.hasArg("c")) colorMode = server.arg("c").toInt();
  if(server.hasArg("i")) {
    int v = server.arg("i").toInt();
    switchInterval = (unsigned long)constrain(v,2,60) * 1000UL;
  }
  prefs.putInt("bright", brightnessPct);
  prefs.putBool("morph", morphingEnabled);
  prefs.putInt("color", colorMode);
  prefs.putULong("interval", switchInterval);
  display.setBrightness(map(brightnessPct,0,100,0,255));
  server.sendHeader("Location", "/");
  server.send(303);
}

void notFound(){ server.send(404, "text/plain", "Not found"); }

void buildBitmapForString(const String &s, bool buf[WIDTH][HEIGHT]){
  // clear
  for(int x=0;x<WIDTH;x++) for(int y=0;y<HEIGHT;y++) buf[x][y]=false;
  if(s.length()==0) return;
  GFXcanvas16 canvas(WIDTH, HEIGHT);
  canvas.fillScreen(0);
  canvas.setTextSize(1);
  int16_t x1,y1; uint16_t w,h;
  canvas.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  int startX = (WIDTH - w)/2;
  int startY = (HEIGHT + h)/2 - 1;
  canvas.setCursor(startX, startY);
  canvas.print(s);
  uint16_t *buf16 = canvas.getBuffer();
  for(int yy=0; yy<HEIGHT; yy++){
    for(int xx=0; xx<WIDTH; xx++){
      buf[xx][yy] = (buf16[yy*WIDTH + xx] != 0);
    }
  }
}

void morphBuffers(bool fromBuf[WIDTH][HEIGHT], bool toBuf[WIDTH][HEIGHT], uint16_t col){
  struct P{uint8_t x,y;};
  static P list[1024];
  int count=0;
  for(int x=0;x<WIDTH;x++) for(int y=0;y<HEIGHT;y++) if(fromBuf[x][y] != toBuf[x][y]) list[count++] = { (uint8_t)x, (uint8_t)y };
  if(count==0) return;
  int steps = max(6, count/8);
  int perFrame = (count + steps -1)/steps;
  for(int i=count-1;i>0;i--){ int j = random(i+1); auto t = list[i]; list[i]=list[j]; list[j]=t; }
  int idx=0;
  for(int f=0; f<steps; f++){
    display.fillScreen(0);
    for(int i=0;i<idx;i++) if(toBuf[list[i].x][list[i].y]) display.drawPixel(list[i].x, list[i].y, col);
    for(int x=0;x<WIDTH;x++) for(int y=0;y<HEIGHT;y++){
      bool changed=false;
      for(int k=0;k<idx;k++) if(list[k].x==x && list[k].y==y){ changed=true; break; }
      if(!changed && fromBuf[x][y]) display.drawPixel(x,y,col);
    }
    display.showBuffer();
    idx += perFrame; if(idx>count) idx=count;
    delay(60);
  }
  // final
  display.fillScreen(0);
  for(int x=0;x<WIDTH;x++) for(int y=0;y<HEIGHT;y++) if(toBuf[x][y]) display.drawPixel(x,y,col);
  display.showBuffer();
}

void setup(){
  Serial.begin(115200);
  delay(50);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  display.begin(16);
  prefs.begin("morphcfg", false);
  brightnessPct = prefs.getInt("bright", brightnessPct);
  morphingEnabled = prefs.getBool("morph", morphingEnabled);
  colorMode = prefs.getInt("color", colorMode);
  switchInterval = prefs.getULong("interval", switchInterval);
  display.setBrightness(map(brightnessPct,0,100,0,255));

  if(!rtc.begin()) Serial.println("RTC not found");
  if(rtc.lostPower()) {
    Serial.println("RTC lost power");
  }

  // Start AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP started: "); Serial.println(ip);

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.onNotFound(notFound);
  server.begin();

  // init buffers
  for(int x=0;x<WIDTH;x++) for(int y=0;y<HEIGHT;y++){ bufA[x][y]=false; bufB[x][y]=false; }

  // initial display
  buildBitmapForString("--:--", bufA);
  display.fillScreen(0);
  display.showBuffer();

  DateTime now = rtc.now();
  lastMinute = now.minute();
}

void loop(){
  server.handleClient();

  // update every second for minute change
  DateTime now = rtc.now();

  if(now.minute() != lastMinute){
    lastMinute = now.minute();
    // beep once on minute change
    tone(BUZZER_PIN, 2000, 60);
  }

  unsigned long t = millis();
  if(t - lastStateSwitch >= switchInterval){
    lastStateSwitch = t;
    showStateIsTime = !showStateIsTime;
    // build new buffer
    String s;
    if(showStateIsTime){
      char tmp[9]; sprintf(tmp, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
      s = String(tmp);
    } else {
      char tmp[32]; sprintf(tmp, "%02d-%02d-%04d", now.day(), now.month(), now.year());
      s = String(tmp);
    }
    buildBitmapForString(s, bufB);
    if(morphingEnabled) morphBuffers(bufA, bufB, colorFromMode(colorMode));
    // copy
    for(int x=0;x<WIDTH;x++) for(int y=0;y<HEIGHT;y++) bufA[x][y]=bufB[x][y];
  }

  // draw current buffer
  display.fillScreen(0);
  uint16_t col = colorFromMode(colorMode);
  for(int x=0;x<WIDTH;x++) for(int y=0;y<HEIGHT;y++) if(bufA[x][y]) display.drawPixel(x,y,col);
  display.showBuffer();

  delay(50);
}

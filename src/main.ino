\
/*
  ESP32 P10 RGB Morphing Clock - AP Mode (PlatformIO Arduino)
  - Board: ESP32 DevKit V1
  - 2x P10 RGB HUB75 horizontal (64x16)
  - RTC: DS3231 (I2C SDA=21 SCL=22)
  - WiFi AP: SSID = Clock_AP, PASS = 12345678
  - Brightness default 75%
  - Morphing effect enabled, colors mixed
*/

#include <PxMatrix.h>
#include <Adafruit_GFX.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// AP credentials
const char* AP_SSID = "Clock_AP";
const char* AP_PASS = "12345678";

// Timezone label (display only)
const char* TZ_LABEL = "Asia/Jakarta";

// Display pins (standard HUB75 adapter mapping)
#define P_LAT  26
#define P_A    21
#define P_B    22
#define P_C    23
#define P_D    -1
#define P_OE   27
#define P_CLK  25

#define R1 16
#define G1 4
#define B1 17
#define R2 5
#define G2 18
#define B2 19

const uint16_t WIDTH = 64;
const uint16_t HEIGHT = 16;

PxMATRIX display(WIDTH, HEIGHT, R1, G1, B1, R2, G2, B2, P_LAT, P_OE, P_CLK, P_A, P_B, P_C, P_D);
RTC_DS3231 rtc;
WebServer server(80);
Preferences prefs;

int brightnessPct = 75;
bool morphingEnabled = true;
int colorMode = 0; // 0 multi, 1 red, 2 green, 3 blue

unsigned long lastStateChange = 0;
int showState = 0; // 0=time,1=date

bool bufA[WIDTH][HEIGHT];
bool bufB[WIDTH][HEIGHT];

uint16_t colorFromMode(int mode){
  if(mode==1) return display.color565(255,0,0);
  if(mode==2) return display.color565(0,255,0);
  if(mode==3) return display.color565(0,0,255);
  static uint8_t phase = 0; phase++;
  uint8_t r = (phase*7)%256;
  uint8_t g = (phase*13)%256;
  uint8_t b = (phase*19)%256;
  return display.color565(r,g,b);
}

void handleRoot(){
  String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'/><title>Jam Morphing</title></head><body>";
  html += "<h3>Jam Morphing - Pengaturan</h3>";
  html += "<form action='/save' method='get'>";
  html += "Brightness: <input type='number' name='b' value='" + String(brightnessPct) + "' min='0' max='100'><br/>";
  html += "Morphing: <select name='m'><option value='1'>On</option><option value='0'>Off</option></select><br/>";
  html += "Color: <select name='c'><option value='0'>Multi</option><option value='1'>Red</option><option value='2'>Green</option><option value='3'>Blue</option></select><br/>";
  html += "<input type='submit' value='Save'></form>";
  html += "<p>SSID: " + String(AP_SSID) + " (pass: " + String(AP_PASS) + ")</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSave(){
  if(server.hasArg("b")) brightnessPct = constrain(server.arg("b").toInt(),0,100);
  if(server.hasArg("m")) morphingEnabled = (server.arg("m").toInt() != 0);
  if(server.hasArg("c")) colorMode = server.arg("c").toInt();
  prefs.putInt("bright", brightnessPct);
  prefs.putBool("morph", morphingEnabled);
  prefs.putInt("color", colorMode);
  display.setBrightness(map(brightnessPct,0,100,0,255));
  server.sendHeader("Location","/");
  server.send(303);
}

void notFound(){ server.send(404, "text/plain", "Not found"); }

void buildBitmapForString(const String &s, bool buf[WIDTH][HEIGHT]){
  for(int x=0;x<WIDTH;x++) for(int y=0;y<HEIGHT;y++) buf[x][y]=false;
  if(s.length()==0) return;
  GFXcanvas16 canvas(WIDTH, HEIGHT);
  canvas.fillScreen(0);
  canvas.setTextSize(1);
  int16_t x1,y1; uint16_t w,h;
  canvas.getTextBounds(s,0,0,&x1,&y1,&w,&h);
  int16_t startX = (WIDTH - w)/2;
  int16_t startY = (HEIGHT + h)/2 - 1;
  canvas.setCursor(startX,startY);
  canvas.print(s);
  uint16_t *buff = canvas.getBuffer();
  for(int yy=0; yy<HEIGHT; yy++) for(int xx=0; xx<WIDTH; xx++) buf[xx][yy] = (buff[yy*WIDTH + xx] != 0);
}

void morphBuffers(bool fromBuf[WIDTH][HEIGHT], bool toBuf[WIDTH][HEIGHT], uint16_t col){
  struct P{uint8_t x,y;};
  static P list[1024];
  int count=0;
  for(int x=0;x<WIDTH;x++) for(int y=0;y<HEIGHT;y++) if(fromBuf[x][y] != toBuf[x][y]) list[count++] = { (uint8_t)x, (uint8_t)y };
  if(count==0) return;
  int steps = max(8, count/6);
  int perFrame = (count + steps -1)/steps;
  for(int i=count-1;i>0;i--){ int j = random(i+1); P t = list[i]; list[i]=list[j]; list[j]=t; }
  int idx=0;
  for(int f=0; f<steps; f++){
    display.fillScreen(0);
    for(int i=0;i<idx;i++) if(toBuf[list[i].x][list[i].y]) display.drawPixel(list[i].x, list[i].y, col);
    for(int x=0;x<WIDTH;x++) for(int y=0;y<HEIGHT;y++){ bool changed=false; for(int k=0;k<idx;k++) if(list[k].x==x && list[k].y==y){ changed=true; break; } if(!changed && fromBuf[x][y]) display.drawPixel(x,y,col); }
    display.showBuffer();
    idx += perFrame; if(idx>count) idx = count;
    delay(70);
  }
  display.fillScreen(0);
  for(int x=0;x<WIDTH;x++) for(int y=0;y<HEIGHT;y++) if(toBuf[x][y]) display.drawPixel(x,y,col);
  display.showBuffer();
}

void setup(){
  Serial.begin(115200);
  delay(100);
  display.begin(16);
  prefs.begin("morphcfg", false);
  brightnessPct = prefs.getInt("bright", brightnessPct);
  morphingEnabled = prefs.getBool("morph", morphingEnabled);
  colorMode = prefs.getInt("color", colorMode);
  display.setBrightness(map(brightnessPct,0,100,0,255));

  if(!rtc.begin()) Serial.println("RTC not found");
  if(rtc.lostPower()) { Serial.println("RTC lost power - set time via serial or web"); }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP started. IP="); Serial.println(ip);

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.onNotFound(notFound);
  server.begin();

  for(int x=0;x<WIDTH;x++) for(int y=0;y<HEIGHT;y++) bufA[x][y]=bufB[x][y]=false;
  buildBitmapForString("--:--", bufA);
  display.fillScreen(0);
}

void loop(){
  server.handleClient();
  unsigned long nowMillis = millis();
  if(nowMillis - lastStateChange > 10000){
    lastStateChange = nowMillis;
    showState = (showState + 1) % 2;
    DateTime now = rtc.now();
    String s;
    if(showState==0){ char tmp[6]; sprintf(tmp, "%02d:%02d", now.hour(), now.minute()); s = String(tmp); }
    else { char tmp[12]; sprintf(tmp, "%02d-%02d-%04d", now.day(), now.month(), now.year()); s = String(tmp); }
    buildBitmapForString(s, bufB);
    if(morphingEnabled) morphBuffers(bufA, bufB, colorFromMode(colorMode));
    for(int x=0;x<WIDTH;x++) for(int y=0;y<HEIGHT;y++) bufA[x][y]=bufB[x][y];
  }

  display.fillScreen(0);
  uint16_t col = colorFromMode(colorMode);
  for(int x=0;x<WIDTH;x++) for(int y=0;y<HEIGHT;y++) if(bufA[x][y]) display.drawPixel(x,y,col);
  display.showBuffer();
  delay(50);
}

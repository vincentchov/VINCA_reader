#include <Arduino.h>
#include "web_assets.h"
#include <WebSocketsServer.h>
#include "ESP_IOT.h"

#ifdef ARDUINO_ARCH_ESP32
  #include "USB.h"
  #include "USBHIDKeyboard.h"
  USBHIDKeyboard Keyboard;
  #define kbd_begin() Keyboard.begin()
  #define kbd_print(x) Keyboard.print(x)

  #define BUTTON 35
  #define SCLCK 7
  #define MISO 9
#else
  #define kbd_begin()
  #define kbd_print(x)
  #define BUTTON 5
  #define SCLCK 14
  #define MISO 12
#endif

#define LED 15

#define MILLIS_BETWEEN_PACKETS 100
int last_millis;
u32_t packet;
bool data_ready;
u32_t data_ready_packet;
u32_t last_data_ready_packet;
char last_value[16];

IRAM_ATTR void clock_isr() {
  // This interrupt is triggered when the clock line goes low.
  // packet vs data_ready_packet vs last_data_ready_packet?
  // Invert the newly-read bit on the MISO line.
  int new_data = digitalRead(MISO) ? 0 : 1;
  unsigned long new_millis = millis();
  if (new_millis - last_millis > MILLIS_BETWEEN_PACKETS)
  {
    // Does this mean we simply assume that we have a new packet when the time
    // between packets is greater than the threshold?

    // What prevents us from reading part of one packet and part of another,
    // and then treating them as one packet? That would probably lead to
    // invalid values.
    data_ready_packet = packet;
    packet = new_data;
    data_ready = true;
  }
  else
  {
    // Shift the data to the left by one and then bitwise OR with new data.
    // This adds a newly-read bit to the packet.
    packet = packet << 1 | new_data;
  }
  
  last_millis  = new_millis;
}

WebSocketsServer webSocket = WebSocketsServer(81);

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

    if (type == WStype_TEXT)
      Serial.printf("%s", payload);
}
 
void handle_index() {
  IOT.server.sendHeader("Content-Encoding", "gzip");
  IOT.server.send_P(200, "text/html", index_html, sizeof(index_html));
}
void handle_favicon() {
  IOT.server.sendHeader("Content-Encoding", "gzip");
  IOT.server.send_P(200, "image/x-icon", favicon_png, sizeof(favicon_png));
}
void handle_reconnecting_websocket() {
  IOT.server.sendHeader("Content-Encoding", "gzip");
  IOT.server.send_P(200, "text/javascript	", reconnecting_websocket_js, sizeof(reconnecting_websocket_js));
}
void handle_csvpng() {
  IOT.server.sendHeader("Content-Encoding", "gzip");
  IOT.server.send_P(200, "image/x-icon", csv_png, sizeof(csv_png));
}
void handle_lcd() {
  IOT.server.sendHeader("Content-Encoding", "gzip");
  IOT.server.send_P(200, "font/woff", LCD_woff, sizeof(LCD_woff));
}
void handle_jexcel_css() {
  IOT.server.sendHeader("Content-Encoding", "gzip");
  IOT.server.send_P(200, "text/css", jexcel_css, sizeof(jexcel_css));
}
void handle_jexcel_js() {
  IOT.server.sendHeader("Content-Encoding", "gzip");
  IOT.server.send_P(200, "text/javascript", jexcel_js, sizeof(jexcel_js));
}
void handle_jsuites_js() {
  IOT.server.sendHeader("Content-Encoding", "gzip");
  IOT.server.send_P(200, "text/javascript", jsuites_js, sizeof(jsuites_js));
}
void handle_jexcel_themes_css() {
  IOT.server.sendHeader("Content-Encoding", "gzip");
  IOT.server.send_P(200, "text/css", jexcel_themes_css, sizeof(jexcel_themes_css));
}
void handle_jsuites_css() {
  IOT.server.sendHeader("Content-Encoding", "gzip");
  IOT.server.send_P(200, "text/css", jsuites_css, sizeof(jsuites_css));
}
void setup() {
  packet = 0;
  last_millis = millis();
  data_ready = false;
  Serial.begin(115200);
  kbd_begin();
  pinMode(BUTTON, INPUT);
  pinMode(SCLCK, INPUT);
  pinMode(MISO, INPUT);
  pinMode(LED, OUTPUT);

  attachInterrupt(digitalPinToInterrupt(SCLCK), clock_isr, FALLING);

  String deviceName = "VINCA_reader";  // Soft AP and device name for OTA.
  IOT.initIOT(deviceName, "password", "ThisPa55word!", deviceName);

  IOT.server.on("/", handle_index);
  IOT.server.on("/favicon.png", handle_favicon);
  IOT.server.on("/csv.png", handle_csvpng);
  IOT.server.on("/LCD.woff", handle_lcd);
  IOT.server.on("/reconnecting_websocket.js", handle_reconnecting_websocket);
  IOT.server.on("/jexcel.css", handle_jexcel_css);
  IOT.server.on("/jexcel.js", handle_jexcel_js);
  IOT.server.on("/jexcel.themes.css", handle_jexcel_themes_css);
  IOT.server.on("/jsuites.css", handle_jsuites_css);
  IOT.server.on("/jsuites.js", handle_jsuites_js);
  
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

u32_t reverse(u32_t p)
{
    // Takes a 32-bit integer and reverses the bits.
    // r |= b is equivalent to r = r | b.
    // | is the bitwise OR operator.
    u32_t r=0;
    for(int i=0;i<24;i++) 
    {
      // (p >> i) means shift to the right by i bits.
      // & 1 means bitwise AND with 1.
      // << (23-i) means shift to the left by (23-i) bits.
      r |= ((p>>i) & 1)<<(23-i);
    }
    return r;
}

void decode_vinca_bitstream(u32_t p)
{
    // The last 4 bits are the sign and the inch/mm flag.
    // If the last bit is 1, it's in inches, ie xxx1 or xxx0.
    bool inch = p & 0x1;
    // If the 4th bit is 1, it's negative, ie. 1000 or 1001.
    float sign = p & 0x8 ? -1.0 : 1.0;
    // Why reverse the bits? Is it because we're reading one bit at a time,
    // starting from the least significant bit?

    // Bitwise AND with 111111111111111111110000, which should clear the last
    // 4 out of 24 bits.  Since this is a 32-bit integer, what happens with
    // the remaining 8 bits?
    p &= 0xfffff0;
    // Reverse the bits.
    p = reverse(p);

    float value = inch ? p * 0.0005 : p * 0.01;
    value *= sign;
    // Write formatted string to last_value with 4 decimal places.
    sprintf(last_value, "%.4f%s\n", value, inch ? "\"" : "mm");
}

void loop() 
{
  IOT.handle();
  webSocket.loop();

  if (data_ready && last_data_ready_packet != data_ready_packet)
  {
    // If the interrupt has finished building a packet, and the packet is
    // different from the last packet, then decode the packet.
    last_data_ready_packet = data_ready_packet;
    data_ready = false;
    decode_vinca_bitstream(last_data_ready_packet);
    
    Serial.printf(last_value);
    webSocket.broadcastTXT(last_value);
  }
  static unsigned long last_button_milis = 0;
  // Check button and debounce.
  if (digitalRead(BUTTON) == 0 && millis() > last_button_milis + 500)
  {
    kbd_print(last_value);
    last_button_milis = millis();
    String msg = "*";
    msg += last_value;
    webSocket.broadcastTXT(msg);
  }
  
}

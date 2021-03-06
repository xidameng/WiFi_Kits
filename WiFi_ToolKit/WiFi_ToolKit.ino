/*******************************************************************************
   Ameba WiFi WiFi Kits
   For RTL872x only.

   Add realtek ameba core support to Arduino IDE:
   https://github.com/ambiot/ambd_arduino

   default pins for RTL8722: TFT_CS: 18, TFT_DC: 17, TFT_RST: 22, TFT_BL: 23
   default pins for RTL8722_mini :TFT_CS: 12, TFT_DC: 14,TFT_RST: 15, TFT_BL: 13
   default pins for RTL8720_BW16: TFT_CS: 9, TFT_DC: 6, TFT_RST: 2, TFT_BL: 3


   Project Progress:
   1. MENU framework [V]
   2. WiFi Scanner   [V]
   3. WiFi Analyzer  [V]
   
   TODO: Add MQTT Client
   TODO: Add WiFi Promisc Person Detection
   TODO: Replace AmebaILI9341 library APIs with GFX library APIs
   TODO: Improve UI Design
 ******************************************************************************/



#include "SPI.h"
#include "AmebaILI9341.h"
#define TFT_RESET       2
#define TFT_DC          6
#define TFT_CS          9
#define BUT_MOV         3
#define BUT_SEL         8

#define NUM_ITEM        2
#define ITEM_NAME_LEN   25

#include <WiFi.h>

AmebaILI9341 tft = AmebaILI9341(TFT_CS, TFT_DC, TFT_RESET);

#define ILI9341_SPI_FREQUENCY 20000000

char menuList[NUM_ITEM][ITEM_NAME_LEN] = { "WiFi Scanner", "WiFi Analyzer" };

typedef struct {
  int currentPtr;
  char *itemName;
  bool confirmed;
} menuControl;

// init menu obj values
menuControl wifiMenu = {
  .currentPtr = 0,
  .itemName = menuList[0],
  .confirmed = false,
};

int cursorFlag = 0;
int height = 0;
int width = 0;

#define SCAN_INTERVAL 3000


#include <lwip_netconf.h>
#include <wifi_conf.h>
#include <wifi_constants.h>
#include <wifi_structures.h>
#include <wl_definitions.h>
#include <wl_types.h>

#include <WiFi.h>


#include <Arduino_GFX_Library.h>
Arduino_DataBus *bus = create_default_Arduino_DataBus();
/* More display class: https://github.com/moononournation/Arduino_GFX/wiki/Display-Class */
Arduino_GFX *gfx = new Arduino_ILI9341(bus, TFT_RST, 1 /* rotation */, false /* IPS */);

static int16_t w, h, text_size, banner_height, graph24_baseline, graph50_baseline, graph_baseline, graph_height, channel24_width, channel50_width, channel_width, signal_width;

// RSSI RANGE
#define RSSI_CEILING -40
#define RSSI_FLOOR -100

// Channel legend mapping
static uint16_t channel_legend[] = {
  1, 2, 3, 4, 5, 6, 7,      //  1,  2,  3,  4,  5,  6,  7,
  8, 9, 10, 11, 12, 13, 14, //  8,  9, 10, 11, 12, 13, 14,
  32, 0, 0, 0, 40, 0, 0,    // 32, 34, 36, 38, 40, 42, 44,
  0, 48, 0, 0, 0, 56, 0,    // 46, 48, 50, 52, 54, 56, 58,
  0, 0, 64, 0, 0, 0,        // 60, 62, 64, 68,N/A, 96,
  100, 0, 0, 0, 108, 0, 0,  //100,102,104,106,108,110,112,
  0, 116, 0, 0, 0, 124, 0,  //114,116,118,120,122,124,126,
  0, 0, 132, 0, 0, 0, 140,  //128,N/A,132,134,136,138,140,
  0, 0, 0, 149, 0, 0, 0,    //142,144,N/A,149,151,153,155,
  157, 0, 0, 0, 165, 0, 0,  //157,159,161,163,165,167,169,
  0, 173
};                  //171,173

// Channel color mapping
static uint16_t channel_color[] = {
  RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, MAGENTA,
  RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, MAGENTA,
  RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, MAGENTA,
  RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, MAGENTA,
  RED, ORANGE, YELLOW, GREEN, WHITE, MAGENTA,
  RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, MAGENTA,
  RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, MAGENTA,
  RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, MAGENTA,
  RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, MAGENTA,
  RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, MAGENTA,
  RED, ORANGE
};

static uint16_t channelIdx(int channel)
{
  if (channel <= 14) // 2.4 GHz, channel 1-14
  {
    return channel - 1;
  }
  if (channel <= 64) // 5 GHz, channel 32 - 64
  {
    return 14 + ((channel - 32) / 2);
  }
  if (channel == 68)
  {
    return 31;
  }
  if (channel == 96)
  {
    return 33;
  }
  if (channel <= 144) // channel 98 - 144
  {
    return 34 + ((channel - 100) / 2);
  }
  // channel 149 - 177
  return 58 + ((channel - 149) / 2);
}

static uint8_t _networkCount;
static char _networkSsid[WL_NETWORKS_LIST_MAXNUM][WL_SSID_MAX_LENGTH];
static int32_t _networkRssi[WL_NETWORKS_LIST_MAXNUM];
static uint32_t _networkEncr[WL_NETWORKS_LIST_MAXNUM];
static uint8_t _networkChannel[WL_NETWORKS_LIST_MAXNUM];
static char _networkMac[WL_NETWORKS_LIST_MAXNUM][18];

static rtw_result_t wifidrv_scan_result_handler(rtw_scan_handler_result_t *malloced_scan_result)
{
  rtw_scan_result_t *record;

  if (malloced_scan_result->scan_complete != RTW_TRUE)
  {
    record = &malloced_scan_result->ap_details;
    record->SSID.val[record->SSID.len] = 0; /* Ensure the SSID is null terminated */

    if (_networkCount < WL_NETWORKS_LIST_MAXNUM)
    {
      strcpy(_networkSsid[_networkCount], (char *)record->SSID.val);
      _networkRssi[_networkCount] = record->signal_strength;
      _networkEncr[_networkCount] = record->security;
      _networkChannel[_networkCount] = record->channel;
      sprintf(_networkMac[_networkCount], "%02X:%02X:%02X:%02X:%02X:%02X",
              record->BSSID.octet[0], record->BSSID.octet[1], record->BSSID.octet[2],
              record->BSSID.octet[3], record->BSSID.octet[4], record->BSSID.octet[5]);

      _networkCount++;
    }
  }

  return RTW_SUCCESS;
}

static int8_t scanNetworks()
{
  uint8_t attempts = 10;

  _networkCount = 0;
  if (wifi_scan_networks(wifidrv_scan_result_handler, NULL) != RTW_SUCCESS)
  {
    return WL_FAILURE;
  }

  do
  {
    delay(SCAN_INTERVAL);
  } while ((_networkCount == 0) && (--attempts > 0));
  return _networkCount;
}

// Button Hnadler
void select_handler(uint32_t id, uint32_t event) {
  wifiMenu.confirmed = true;
}

void move_handler(uint32_t id, uint32_t event) {
  wifiMenu.currentPtr ++;
  cursorFlag = 1;
  if (wifiMenu.currentPtr >= NUM_ITEM) {
    wifiMenu.currentPtr = 0;
  }
}


// Menu functions
void selectMenu() {
  if(cursorFlag){
    //tft.clr();
    tft.setCursor(0, 0);
    tft.setForeground(ILI9341_YELLOW);
    tft.setBackground(ILI9341_NAVY);
    tft.setFontSize(3);
    tft.setRotation(1);
    tft.println(" Ameba WiFi kits ");
    Serial.println("Printing items");
    tft.setForeground(ILI9341_CYAN);
    tft.setFontSize(2);
    tft.println();

    for ( int i = 0 ;  i < (NUM_ITEM); i++){
      if (i == wifiMenu.currentPtr){
        // highlighting
        tft.setBackground(ILI9341_DARKGREY);
      } else {
        tft.setBackground(ILI9341_BLACK);
      }
      Serial.print("Printing No.");
      Serial.println(i+1);
      tft.print(" ");
      tft.print(i+1);
      tft.print(".");
      tft.println(menuList[i]);  
      tft.setBackground(ILI9341_BLACK);
    }
  }
  cursorFlag = 0;
}

void drawMenu() {
  Serial.println("Drawing Menu Homepage");

  // draw a static start-up menu page
  tft.setCursor(0, 0);
  tft.setForeground(ILI9341_YELLOW);
  tft.setBackground(ILI9341_NAVY);
  tft.setFontSize(3);
  tft.setRotation(1);
  tft.println(" Ameba WiFi kits ");

  // highlighting
  tft.setBackground(ILI9341_DARKGREY);

  // set item font color
  tft.setForeground(ILI9341_CYAN);
  tft.setFontSize(2);
  tft.println();
  tft.print(" 1.");
  tft.println(menuList[wifiMenu.currentPtr]);
  tft.setBackground(ILI9341_BLACK);
  
  //print the rest of the menu, starting from second item -- "1"
  for ( int i = 1 ;  i < (NUM_ITEM); i++){
    tft.print(" ");
    tft.print(i+1);
    tft.print(".");
    tft.println(menuList[i]);  
  }
}



void setup()
{
  Serial.begin(115200);

  // register buttons interrupt
  pinMode(BUT_SEL, INPUT_IRQ_RISE);
  pinMode(BUT_MOV, INPUT_IRQ_RISE);
  digitalSetIrqHandler(BUT_SEL, select_handler);
  digitalSetIrqHandler(BUT_MOV, move_handler);

  //Init SPI LCD display
  SPI.setDefaultFrequency(ILI9341_SPI_FREQUENCY);
  tft.begin();
  tft.clr();

  // determine the exact screen size
  Serial.print("Height: ");
  Serial.println(tft.getHeight());
  Serial.print("Width: ");
  Serial.println(tft.getWidth());

  // 1. Draw Menu HomePage
  drawMenu();

  // 2. Init WiFi driver
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // don't continue:
    while (true);
  }
  //  LwIP_Init();
  //  wifi_on(RTW_MODE_STA);

  // 3. Display Dynamic Menu Selection
  do{
    selectMenu();
    //Serial.println("Selecting menu item ...");
  } while (!wifiMenu.confirmed);

}


void loop()
{
  Serial.println("Enter loop");
  //drawMenu();
  //if (wifiMenu.confirmed == true) {
    switch (wifiMenu.currentPtr) {
      case 0:
        drawScanner();
        wifiMenu.confirmed = false;
        delay(3000);
        break;
      case 1:
        drawAnalyzer();
        wifiMenu.confirmed = false;
        delay(3000);
        break;
    }
  //}
}

void drawScanner() {
  Serial.println("Drawing Scanner Page");

  // only do once
  if (wifiMenu.confirmed == true) {
    tft.clr();
    tft.setCursor(0, 0);
    tft.setForeground(ILI9341_WHITE);
    tft.setBackground(ILI9341_PURPLE);
    tft.setFontSize(2);
    tft.setRotation(1);
    tft.println("Ameba WiFi Scanner        ");
    tft.setBackground(ILI9341_BLACK);
  
    // Print WiFi MAC address:
    printMacAddress();
  }

  // offset banner height
  int offset = 40; 
  
  tft.fillRectangle(0, offset, width, height - offset, ILI9341_BLACK);
  tft.setCursor(0, offset);

  // scan for existing networks:
  Serial.println("Scanning available networks...");
  listNetworks();

}

void drawAnalyzer() {
  Serial.println("Drawing Analyzer Page");

  // only do once
  if (wifiMenu.confirmed == true) {
    //SPI.end();
    // init LCD
    gfx->begin();
    w = gfx->width();
    h = gfx->height();
    text_size = (h < 200) ? 1 : 2;
    banner_height = (text_size * 8) + 4;
    graph_height = ((gfx->height() - banner_height) / 2) - 30;
    graph24_baseline = banner_height + graph_height + 10;
    graph50_baseline = graph24_baseline + graph_height + 30;
    channel24_width = w / 17;
    channel50_width = w / 62;
  
    // direct draw banner to output display
    gfx->setTextSize(text_size);
    gfx->fillScreen(BLACK);
    gfx->setTextColor(GREEN);
    gfx->setCursor(2, 2);
    gfx->print("RTL");
    gfx->setTextColor(WHITE);
    gfx->print(" WiFi Analyzer");
  }
  

  uint8_t ap_count_list[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  int32_t peak_list[] = {RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR, RSSI_FLOOR};
  int16_t peak_id_list[] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  int32_t channel;
  uint16_t idx;
  int32_t rssi;
  String ssid;
  uint16_t color;
  int16_t height, offset, text_width;

  int n = scanNetworks();

  // clear old graph
  gfx->fillRect(0, banner_height, w, h - banner_height, BLACK);
  gfx->setTextSize(1);

  if (n == 0)
  {
    gfx->setTextColor(WHITE);
    gfx->setCursor(0, banner_height);
    gfx->println("No networks found");
  }
  else
  {
    for (int i = 0; i < n; i++)
    {
      channel = _networkChannel[i];
      idx = channelIdx(channel);
      rssi = _networkRssi[i];

      // channel peak stat
      if (peak_list[idx] < rssi)
      {
        peak_list[idx] = rssi;
        peak_id_list[idx] = i;
      }

      ap_count_list[idx]++;
    }

    // plot found WiFi info
    for (int i = 0; i < n; i++)
    {
      channel = _networkChannel[i];
      idx = channelIdx(channel);
      rssi = _networkRssi[i];
      color = channel_color[idx];
      height = constrain(map(rssi, RSSI_FLOOR, RSSI_CEILING, 1, graph_height), 1, graph_height);
      if (idx < 14)
      {
        graph_baseline = graph24_baseline;
        channel_width = channel24_width;
        signal_width = channel24_width * 2;
        offset = (idx + 2) * channel24_width;
      }
      else
      {
        graph_baseline = graph50_baseline;
        channel_width = channel50_width;
        signal_width = channel50_width * 2;
        offset = (idx - 14 + 2) * channel50_width;
      }

      // trim rssi with RSSI_FLOOR
      if (rssi < RSSI_FLOOR)
      {
        rssi = RSSI_FLOOR;
      }

      // plot chart
      gfx->startWrite();
      gfx->drawEllipseHelper(offset, graph_baseline + 1, signal_width, height, 0b0011, color);
      gfx->endWrite();

      if (i == peak_id_list[idx])
      {
        // Print SSID, signal strengh and if not encrypted
        String ssid = _networkSsid[i];
        if (ssid.length() == 0)
        {
          ssid = _networkMac[i];
        }
        text_width = (ssid.length() + 6) * 6;
        if (text_width > w)
        {
          offset = 0;
        }
        else
        {
          if ((offset + text_width) > w)
          {
            offset = w - text_width;
          }
        }
        gfx->setTextColor(color);
        gfx->setCursor(offset, graph_baseline - 10 - height);
        gfx->print(ssid);
        gfx->print('(');
        gfx->print(rssi);
        gfx->print(')');
        if (_networkEncr[i] == RTW_SECURITY_OPEN)
        {
          gfx->print('*');
        }
      }
    }
  }

  // print WiFi found
  gfx->setTextColor(WHITE);
  gfx->setCursor(2, banner_height);
  gfx->print(n);
  gfx->print(" networks");

  // draw 2.4 GHz graph base axle
  gfx->drawFastHLine(0, graph24_baseline, 320, WHITE);
  for (idx = 0; idx < 14; idx++)
  {
    channel = channel_legend[idx];
    offset = (idx + 2) * channel24_width;
    if (channel > 0)
    {
      gfx->setTextColor(channel_color[idx]);
      gfx->setCursor(offset - ((channel < 10) ? 3 : 6), graph24_baseline + 2);
      gfx->print(channel);
    }
    if (ap_count_list[idx] > 0)
    {
      gfx->setTextColor(LIGHTGREY);
      gfx->setCursor(offset - ((ap_count_list[idx] < 10) ? 3 : 6), graph24_baseline + 8 + 2);
      gfx->print(ap_count_list[idx]);
    }
  }

  // draw 5 GHz graph base axle
  gfx->drawFastHLine(0, graph50_baseline, 320, WHITE);
  for (idx = 14; idx < 71; idx++)
  {
    channel = channel_legend[idx];
    offset = (idx - 14 + 2) * channel50_width;
    if (channel > 0)
    {
      gfx->setTextColor(channel_color[idx]);
      gfx->setCursor(offset - ((channel < 100) ? 6 : 9), graph50_baseline + 2);
      gfx->print(channel);
    }
    if (ap_count_list[idx] > 0)
    {
      gfx->setTextColor(DARKGREY);
      gfx->setCursor(offset - ((ap_count_list[idx] < 10) ? 3 : 6), graph50_baseline + 8 + 2);
      gfx->print(ap_count_list[idx]);
    }
  }
}



void printMacAddress() {
  // the MAC address of your Wifi shield
  byte mac[6];

  tft.setForeground(ILI9341_YELLOW);
  tft.setFontSize(1);
  tft.println("MAC Address:");
  // print your MAC address:
  WiFi.macAddress(mac);
  Serial.print("MAC: ");
  Serial.print(mac[0], HEX);
  tft.print(mac[0], HEX);
  tft.print(":");
  Serial.print(":");
  Serial.print(mac[1], HEX);
  tft.print(mac[1], HEX);
  tft.print(":");
  Serial.print(":");
  Serial.print(mac[2], HEX);
  tft.print(mac[2], HEX);
  tft.print(":");
  Serial.print(":");
  Serial.print(mac[3], HEX);
  tft.print(mac[3], HEX);
  tft.print(":");
  Serial.print(":");
  Serial.print(mac[4], HEX);
  tft.print(mac[4], HEX);
  tft.print(":");
  Serial.print(":");
  Serial.println(mac[5], HEX);
  tft.println(mac[5], HEX);
  tft.println();
}

void listNetworks() {
  // scan for nearby networks:
  Serial.println("** Scan Networks **");
  int numSsid = WiFi.scanNetworks();
  if (numSsid == -1) {
    Serial.println("Couldn't get a wifi connection");
    while (true);
  }

  tft.setForeground(ILI9341_CYAN);
  // print the list of networks seen:
  Serial.print("number of available networks:");
  Serial.println(numSsid);
  tft.print("Number of WiFi found:");
  tft.println(numSsid);
  tft.println();

  // print the network number and name for each network found:
  for (int thisNet = 0; thisNet < numSsid; thisNet++) {
    Serial.print(thisNet);
    tft.print(thisNet);
    Serial.print(") ");
    tft.print(") ");
    Serial.print(WiFi.SSID(thisNet));
    tft.print(WiFi.SSID(thisNet));
    tft.print(" : ");
    Serial.print("\tSignal: ");
    Serial.print(WiFi.RSSI(thisNet));
    tft.print(WiFi.RSSI(thisNet));
    Serial.print(" dBm");
    tft.print(" dBm");
    Serial.print("\tEncryptionRaw: ");
    tft.print(", ");
    printEncryptionTypeEx(WiFi.encryptionTypeEx(thisNet));
    Serial.print("\tEncryption: ");
    printEncryptionType(WiFi.encryptionType(thisNet));
  }
}

void printEncryptionTypeEx(uint32_t thisType) {
  /*  Arduino wifi api use encryption type to mapping to security type.
     This function demonstrate how to get more richful information of security type.
  */
  switch (thisType) {
    case SECURITY_OPEN:
      Serial.print("Open");
      break;
    case SECURITY_WEP_PSK:
      Serial.print("WEP");
      break;
    case SECURITY_WPA_TKIP_PSK:
      Serial.print("WPA TKIP");
      break;
    case SECURITY_WPA_AES_PSK:
      Serial.print("WPA AES");
      break;
    case SECURITY_WPA2_AES_PSK:
      Serial.print("WPA2 AES");
      break;
    case SECURITY_WPA2_TKIP_PSK:
      Serial.print("WPA2 TKIP");
      break;
    case SECURITY_WPA2_MIXED_PSK:
      Serial.print("WPA2 Mixed");
      break;
    case SECURITY_WPA_WPA2_MIXED:
      Serial.print("WPA/WPA2 AES");
      break;
  }
}

void printEncryptionType(int thisType) {
  // read the encryption type and print out the name:
  switch (thisType) {
    case ENC_TYPE_WEP:
      Serial.println("WEP");
      tft.println("WEP");
      break;
    case ENC_TYPE_TKIP:
      Serial.println("WPA");
      tft.println("WPA");
      break;
    case ENC_TYPE_CCMP:
      Serial.println("WPA2");
      tft.println("WPA2");
      break;
    case ENC_TYPE_NONE:
      Serial.println("None");
      tft.println("None");
      break;
    case ENC_TYPE_AUTO:
      Serial.println("Auto");
      tft.println("Auto");
      break;
  }
}

#define DEBUG
#define LOG SerialUSB

// Libs needed for WiFi
#include <SPI.h>
#include <WiFi101.h>

#define APP_START_ADDRESS 0x12000 // must correspond to the linker script flash_with_ota.ld
//#define RESET_MAGIC_ADDRESS ((volatile uint32_t *)(APP_START_ADDRESS - 4)) // last 4 bytes of OTA bootloader (not writeable?)

// WARNING!!! do not set this to (HMCRAMC0_ADDR + HMCRAMC0_SIZE - 4) !!! otherwise you cant double tap into the original bootloader anymore!
#define RESET_MAGIC_ADDRESS ((volatile uint32_t *)(HMCRAMC0_ADDR + HMCRAMC0_SIZE - 16)) // last 16 bytes of RAM, should survive a reset

//#define RESET_MAGIC_ADDRESS ((volatile uint32_t *)(0x200004u)) // start of ram
#define RESET_MAGIC 67
#define RESET_TIMEOUT 3000 // milliseconds within which a second reset has to occur to stay in OTA mode. this counts additionally to the bootloader timeout of 500ms
#define FLASH_SIZE 0x40000UL
byte mac[6];
int status = WL_IDLE_STATUS;
WiFiServer server(80);          // Server on Port 80

bool validateFlashedApp() {
  /**
   * Test reset vector of application @APP_START_ADDRESS+4
   * Sanity check on the Reset_Handler address.
   * TODO: proper CRC check?
   */
 
  /* Load the Reset Handler address of the application */
  uint32_t app_reset_ptr = *(uint32_t *)(APP_START_ADDRESS + 4);

  #ifdef DEBUG
  LOG.println(app_reset_ptr);
  #endif  

  if (app_reset_ptr < APP_START_ADDRESS || app_reset_ptr > FLASH_SIZE) {
    #ifdef DEBUG
    LOG.println("no valid app!");
    #endif
    return false;
  }
  
  return true;
}

bool checkDoubleTapReset() {
  // check if magic value is set.
  // this is the case, when a reset was performed, before the timeout has run out.

  // FIXME: not working, this adress is always reset to 8192... but why? for the original bootloader it works..
  if (*RESET_MAGIC_ADDRESS == RESET_MAGIC) {
    return true;
  }

  *RESET_MAGIC_ADDRESS = RESET_MAGIC;
  delay(RESET_TIMEOUT);
  *RESET_MAGIC_ADDRESS = 12;
  
  return false;
}

void jumpToApp() {
  /* Load the Reset Handler address of the application */
  uint32_t app_reset_ptr = *(uint32_t *)(APP_START_ADDRESS + 4);

  // FIXME: not working..

  /* Rebase the Stack Pointer */
  __set_MSP(*(uint32_t *)APP_START_ADDRESS);

  /* Rebase the vector table base address */
  SCB->VTOR = ((uint32_t)APP_START_ADDRESS & SCB_VTOR_TBLOFF_Msk);

  /* Jump to application Reset Handler in the application */
  asm("bx %0" ::"r"(app_reset_ptr));
}

void printMacAddress(byte mac[]) {
  for (int i = 5; i >= 0; i--) {
    if (mac[i] < 16) {
      Serial.print("0");
    }
    Serial.print(mac[i], HEX);
    if (i > 0) {
      Serial.print(":");
    }
  }
  Serial.println();
}

void sendPage(WiFiClient client){
  // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
  // and a content-type so the client knows what's coming, then a blank line:
  client.println("HTTP/1.1 200 OK");
  client.println("Access-Control-Allow-Origin:*");
  client.println("Content-type:text/html");
  client.println();
  // the content of the HTTP response follows the header:
  client.println("<h1>Upload your compiled sketch here</h1>");
  client.println("<form method='post' enctype='multipart/form-data'>");
  client.println("<input name='file' type='file'>");
  client.println("<input class='button' type='submit' value='Upload'>");
  client.println("</form>"); 
  // The HTTP response ends with another blank line:
  client.println();
            }

            
void printWiFiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
  // print where to go in a browser:
  Serial.print("To see this page in action, open a browser to http://");
  Serial.println(ip);

}

void setupOTA() {
  // TODO: check for wifi shield
  if (WiFi.status() == WL_NO_SHIELD) {
  Serial.println("WiFi shield not present");
  // don't continue
  while (true);
  }
  // Assign mac address to byte array
  // & push it to a char array 
  WiFi.macAddress(mac);
  String ssid_string = String("senseBox:"+String(mac[1],HEX)+String(mac[0],HEX));
  char ssid[20];
  ssid_string.toCharArray(ssid,20);
  Serial.print("Creating access point named: ");
  Serial.println(ssid);
  // TODO: initialize wifi
  // set SSID based on last 4 bytes of MAC address
  status = WiFi.beginAP(ssid);
  if (status != WL_AP_LISTENING) {
  Serial.println("Creating access point failed");
  // don't continue
  while (true);
    }

  // wait 10 seconds for connection:
  delay(10000);

  // start the web server on port 80
  server.begin();
  
  // TODO: turn on status LED
  printWiFiStatus();
}

void setup() {
  #ifdef DEBUG
  LOG.begin(115200);
  while(!LOG) {;} // dont continue until serial was opened
  #endif
  
  LOG.println(*RESET_MAGIC_ADDRESS);


  if (validateFlashedApp()) {
    if (!checkDoubleTapReset()) {
      jumpToApp();
    }
  }

  setupOTA();
}

void loop() {
    // LOG.println("double tapped!");
    // LOG.println(*RESET_MAGIC_ADDRESS);
    // delay(100);
   // Web Server listens to changes 
  if (status != WiFi.status()) {
      // it has changed update the variable
      status = WiFi.status();
  
      if (status == WL_AP_CONNECTED) {
        byte remoteMac[6];
        // a device has connected to the AP
        Serial.print("Device connected to AP, MAC address: ");
        WiFi.APClientMacAddress(remoteMac);
        printMacAddress(remoteMac);
      } else {
        // a device has disconnected from the AP, and we are back in listening mode
        Serial.println("Device disconnected from AP");
      }
    }
    
    WiFiClient client = server.available();   // listen for incoming clients
  
    if (client) {                             // if you get a client,
      boolean skip = false;
      boolean post = false;
      Serial.println("new client");           // print a message out the serial port
      String currentLine = "";
      int webkitCounter = 0;                     // make a String to hold incoming data from the client
      String file = "";
      while (client.connected()) {                          // loop while the client's connected
        if (client.available()) {             // if there's bytes to read from the client,
          char c = client.read();             // read a byte, then
          Serial.write(c);                    // print it out the serial monitor
          if(webkitCounter == 1){
            file += c;
          }
          if (c == '\n') {                    // if the byte is a newline character
  
            if(currentLine.startsWith("POST")){   // if its a POST look at the whole response with body
              post = true;
              skip = true;
            }
            if(webkitCounter == 1){ // if the parser is within the sended file 
              skip=true;
              }
            
            if(currentLine.startsWith("Content-Type: application/octet-stream")){
              webkitCounter = webkitCounter + 1;
            }
            
            // if the current line is blank, you got two newline characters in a row.
            // that's the end of the client HTTP request, so send a response:
            if ((currentLine.length()==0 && !skip) || webkitCounter == 1 && currentLine.startsWith("----")) {
              // Send response
              if(post){
              Serial.println("The file sent looks like this");
              Serial.println("---------------start----------------");
              Serial.println(file);
              Serial.println("---------------end------------------");
              }
              // give response back to the client
              sendPage(client);
              // break out of the while loop:
              break;
            }
            else if(currentLine.length()== 0 && skip){
                skip = false;
                continue;
                ;}
            else {      // if you got a newline, then clear currentLine:
              currentLine = "";
            }
          }
          else if (c != '\r') {    // if you got anything else but a carriage return character,
            currentLine += c;      // add it to the end of the currentLine
          }
        }
  
        }
      
      // close the connection:
      client.stop();
      Serial.println("client disconnected");
    }
  }
  






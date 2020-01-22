
#include "OctoprintClient.h"

WiFiClient connection;
const String apiKey ("6f1ff46569758f788988069baaa0a909"); // can be found in OctoPrint Settings -> API

IPAddress ip;
// String url = octoprint.my
uint16_t port = 5000;

octoprint::OctoprintClient(apiKey, connection, ip, port);
// OctoprintClient(apiKey, connection, ip, url);

void setup()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin("myWifi", "mySecret");
}

void loop() {
    sleep(1);
}

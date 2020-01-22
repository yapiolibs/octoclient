/* ___       _        ____       _       _      _    ____ ___
  / _ \  ___| |_ ___ |  _ \ _ __(_)_ __ | |_   / \  |  _ \_ _|
 | | | |/ __| __/ _ \| |_) | '__| | '_ \| __| / _ \ | |_) | |
 | |_| | (__| || (_) |  __/| |  | | | | | |_ / ___ \|  __/| |
  \___/ \___|\__\___/|_|   |_|  |_|_| |_|\__/_/   \_\_|  |___|

 Author: https://github.com/rubienr
 based on Stephen Ludgate https://www.chunkymedia.co.uk
*/

#include "Arduino.h"
#include "OctoPrintAPI.h"

namespace octoprint {

OctoprintClient::OctoprintClient(const String &apiKey, Client &client, const IPAddress &hostIp, uint16_t hostPort) :
        client{client},
        apiKey{apiKey},
        hostIp{hostIp},

        hostPort{hostPort} {}


OctoprintClient::OctoprintClient(const String &apiKey, Client &client, const String &hostUrl, uint16_t hostPort) :
        client{client},
        apiKey{apiKey},

        hostUrl{hostUrl},
        hostPort{hostPort} {}

String OctoprintClient::sendRequestToOctoprint(const String &type, const String &command, const String &data) {
    if (isDebugEnabled) Serial.println("OctoprintClient::sendRequestToOctoprint");

    if ((type != "GET") && (type != "POST")) {
        if (isDebugEnabled)
            Serial.println(
                    "OctoprintApi::sendRequestToOctoprint: unrecognized request, " + type + " must GET or POST)");
        return String();
    }

    String statusCode = "";
    String headers = "";
    String body = "";
    bool finishedStatusCode = false;
    bool finishedHeaders = false;
    bool currentLineIsBlank = true;
    uint16_t ch_count = 0;
    int headerCount = 0;
    int headerLineStart = 0;
    int bodySize = -1;
    unsigned long now;

    bool isConnected{false};

    if (hostUrl.isEmpty()) {
        isConnected = client.connect(hostIp, hostPort);
    } else {
        isConnected = client.connect(hostUrl.c_str(), hostPort);
    }

    if (isConnected) {
        if (isDebugEnabled) Serial.println(".... connected to server");

        char useragent[64];
        snprintf(useragent, 64, "User-Agent: %s", USER_AGENT);

        client.println(type + " " + command + " HTTP/1.1");
        client.print("Host: ");
        if (hostUrl.isEmpty()) {
            client.println(hostIp);
        } else {
            client.println(hostUrl);
        }
        client.print("X-Api-Key: ");
        client.println(apiKey);
        client.println(useragent);
        client.println("Connection: keep-alive");
        if (!data.isEmpty()) {
            client.println("Content-Type: application/json");
            client.print("Content-Length: ");
            client.println(data.length());                   // number of bytes in the payload
            client.println();                               // important need an empty line here
            client.println(data);                           // the payload
        } else {
            client.println();
        }

        now = millis();
        while (millis() - now < OPAPI_TIMEOUT) {
            while (client.available()) {
                char c = client.read();

                if (isDebugEnabled) Serial.print(c);

                if (!finishedStatusCode) {
                    if (c == '\n') {
                        finishedStatusCode = true;
                    } else {
                        statusCode = statusCode + c;
                    }
                }

                if (!finishedHeaders) {
                    if (c == '\n') {
                        if (currentLineIsBlank) {
                            finishedHeaders = true;
                        } else {
                            if (headers.substring(headerLineStart).startsWith("Content-Length: ")) {
                                bodySize = (headers.substring(headerLineStart + 16)).toInt();
                            }
                            headers = headers + c;
                            headerCount++;
                            headerLineStart = headerCount;
                        }
                    } else {
                        headers = headers + c;
                        headerCount++;
                    }
                } else {
                    if (ch_count < maxMessageLengthBytes) {
                        body = body + c;
                        ch_count++;
                        if (ch_count == bodySize) {
                            break;
                        }
                    }
                }
                if (c == '\n') {
                    currentLineIsBlank = true;
                } else if (c != '\r') {
                    currentLineIsBlank = false;
                }
            }
            if (ch_count == bodySize) {
                break;
            }
        }
    } else {
        Serial.println(" OctoprintClient::sendRequestToOctoprint: connection failed");
    }

    closeClient();

    int httpCode = extractHttpCode(statusCode, body);
    if (isDebugEnabled) {
        Serial.print("\nhttpCode:");
        Serial.println(httpCode);
    }
    httpStatusCode = httpCode;

    return body;
}


String OctoprintClient::sendGetToOctoprint(String command) {
    if (isDebugEnabled) Serial.println("OctoprintClient::sendGetToOctoprint");
    return sendRequestToOctoprint("GET", command, "");
}


bool OctoprintClient::getOctoprintVersion() {
    /** Retrieve information regarding server and API version.
    * Returns a JSON object with two keys, api (API version), server (server version).
    * Status Codes: 200 OK – No error
    * http://docs.octoprint.org/en/master/api/version.html#version-information
    **/
    const String command = "/api/version";
    const String response = sendGetToOctoprint(command);

    DeserializationError e = deserializeJson(requestBuffer, response);
    if (!e) {
        if (requestBuffer.containsKey("api")) {
            octoprintVersion.api = requestBuffer["api"].as<String>();
            octoprintVersion.server = requestBuffer["server"].as<String>();
            return true;
        }
    }
    return false;
}

bool OctoprintClient::getPrinterStatistics() {
    /**
    * Retrieves the current state of the printer.
    * Returns: 200 OK with a Full State Response in the body upon success.
    * http://docs.octoprint.org/en/master/api/printer.html#retrieve-the-current-printer-state
    **/
    String command = "/api/printer";
    String response = sendGetToOctoprint(command);       //recieve reply from OctoPrint

    DeserializationError e = deserializeJson(requestBuffer, response);
    if (!e) {
        if (requestBuffer.containsKey("state")) {
            fetchPrinterStateFromJson(requestBuffer.as<JsonVariant>());
        }
        if (requestBuffer.containsKey("temperature")) {
            fetchPrinterThermalDataFromJson(requestBuffer.as<JsonVariant>());
        }
        return true;
    } else {
        printerStatus.printerStateText = response;
        if (response == "Printer is not operational") {
            return true;
        }
    }
    return false;
}

/***** PRINT JOB OPPERATIONS *****/

bool OctoprintClient::octoPrintJobStart() {
    /**
    * Job commands allow starting, pausing and cancelling print jobs.
    *
    * Available commands are: start, cancel, restart, pause - Accepts one optional additional parameter action specifying
    * which action to take - pause, resume, toggle
    * If no print job is active (either paused or printing), a 409 Conflict will be returned.
    * Upon success, a status code of 204 No Content and an empty body is returned.
    *
    * http://docs.octoprint.org/en/devel/api/job.html#issue-a-job-command
    **/

    const String command = "/api/job";
    const String postData("{\"command\": \"start\"}");
    String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}

bool OctoprintClient::octoPrintJobCancel() {
    String command = "/api/job";
    String postData = "{\"command\": \"cancel\"}";
    String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}

bool OctoprintClient::octoPrintJobRestart() {
    String command = "/api/job";
    String postData = "{\"command\": \"restart\"}";
    String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}

bool OctoprintClient::octoPrintJobPauseResume() {
    String command = "/api/job";
    String postData = "{\"command\": \"pause\"}";
    String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}

bool OctoprintClient::octoPrintJobPause() {
    const String command = "/api/job";
    const String postData = "{\"command\": \"pause\", \"action\": \"pause\"}";
    const String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}

bool OctoprintClient::octoPrintJobResume() {
    const String command = "/api/job";
    const String postData = "{\"command\": \"pause\", \"action\": \"resume\"}";
    const String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}


bool OctoprintClient::octoPrintFileSelect(String &path) {
    const String command = "/api/files/local" + path;
    const String postData = "{\"command\": \"select\", \"print\": false }";
    const String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}

//bool OctoprintApi::octoPrintJobPause(String actionCommand){}

/** getPrintJob
 * http://docs.octoprint.org/en/master/api/job.html#retrieve-information-about-the-current-job
 * Retrieve information about the current job (if there is one).
 * Returns a 200 OK with a Job information response in the body.
 * */
bool OctoprintClient::getPrintJob() {
    const String command = "/api/job";
    const String response = sendGetToOctoprint(command);

    DeserializationError e = deserializeJson(requestBuffer, response);
    if (!e) {
        String printerState = requestBuffer["state"];
        printJob.printerState = printerState;

        if (requestBuffer.containsKey("job")) {
            long estimatedPrintTime = requestBuffer["job"]["estimatedPrintTime"];
            printJob.estimatedPrintTime = estimatedPrintTime;

            long jobFileDate = requestBuffer["job"]["file"]["date"];
            String jobFileName = requestBuffer["job"]["file"]["name"] | "";
            String jobFileOrigin = requestBuffer["job"]["file"]["origin"] | "";
            long jobFileSize = requestBuffer["job"]["file"]["size"];
            printJob.jobFileDate = jobFileDate;
            printJob.jobFileName = jobFileName;
            printJob.jobFileOrigin = jobFileOrigin;
            printJob.jobFileSize = jobFileSize;

            long jobFilamentTool0Length = requestBuffer["job"]["filament"]["tool0"]["length"] | 0;
            float jobFilamentTool0Volume = requestBuffer["job"]["filament"]["tool0"]["volume"] | 0.0;
            printJob.jobFilamentTool0Length = jobFilamentTool0Length;
            printJob.jobFilamentTool0Volume = jobFilamentTool0Volume;
            long jobFilamentTool1Length = requestBuffer["job"]["filament"]["tool1"]["length"] | 0;
            float jobFilamentTool1Volume = requestBuffer["job"]["filament"]["tool1"]["volume"] | 0.0;
            printJob.jobFilamentTool1Length = jobFilamentTool1Length;
            printJob.jobFilamentTool1Volume = jobFilamentTool1Volume;
        }
        if (requestBuffer.containsKey("progress")) {
            float progressCompletion = requestBuffer["progress"]["completion"] |
                                       0.0;//isnan(root["progress"]["completion"]) ? 0.0 : root["progress"]["completion"];
            long progressFilepos = requestBuffer["progress"]["filepos"];
            long progressPrintTime = requestBuffer["progress"]["printTime"];
            long progressPrintTimeLeft = requestBuffer["progress"]["printTimeLeft"];

            printJob.progressCompletion = progressCompletion;
            printJob.progressFilepos = progressFilepos;
            printJob.progressPrintTime = progressPrintTime;
            printJob.progressPrintTimeLeft = progressPrintTimeLeft;
        }
        return true;
    }
    return false;
}

/** getOctoprintEndpointResults()
 * General function to get any GET endpoint of the API and return body as a string for you to format or view as you wish.
 * */
String OctoprintClient::getOctoprintEndpointResults(String command) {
    if (isDebugEnabled) Serial.println("OctoprintApi::getOctoprintEndpointResults() CALLED");
    return sendGetToOctoprint("/api/" + command);
}


String OctoprintClient::sendPostToOctoPrint(const String &command, const String &postData) {
    if (isDebugEnabled) Serial.println("OctoprintApi::sendPostToOctoPrint() CALLED");
    return sendRequestToOctoprint("POST", command, postData.c_str());
}

/***** CONNECTION HANDLING *****/
/**
 * http://docs.octoprint.org/en/master/api/connection.html#issue-a-connection-command
 * Issue a connection command. Currently available command are: connect, disconnect, fake_ack
 * Status Codes:
 * 204 No Content – No error
 * 400 Bad Request – If the selected port or baudrate for a connect command are not part of the available options.
 * */
bool OctoprintClient::octoPrintConnectionAutoConnect() {
    const String command = "/api/connection";
    const String postData = "{\"command\": \"connect\"}";
    const String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}

bool OctoprintClient::octoPrintConnectionDisconnect() {
    const String command = "/api/connection";
    const String postData = "{\"command\": \"disconnect\"}";
    const String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}

bool OctoprintClient::octoPrintConnectionFakeAck() {
    const String command = "/api/connection";
    const String postData = "{\"command\": \"fake_ack\"}";
    const String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}


/***** PRINT HEAD *****/
/**
 * http://docs.octoprint.org/en/master/api/printer.html#issue-a-print-head-command
 * Print head commands allow jogging and homing the print head in all three axes.
 * Available commands are: jog, home, feedrate
 * All of these commands except feedrate may only be sent if the printer is currently operational and not printing. Otherwise a 409 Conflict is returned.
 * Upon success, a status code of 204 No Content and an empty body is returned.
 * */
bool OctoprintClient::octoPrintPrintHeadHome() {
    const String command = "/api/printer/printhead";
    //   {
    //   "command": "home",
    //   "axes": ["x", "y", "z"]
    // }
    String postData = "{\"command\": \"home\",\"axes\": [\"x\", \"y\"]}";
    String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}


bool OctoprintClient::octoPrintPrintHeadRelativeJog(double x, double y, double z, double f) {
    const String command = "/api/printer/printhead";
    //  {
    // "command": "jog",
    // "x": 10,
    // "y": -5,
    // "z": 0.02,
    // "absolute": false,
    // "speed": 30
    // }
    char postData[1024];
    char tmp[128];
    postData[0] = '\0';

    strcat(postData, "{\"command\": \"jog\"");
    if (x != 0) {
        snprintf(tmp, 128, ", \"x\": %f", x);
        strcat(postData, tmp);
    }
    if (y != 0) {
        snprintf(tmp, 128, ", \"y\": %f", y);
        strcat(postData, tmp);
    }
    if (z != 0) {
        snprintf(tmp, 128, ", \"z\": %f", z);
        strcat(postData, tmp);
    }
    if (f != 0) {
        snprintf(tmp, 128, ", \"speed\": %f", f);
        strcat(postData, tmp);
    }
    strcat(postData, ", \"absolute\": false");
    strcat(postData, " }");
    Serial.println(postData);

    const String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}

bool OctoprintClient::octoPrintExtrude(double amount) {
    const String command = "/api/printer/tool";
    char postData[256];
    snprintf(postData, 256, "{ \"command\": \"extrude\", \"amount\": %f }", amount);

    const String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}

bool OctoprintClient::octoPrintSetBedTemperature(uint16_t t) {
    const String command = "/api/printer/bed";
    char postData[256];
    snprintf(postData, 256, "{ \"command\": \"target\", \"target\": %d }", t);

    const String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}


bool OctoprintClient::octoPrintSetTool0Temperature(uint16_t t) {
    const String command = "/api/printer/tool";
    char postData[256];
    snprintf(postData, 256, "{ \"command\": \"target\", \"targets\": { \"tool0\": %d } }", t);

    String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}

bool OctoprintClient::octoPrintSetTool1Temperature(uint16_t t) {
    const String command = "/api/printer/tool";
    char postData[256];
    snprintf(postData, 256, "{ \"command\": \"target\", \"targets\": { \"tool1\": %d } }", t);

    String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}




/***** PRINT BED *****/
/** octoPrintGetPrinterBed()
 * http://docs.octoprint.org/en/master/api/printer.html#retrieve-the-current-bed-state
 * Retrieves the current temperature data (actual, target and offset) plus optionally a (limited) history (actual, target, timestamp) for the printer’s heated bed.
 * It’s also possible to retrieve the temperature history by supplying the history query parameter set to true.
 * The amount of returned history data points can be limited using the limit query parameter.
 * Returns a 200 OK with a Temperature Response in the body upon success.
 * If no heated bed is configured for the currently selected printer profile, the resource will return an 409 Conflict.
 * */
bool OctoprintClient::octoPrintGetPrinterBed() {
    String command = "/api/printer/bed?history=true&limit=2";
    String response = sendGetToOctoprint(command);

    DeserializationError e = deserializeJson(requestBuffer, response);
    if (!e) {
        if (requestBuffer.containsKey("bed")) {
            printerStatus.temperature.bedCurrentCelsius = requestBuffer["bed"]["actual"].as<float>();
            printerStatus.temperature.bedOffsetCelsius = requestBuffer["bed"]["offset"].as<float>();
            printerStatus.temperature.bedTargetCelsius = requestBuffer["bed"]["target"].as<float>();
        }
        if (requestBuffer.containsKey("history")) {
            const JsonArray &history = requestBuffer["history"];
            printerStatus.temperature.bedHistoryTempTimestamp = history[0]["time"].as<long>();
            printerStatus.temperature.bedHistoryTempCurrentCelsius = history[0]["bed"]["actual"].as<float>();
        }
        return true;
    }
    return false;
}


/***** SD FUNCTIONS *****/
/*
 * http://docs.octoprint.org/en/master/api/printer.html#issue-an-sd-command
 * SD commands allow initialization, refresh and release of the printer’s SD card (if available).
 * Available commands are: init, refresh, release
*/
bool OctoprintClient::octoPrintPrinterSDInit() {
    const String command = "/api/printer/sd";
    const String postData = "{\"command\": \"init\"}";
    String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}

bool OctoprintClient::octoPrintPrinterSDRefresh() {
    const String command = "/api/printer/sd";
    const String postData = "{\"command\": \"refresh\"}";
    String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}

bool OctoprintClient::octoPrintPrinterSDRelease() {
    const String command = "/api/printer/sd";
    const String postData = "{\"command\": \"release\"}";
    String response = sendPostToOctoPrint(command, postData);
    if (httpStatusCode == 204) return true;
    return false;
}

/*
http://docs.octoprint.org/en/master/api/printer.html#retrieve-the-current-sd-state
Retrieves the current state of the printer’s SD card.
If SD support has been disabled in OctoPrint’s settings, a 404 Not Found is returned.
Returns a 200 OK with an SD State Response in the body upon success.
*/
bool OctoprintClient::octoPrintGetPrinterSD() {
    const String command = "/api/printer/sd";
    String response = sendGetToOctoprint(command);

    DeserializationError e = deserializeJson(requestBuffer, response);
    if (!e) {
        if (requestBuffer["ready"].as<bool>())
            printerStatus.addState(PrinterState::OperationalStateFlags::Ready);
        return true;
    }
    return false;
}


/***** COMMANDS *****/
/*
http://docs.octoprint.org/en/master/api/printer.html#send-an-arbitrary-command-to-the-printer
Sends any command to the printer via the serial interface. Should be used with some care as some commands can interfere with or even stop a running print job.
If successful returns a 204 No Content and an empty body.
*/
bool OctoprintClient::octoPrintPrinterCommand(char *gcodeCommand) {
    const String command = "/api/printer/command";
    char postData[50];

    postData[0] = '\0';
    sprintf(postData, "{\"command\": \"%s\"}", gcodeCommand);

    String response = sendPostToOctoPrint(command, postData);

    if (httpStatusCode == 204) return true;
    return false;
}


/***** GENERAL FUNCTIONS *****/

/**
 * Close the client
 * */
void OctoprintClient::closeClient() {
    // if(client.connected()){    //1.1.4 - Seems to crash/halt ESP32 if 502 Bad Gateway server error
    client.stop();
    // }
}

/**
 * Extract the HTTP header response code. Used for error reporting - will print in serial monitor any non 200 response codes (i.e. if something has gone wrong!).
 * Thanks Brian for the start of this function, and the chuckle of watching you realise on a live stream that I didn't use the response code at that time! :)
 * */
int OctoprintClient::extractHttpCode(String statusCode, String body) {
    if (isDebugEnabled) {
        Serial.print("\nStatus code to extract: ");
        Serial.println(statusCode);
    }
    int firstSpace = statusCode.indexOf(" ");
    int lastSpace = statusCode.lastIndexOf(" ");
    if (firstSpace > -1 && lastSpace > -1 && firstSpace != lastSpace) {
        String statusCodeALL = statusCode.substring(firstSpace + 1);                //"400 BAD REQUEST"
        String statusCodeExtract = statusCode.substring(firstSpace + 1,
                                                        lastSpace); //May end up being e.g. "400 BAD"
        int statusCodeInt = statusCodeExtract.toInt();                              //Converts to "400" integer - i.e. strips out rest of text characters "fix"
        if (statusCodeInt != 200
            and statusCodeInt != 201
            and statusCodeInt != 202
            and statusCodeInt != 204) {
            Serial.print("\nSERVER RESPONSE CODE: " + String(statusCodeALL));
            if (body != "") Serial.println(" - " + body);
            else Serial.println();
        }
        return statusCodeInt;
    } else {
        return -1;
    }
}

void OctoprintClient::fetchPrinterStateFromJson(const JsonVariant root) {
    printerStatus.printerStateText = root["state"]["text"].as<String>();

    if (root["state"]["flags"]["closedOrError"].as<bool>())
        printerStatus.setState(PrinterState::OperationalStateFlags::ClosedOrError);

    else if (root["state"]["flags"]["error"].as<bool>())
        printerStatus.setState(PrinterState::OperationalStateFlags::Error);

    else if (root["state"]["flags"]["operational"].as<bool>())
        printerStatus.setState(PrinterState::OperationalStateFlags::Operational);

    else if (root["state"]["flags"]["paused"].as<bool>())
        printerStatus.setState(PrinterState::OperationalStateFlags::Paused);

    else if (root["state"]["flags"]["printing"].as<bool>())
        printerStatus.setState(PrinterState::OperationalStateFlags::Printing);

    else if (root["state"]["flags"]["ready"].as<bool>())
        printerStatus.setState(PrinterState::OperationalStateFlags::Ready);

    else if (root["state"]["flags"]["sdReady"].as<bool>())
        printerStatus.setState(PrinterState::OperationalStateFlags::SdReady);
}

void OctoprintClient::fetchPrinterThermalDataFromJson(const JsonVariant &root) {
    printerStatus.temperature.bedCurrentCelsius = root["temperature"]["bed"]["actual"].as<float>();
    printerStatus.temperature.bedCurrentCelsius = root["temperature"]["bed"]["target"].as<float>();

    printerStatus.temperature.tool0TargetCelsius = root["temperature"]["tool0"]["target"].as<float>();
    printerStatus.temperature.tool0CurrentCelsius = root["temperature"]["tool0"]["actual"].as<float>();

    printerStatus.temperature.tool1TargetCelsius = root["temperature"]["tool1"]["target"].as<float>();
    printerStatus.temperature.tool1CurrentCelsius = root["temperature"]["tool1"]["actual"].as<float>();
}

} // namespace octoprint

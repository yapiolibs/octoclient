/**
 * Author: https://github.com/rubienr
 * based on OcttoPrintAPI Stephen Ludgate https://www.chunkymedia.co.uk
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Client.h>
#include <string>

#define OPAPI_TIMEOUT 3000
#define USER_AGENT "OctoPrintAPI/1.1.4 (Arduino)"

namespace octoprint {
struct PrinterState {

    enum class OperationalStateFlags : uint8_t {
        Undefined = 0,
        ClosedOrError = 2,
        Error = 4,
        Operational = 8,
        Paused = 16,
        Printing = 32,
        Ready = 64,
        SdReady = 128
    };

    using UnderlyingOperationalStateType = std::underlying_type<OperationalStateFlags>::type;

    bool hasState(OperationalStateFlags stateFlag) {
        return (static_cast<UnderlyingOperationalStateType>(stateFlags) |
                static_cast<UnderlyingOperationalStateType> (stateFlag)) != 0;
    }

    bool hasStates(UnderlyingOperationalStateType stateFlags) {
        return (static_cast<UnderlyingOperationalStateType>(this->stateFlags) | stateFlags) != 0;
    }

    void addState(OperationalStateFlags stateFlag) {
        stateFlags = static_cast<OperationalStateFlags>(
                static_cast<UnderlyingOperationalStateType>(stateFlags) |
                static_cast<UnderlyingOperationalStateType>(stateFlag));
    }

    void setState(OperationalStateFlags stateFlag) {
        stateFlags = stateFlag;
    }

    OperationalStateFlags stateFlags = OperationalStateFlags::Undefined;
    String printerStateText;

    struct Thermal {
        float bedCurrentCelsius;
        float bedTargetCelsius;
        float bedOffsetCelsius;

        long bedHistoryTempTimestamp;
        float bedHistoryTempCurrentCelsius;

        float tool0CurrentCelsius;
        float tool0TargetCelsius;

        float tool1CurrentCelsius;
        float tool1TargetCelsius;

    } temperature;
};

struct OctoprintVersion {
    String api;
    String server;
};

namespace internal {
struct JobRequest {

    String printerState;
    long estimatedPrintTime;

    long jobFileDate;
    String jobFileName;
    String jobFileOrigin;
    long jobFileSize;

    float progressCompletion;
    long progressFilepos;
    long progressPrintTime;
    long progressPrintTimeLeft;

    long jobFilamentTool0Length;
    float jobFilamentTool0Volume;
    long jobFilamentTool1Length;
    float jobFilamentTool1Volume;
};

struct BedCallRequest {
    float tempActualCelsius;
    float tempOffsetCelsius;
    float tempTargetCelsius;
    long  tempHistoryTimestamp;
    float tempHistoryActual;
};
} // namespace internal

struct OctoprintClient {

    PrinterState printerStatus;
    OctoprintVersion octoprintVersion;
    octoprint::internal::BedCallRequest bedRequest;
    octoprint::internal::JobRequest printJob;
    int httpStatusCode = 0;
    String httpErrorBody = "";

    OctoprintClient(const String &apiKey, Client &connection, const IPAddress &hostIp, uint16_t hostPort = 5000);

    OctoprintClient(const String &apiKey, Client &connecion, const String &hostUrl, uint16_t hostPort = 5000);

    // xxx
    String sendGetToOctoprint(String command);

    String getOctoprintEndpointResults(String command);

    bool getPrinterStatistics();

    bool getOctoprintVersion();


    bool getPrintJob();

    String sendPostToOctoPrint(const String &command, const String &postData);

    bool octoPrintConnectionDisconnect();

    bool octoPrintConnectionAutoConnect();

    bool octoPrintConnectionFakeAck();

    bool octoPrintPrintHeadHome();

    bool octoPrintPrintHeadRelativeJog(double x, double y, double z, double f);

    bool octoPrintExtrude(double amount);

    bool octoPrintSetBedTemperature(uint16_t t);

    bool octoPrintSetTool0Temperature(uint16_t t);

    bool octoPrintSetTool1Temperature(uint16_t t);

    bool octoPrintGetPrinterSD();

    bool octoPrintPrinterSDInit();

    bool octoPrintPrinterSDRefresh();

    bool octoPrintPrinterSDRelease();

    bool octoPrintGetPrinterBed();

    bool octoPrintJobStart();

    bool octoPrintJobCancel();

    bool octoPrintJobRestart();

    bool octoPrintJobPauseResume();

    bool octoPrintJobPause();

    bool octoPrintJobResume();

    bool octoPrintFileSelect(String &path);

    bool octoPrintPrinterCommand(char *gcodeCommand);

private:
    Client &client;
    const String &apiKey;
    const IPAddress &hostIp{};
    const String &hostUrl {};
    const uint16_t hostPort;

    const uint16_t maxMessageLengthBytes = 1000;
    bool isDebugEnabled{false};
    DynamicJsonDocument requestBuffer{1024};

    void closeClient();

    int extractHttpCode(String statusCode, String body);

    String sendRequestToOctoprint(const String &type, const String &command, const String &data);

    void fetchPrinterStateFromJson(const JsonVariant root);

    void fetchPrinterThermalDataFromJson(const JsonVariant &root);
};

} // namespace octoprint

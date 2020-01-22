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
    long tempHistoryTimestamp;
    float tempHistoryActual;
};
} // namespace internal

struct OverallState {
    PrinterState printerState;
    OctoprintVersion octoprintVersion;
    octoprint::internal::BedCallRequest bedRequest;
    octoprint::internal::JobRequest printJob;
    mutable int httpStatusCode{0};
    String httpErrorBody{""};
};

struct OctoprintClient {

    OctoprintClient(const String &apiKey, Client &connection, const IPAddress &hostIp, uint16_t hostPort = 5000);

    OctoprintClient(const String &apiKey, Client &connecion, const String &hostUrl, uint16_t hostPort = 5000);

    OverallState getCachedState() const;

    /**
     * Send custom command to OctoPrint.
     * Sends a custom command via GET to the endpoint's API.
     * @param command custom api command
     * @return the Json response body
     */
    String sendCustomCommand(String command) const;

    bool fetchOctoprintVersion();

    bool fetchPrinterStatistics();

    bool fetchPrintJob();

    bool sendDisconnect();

    bool sendAutoConnect();

    bool sendFakeAck();

    bool printHeadHome();

    bool printHeadRelativeJog(double x, double y, double z, double f);

    bool printExtrude(double amount);

    bool setTargetBedTemperature(uint16_t celsius);

    bool setTargetTool0Temperature(uint16_t celsius);

    bool setTargetTool1Temperature(uint16_t celsius);

    bool fetchPrinterSdStatus();

    bool printerSdInit();

    bool printerSdRefresh();

    bool printerSdRelease();

    bool fetchPrinterBed();

    bool jobStart();

    bool jobCancel();

    bool jobRestart();

    bool jobPauseResume();

    bool jobPause();

    bool jobResume();

    bool fileSelect(String &path);

    bool printerCommand(char *gcodeCommand);

private:
    OverallState state;

    Client &client;
    const String &apiKey;
    const IPAddress &hostIp{};
    const String &hostUrl{};
    const uint16_t hostPort;

    const uint16_t maxMessageLengthBytes = 1000;
    bool isDebugEnabled{false};
    DynamicJsonDocument requestBuffer{1024};

    void closeClient() const;

    String sendGetToOctoprint(String command) const;

    String sendPostToOctoPrint(const String &command, const String &postData) const;

    String sendRequestToOctoprint(const String &type, const String &command, const String &data) const;

    int extractHttpCode(String statusCode, String body) const;

    void fetchPrinterStateFromJson(const JsonVariant root);

    void fetchPrinterThermalDataFromJson(const JsonVariant &root);
};

} // namespace octoprint

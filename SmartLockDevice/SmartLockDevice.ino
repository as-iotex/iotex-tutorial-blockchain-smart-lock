#include <Arduino.h>

#ifdef ESP32
    #include <WiFi.h>
#endif
#ifdef ESP8266
    #include <ESP8266WiFi.h>
    #include <ESP8266HTTPClient.h>
    #include <WiFiClient.h>
#endif
#ifdef __SAMD21G18A__
    #include <WiFiNINA.h>
#endif

#include <map>
#include "IoTeX-blockchain-client.h"
#include "secrets.h"
#include "abi.h"

// Server details
constexpr const char ip[] = IOTEX_GATEWAY_IP;
constexpr const int port = IOTEX_GATEWAY_PORT;
constexpr const char wifiSsid[] = SECRET_WIFI_SSID;
constexpr const char wifiPass[] = SECRET_WIFI_PASS;

// Create the IoTeX client connection
Connection<Api> connection(ip, port, "");

// Enum that represents the status of the lock
enum LockStatus { LOCK_OPEN, LOCK_CLOSED };

// The address
const char contractAddress[] = SECRET_CONTRACT_ADDRESS_IO;

// The address which performs the action
const char fromAddress[] = IOTEX_ADDRESS_IO;

// The contract object
Contract contract(abiJson);

// The call data
String callData = "";
ParameterValuesDictionary params;

// The execution action
Execution execution;

// Global variable to store previous status for toggling the lock
static bool previousStatus = false;

// The button pin
#if defined(__SAMD21G18A__)
#define BUTTON_PIN 3    // Pin D3
#else
#define BUTTON_PIN 18
#endif

// Toggles the status of the lock in the smart contract
void toggleStatusOnBlockchain()
{
    Serial.println("Toggling lock status");
    // Convert the privte key to a byte array
    const char pK[] = SECRET_PRIVATE_KEY;
    uint8_t pk[IOTEX_PRIVATE_KEY_SIZE];
    signer.str2hex(pK, pk, IOTEX_PRIVATE_KEY_SIZE);

    // Create the account and get the nonce
    Account originAccount(pk);
    AccountMeta accMeta;
    ResultCode result = connection.api.wallets.getAccount(fromAddress, accMeta);
    if (result != ResultCode::SUCCESS)
    {
        Serial.print("Error getting account meta: ");
        Serial.print(IotexHelpers.GetResultString(result));
    }
    int nonce = atoi(accMeta.pendingNonce.c_str());

    // Construct the action - Create the parameters
    ParameterValue paramOpen;
	paramOpen.value.boolean = !previousStatus;
	paramOpen.type = EthereumTypeName::BOOL;
    ParameterValuesDictionary params;
    params.AddParameter("open", paramOpen);

    // Contruct the action - Generate contract call data
    String callData = "";
    contract.generateCallData("setState", params, callData);

    // Send the action and store it's hash for printing it to the console
    uint8_t hash[IOTEX_HASH_SIZE] = {0};
    result = originAccount.sendExecutionAction(connection, nonce, 20000000, "1000000000000", "0", contractAddress, callData, hash);

    // If successful print the action has, otherwise print an error message
    if (result == ResultCode::SUCCESS)
    {
        Serial.print("Hash: ");
        for (int i=0; i<IOTEX_HASH_SIZE; i++)
        {
            char buf[3] = "";
            sprintf(buf, "%02x", hash[i]);
            Serial.print(buf);
        }
        Serial.println();
    }
    else
    {
        Serial.println("Failed to toggle lock status");
    }
}

// Flag that is set on button press
volatile bool buttonPressed = false;
// Interrupt service routine that is triggered when the button is pressed 
#if defined(ESP32)
void IRAM_ATTR isr() {
#else
void isr() {
#endif
    detachInterrupt(BUTTON_PIN);
	buttonPressed = true;
    attachInterrupt(BUTTON_PIN, isr, FALLING);
}

// Sets the pin status of the lock
void SetLockPinStatus(bool open)
{
    digitalWrite(LOCK_PIN, open);
}

// Connects to the Wifi network
void initWiFi() 
{
    #if defined(ESP32)
        WiFi.mode(WIFI_STA);
        #define LED_BUILTIN 2
    #endif
    WiFi.begin(wifiSsid, wifiPass);
    Serial.print(F("Connecting to WiFi .."));
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print('.');
        delay(1000);
    }
    Serial.println(F("Connected. IP: "));
    Serial.println(WiFi.localIP());
}

void setup()
{
    Serial.begin(115200);

    #if defined(__SAMD21G18A__)
    delay(5000);    // Delay for 5000 seconds to allow a serial connection to be established
    #endif

    // Connect to the wifi network
    initWiFi();

    // Create the execution action for calling the "isOpen" function
    contract.generateCallData("isOpen", params, callData);
    execution.data = callData;
    strcpy(execution.contract, contractAddress);

    // Configure the lock pin as an output
    pinMode(LOCK_PIN, OUTPUT);
    digitalWrite(LOCK_PIN, LOW);
    
    // Setup the interrupt on the button
    pinMode(BUTTON_PIN, INPUT_PULLUP);
	attachInterrupt(BUTTON_PIN, isr, FALLING);
}

void loop()
{
    // First check if the button has been pressed and update the lock pin and the smart contract
    if (buttonPressed)
    {
        toggleStatusOnBlockchain();
        SetLockPinStatus(previousStatus);
        buttonPressed = false;
        previousStatus = !previousStatus;
        String statusStr = previousStatus == true ? "OPEN" : "CLOSED";
        Serial.println("Button was pressed. Status changed to: " + statusStr);
        // Delay 7.5 seconds which is the avg confirmation time time, to ensure we don't read stale data on the next read
        delay(7500);
    }
    // If the buton wasn't pressed check if the status has changed in the blockchain
    else
    {
        // Read the contract
        ReadContractResponse response;
        ResultCode result = connection.api.wallets.readContract(execution, fromAddress, 200000, &response);
        if (result != ResultCode::SUCCESS)
        {
            Serial.println("Failed to read contract");
            return;
        }

        // Decode the data into a boolean value where 0 = closed and 1 = open
        bool newStatus = decodeBool(response.data.c_str());
        
        // If we read the contract successfully, update the lock status. Otherwise print an error message
        if (result != ResultCode::SUCCESS)
        {
            Serial.println("Failed to decode data");
        }
        else
        {
            if (newStatus != previousStatus)
            {
                String statusStr = newStatus == true ? "OPEN" : "CLOSED";
                Serial.println("Status read from blockchain has changed to: " + statusStr);
                previousStatus = newStatus;
            }
        }
        // Wait 1 second before polling again
        delay(1000);
    }

    
}
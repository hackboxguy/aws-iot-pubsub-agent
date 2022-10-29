/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/io/TlsOptions.h>
#include <aws/iot/MqttClient.h>
#include <algorithm>
#include <aws/crt/UUID.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <sys/stat.h>
#include <fstream>
//#include <aws/common/CommandLineUtils.h>
#include "CommandLineUtils.h"
#include "LinuxDomainSocketSrv.h"
#include "Publisher.h"
static const char* linuxDomainSockPath = "/tmp/aws-iot-demo-agent-ipc-node";
using namespace Aws::Crt;

//custom extensions to sample program
#define MAX_PAYLOAD_SIZE 4096 //lets limit the message size to 4kb
#define SUBSCRIBER_DATA_FILE "/tmp/subscriber-data-file.txt"
#define INIT_ACCESSORY_FILE_PATH "/usr/sbin/init-accessories.sh"
String InvokeShellCommand(const char* command);
bool IsValidFile(const char* filepath);

int main(int argc, char *argv[])
{

    /************************ Setup the Lib ****************************/
    /*
     * Do the global initialization for the API.
     */
    ApiHandle apiHandle;
    uint32_t messageCount = 10;
    uint32_t intervalSec = 1;
    /*********************** Parse Arguments ***************************/
    Utils::CommandLineUtils cmdUtils = Utils::CommandLineUtils();
    cmdUtils.RegisterProgramName("basic_pub_sub");
    cmdUtils.AddCommonMQTTCommands();
    cmdUtils.RegisterCommand("key", "<path>", "Path to your key in PEM format.");
    cmdUtils.RegisterCommand("cert", "<path>", "Path to your client certificate in PEM format.");
    cmdUtils.AddCommonProxyCommands();
    cmdUtils.AddCommonTopicMessageCommands();
    cmdUtils.RegisterCommand("client_id", "<str>", "Client id to use (optional, default='test-*')");
    cmdUtils.RegisterCommand("count", "<int>", "The number of messages to send (optional, default='10')");
    cmdUtils.RegisterCommand("port_override", "<int>", "The port override to use when connecting (optional)");
    cmdUtils.RegisterCommand("pub_interval", "<int>", "Specify wait time(in seconds) between two publish messages (optional, default=1)");
    cmdUtils.RegisterCommand("subtopic", "<str>", "subscribe to a topic(optional, default=test/topic)");
    cmdUtils.RegisterCommand("subtopic_handler", "<str>", "a handler script to take action when message arrives");

    cmdUtils.AddLoggingCommands();
    const char **const_argv = (const char **)argv;
    cmdUtils.SendArguments(const_argv, const_argv + argc);
    cmdUtils.StartLoggingBasedOnCommand(&apiHandle);

    String topic = cmdUtils.GetCommandOrDefault("topic", "test/topic");
    String clientId = cmdUtils.GetCommandOrDefault("client_id", String("test-") + Aws::Crt::UUID().ToString());
    String subtopic = cmdUtils.GetCommandOrDefault("subtopic", "test/topic");
    String subTopicHandler = cmdUtils.GetCommandOrDefault("subtopic_handler", "");
    String messagePayload = cmdUtils.GetCommandOrDefault("message", "Hello world!");
    if (cmdUtils.HasCommand("count"))
    {
        //TODO: what if count arg has some non-numeral chars? we need to handle this error
        int count = atoi(cmdUtils.GetCommand("count").c_str());
        //if (count > 0)
        {
            messageCount = count;
        }
    }

    if (cmdUtils.HasCommand("pub_interval"))
    {
        int interval = atoi(cmdUtils.GetCommand("pub_interval").c_str());
        if (interval > 0)
        {
            intervalSec = interval;
        }
    }

    /* do some basic tests for availability of CA/Cert/Key files and endpoint */
    String tmpString = cmdUtils.GetCommand("ca_file");
    if(!IsValidFile(tmpString.c_str()))
    {
        fprintf(stdout, "CA_FILE not found!!!\n");
        while(1)
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        exit(-1);
    }
    tmpString = cmdUtils.GetCommand("key");
    if(!IsValidFile(tmpString.c_str()))
    {
        fprintf(stdout, "PRIVATE_KEY_FILE not found!!!\n");
        while(1)
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        exit(-1);
    }
    tmpString = cmdUtils.GetCommand("cert");
    if(!IsValidFile(tmpString.c_str()))
    {
        fprintf(stdout, "DEVICE_CERTIFICATE_FILE not found!!!\n");
        while(1)
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        exit(-1);
    }
    tmpString = cmdUtils.GetCommand("endpoint");
    if(tmpString == "replace.this.with.your.endpoint")
    {
        fprintf(stdout, "looks like endpoint is not correctly specified!!!\n");
        while(1)
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        exit(-1);
    }

    /* Get a MQTT client connection from the command parser */
    auto connection = cmdUtils.BuildMQTTConnection();
    TopicPublisher::Publisher publisher(connection);//this will start a monoshot thread
    //start linux-domain-socket server
    DomainSock::LinuxDomainSocketSrv DomainSocket(linuxDomainSockPath,&publisher);

    /*
     * In a real world application you probably don't want to enforce synchronous behavior
     * but this is a sample console application, so we'll just do that with a condition variable.
     */
    std::promise<bool> connectionCompletedPromise;
    std::promise<void> connectionClosedPromise;

    /*
     * This will execute when an mqtt connect has completed or failed.
     */
    auto onConnectionCompleted = [&](Mqtt::MqttConnection &, int errorCode, Mqtt::ReturnCode returnCode, bool) {
        if (errorCode)
        {
            fprintf(stdout, "Connection failed with error %s\n", ErrorDebugString(errorCode));
            connectionCompletedPromise.set_value(false);
        }
        else
        {
            fprintf(stdout, "Connection completed with return code %d\n", returnCode);
            connectionCompletedPromise.set_value(true);
        }
    };

    auto onInterrupted = [&](Mqtt::MqttConnection &, int error) {
        fprintf(stdout, "Connection interrupted with error %s\n", ErrorDebugString(error));
    };

    auto onResumed = [&](Mqtt::MqttConnection &, Mqtt::ReturnCode, bool) { fprintf(stdout, "Connection resumed\n"); };

    /*
     * Invoked when a disconnect message has completed.
     */
    auto onDisconnect = [&](Mqtt::MqttConnection &) {
        {
            fprintf(stdout, "Disconnect completed\n");
            connectionClosedPromise.set_value();
        }
    };

    connection->OnConnectionCompleted = std::move(onConnectionCompleted);
    connection->OnDisconnect = std::move(onDisconnect);
    connection->OnConnectionInterrupted = std::move(onInterrupted);
    connection->OnConnectionResumed = std::move(onResumed);

    /*
     * Actually perform the connect dance.
     */
    fprintf(stdout, "Connecting...\n");
    if (!connection->Connect(clientId.c_str(), false /*cleanSession*/, 1000 /*keepAliveTimeSecs*/))
    {
        fprintf(stderr, "MQTT Connection failed with error %s\n", ErrorDebugString(connection->LastError()));
        exit(-1);
    }

    if (connectionCompletedPromise.get_future().get())
    {
        std::mutex receiveMutex;
        std::condition_variable receiveSignal;
        uint32_t receivedCount = 0;

        /*
         * This is invoked upon the receipt of a Publish on a subscribed topic.
         */
        auto onMessage = [&](Mqtt::MqttConnection &,
                             const String &topic,
                             const ByteBuf &byteBuf,
                             bool /*dup*/,
                             Mqtt::QOS /*qos*/,
                             bool /*retain*/) {
            {
                std::lock_guard<std::mutex> lock(receiveMutex);
                ++receivedCount;
                //fprintf(stdout, "Publish #%d received on topic %s\n", receivedCount, topic.c_str());
                //fprintf(stdout, "Message: ");
                //fwrite(byteBuf.buffer, 1, byteBuf.len, stdout);
                //fprintf(stdout, "\n");

                //a handler needs to process incoming message
                //check if user has passed a handler binary or script, and let it process the data
                char incomingMsg[MAX_PAYLOAD_SIZE];
                if(byteBuf.len<MAX_PAYLOAD_SIZE)
                {
                    strncpy((char*)incomingMsg,(char*)byteBuf.buffer,byteBuf.len);
                    incomingMsg[byteBuf.len] = '\0';
                    if(IsValidFile(subTopicHandler.c_str())) //check if subscribe topic handler binay exists
                    {
                        //pass the incoming payload to handler via file
                        String dataIn(incomingMsg);
                        std::ofstream subscrData(SUBSCRIBER_DATA_FILE,std::ofstream::out | std::ofstream::trunc);
                        subscrData << dataIn << std::endl;
                        subscrData.close();
                        String invokeCommand = subTopicHandler + " " + SUBSCRIBER_DATA_FILE;
                        //e.g "/usr/sbin/blink-led.sh /tmp/incoming-data.json"
                        InvokeShellCommand(invokeCommand.c_str());
                    }
                    //else
                        //fprintf(stdout, "handler for incoming topic not found\n");
                }
            }

            receiveSignal.notify_all();
        };

        /*
         * Subscribe for incoming publish messages on topic.
         */
        std::promise<void> subscribeFinishedPromise;
        auto onSubAck =
            [&](Mqtt::MqttConnection &, uint16_t packetId, const String &topic, Mqtt::QOS QoS, int errorCode) {
                if (errorCode)
                {
                    fprintf(stderr, "Subscribe failed with error %s\n", aws_error_debug_str(errorCode));
                    exit(-1);
                }
                else
                {
                    if (!packetId || QoS == AWS_MQTT_QOS_FAILURE)
                    {
                        fprintf(stderr, "Subscribe rejected by the broker.");
                        exit(-1);
                    }
                    else
                    {
                        fprintf(stdout, "Subscribe on topic %s on packetId %d Succeeded\n", topic.c_str(), packetId);
                    }
                }
                subscribeFinishedPromise.set_value();
            };

        connection->Subscribe(subtopic.c_str(), AWS_MQTT_QOS_AT_LEAST_ONCE, onMessage, onSubAck);
        subscribeFinishedPromise.get_future().wait();

	if ( IsValidFile(INIT_ACCESSORY_FILE_PATH) )
		InvokeShellCommand(INIT_ACCESSORY_FILE_PATH);

        uint32_t publishedCount = 0;
        String msgPayload;
        while (publishedCount < messageCount)
        {
            if(messagePayload != "") //if empty string, then dont publish anything
            {
                //check if message is a string or path to a shell-script
                if(IsValidFile(messagePayload.c_str())) //its a file in the rootfs
                    msgPayload=InvokeShellCommand(messagePayload.c_str());//e.g /usr/sbin/read-temperature.sh shall print json string
                else //else its just a string
                    msgPayload=messagePayload;

                //ByteBuf payload = ByteBufFromArray((const uint8_t *)msgPayload.data(), msgPayload.length());
                //auto onPublishComplete = [topic](Mqtt::MqttConnection &, uint16_t, int)
                //{
                //    fprintf(stdout, "Publish Complete on topic %s\n",topic.c_str());
                //};
                //connection->Publish(topic.c_str(), AWS_MQTT_QOS_AT_LEAST_ONCE, false, payload, onPublishComplete);

                //serialized the publish requests through publisher thread(external publish request may come from linux-domain-socket)
                std::string tp(topic.c_str());
                std::string pl(msgPayload.c_str());
                publisher.publishTopic(tp,pl);
            }
            if(messageCount>=0)//if count == -1 then run the loop forever till SIGTERM is received
                ++publishedCount;

            std::this_thread::sleep_for(std::chrono::milliseconds(1000 * intervalSec));
        }

        {
            std::unique_lock<std::mutex> receivedLock(receiveMutex);
            receiveSignal.wait(receivedLock, [&] { return receivedCount >= messageCount; });
        }

        /* Just wait here(processing subscribed topics) till SIGTERM is sent to this process */
        fprintf(stdout, "Just Waiting for SIGTERM or CTRL+x\n");
        while(1)
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        /*
         * Unsubscribe from the topic.
         */
        std::promise<void> unsubscribeFinishedPromise;
        connection->Unsubscribe(
            subtopic.c_str(), [&](Mqtt::MqttConnection &, uint16_t, int) { unsubscribeFinishedPromise.set_value(); });
        unsubscribeFinishedPromise.get_future().wait();

        /* Disconnect */
        if (connection->Disconnect())
        {
            connectionClosedPromise.get_future().wait();
        }
    }
    else
    {
        exit(-1);
    }
    return 0;
}
/*****************************************************************************/
//custom extensions to sample program
//from a security perspective this is not a good idea to allow invoking remote commands,
//but as a demo application, we will allow this and we assume user of this binary knows how to use it
String InvokeShellCommand(const char* command)
{
    std::array<char, 128> buffer;
    String result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}
//check if this is a valid file in the filesystem
bool IsValidFile(const char* filepath)
{
        struct stat buffer;
        if(stat(filepath,&buffer)!=0)
                return false;
        if(buffer.st_mode & S_IFREG)
                return true;
        return false;//it could be a directory
}
/*****************************************************************************/

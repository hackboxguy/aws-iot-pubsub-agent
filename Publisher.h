#pragma once
#include "ADThread.h"
#include <string>
#include <deque>
#include <aws/iot/MqttClient.h>
#define SOCK_MAX_PATH 4096
struct PublishEntry
{
        std::string Topic;
        std::string Data;
public:
        PublishEntry(std::string topic,std::string data) :Topic(topic),Data(data){}
};

namespace TopicPublisher
{
    class Publisher : public ADThreadConsumer
    {
        std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> connection;
        std::deque<PublishEntry> PublishList;
        ADThread PublisherThread;//thread for linux-domain-socket-server
        virtual int monoshot_callback_function(void* pUserData,ADThreadProducer* pObj);//{return 0;};//we are not using this one
        virtual int thread_callback_function(void* pUserData,ADThreadProducer* pObj){return 0;};//we are not using this one..
      public:
        Publisher(std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> handle);
        ~Publisher();
        int publishTopic(std::string topic, std::string data);
    };
} // namespace TopicPublisher
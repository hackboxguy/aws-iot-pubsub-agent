//this class acts as a serializer for publishing the data to aws-iot-core.
//publish data is pushed from main.cpp and from LinuxDomainSocketSrv.cpp

#include <aws/crt/Api.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/io/TlsOptions.h>
#include <aws/iot/MqttClient.h>
#include <iostream>
#include "Publisher.h"
using namespace Aws::Crt;

namespace TopicPublisher
{
Publisher::Publisher(std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> handle)
{
        connection=handle;
        //set thread properties
        PublisherThread.subscribe_thread_callback(this);
        PublisherThread.set_thread_properties(THREAD_TYPE_MONOSHOT,(void *)this);
        PublisherThread.start_thread();
}
Publisher::~Publisher()
{
    PublisherThread.stop_thread();
}

int Publisher::monoshot_callback_function(void* pUserData,ADThreadProducer* pObj)
{
    std::cout<<"Publisher::monoshot_callback_function"<<std::endl;
    while (!PublishList.empty()) //this thread goes to sleep if list is empty
    {
        PublishEntry entry = PublishList.front();
        //TODO: publish the data on a given topic
        std::cout<<"topic:"<<entry.Topic<<" data:"<<entry.Data<<std::endl;

        String tp(entry.Topic.c_str());
        String pl(entry.Data.c_str());
        ByteBuf payload = ByteBufFromArray((const uint8_t *)pl.data(), pl.length());
        auto onPublishComplete = [tp](Mqtt::MqttConnection &, uint16_t, int)
        {
            fprintf(stdout, "Publish Complete on topic %s\n",tp.c_str());
        };
        connection->Publish(tp.c_str(), AWS_MQTT_QOS_AT_LEAST_ONCE, false, payload, onPublishComplete);
        PublishList.pop_front();//after processing delete the entry
    }
    return 0;
}
int Publisher::publishTopic(std::string topic, std::string data)
{
    PublishList.push_back(PublishEntry(topic,data));
    PublisherThread.wakeup_thread();
    return 0;
}

} // namespace TopicPublisher
#pragma once
#include "ADThread.h"
#include "Publisher.h"
#include <string>
#define SOCK_MAX_PATH 4096
namespace DomainSock
{
    class LinuxDomainSocketSrv : public ADThreadConsumer
    {
        TopicPublisher::Publisher *pPublisher;
        char socket_path[SOCK_MAX_PATH +1];
        ADThread ServerThread;//thread for linux-domain-socket-server
        virtual int monoshot_callback_function(void* pUserData,ADThreadProducer* pObj){return 0;};//we are not using this one
        virtual int thread_callback_function(void* pUserData,ADThreadProducer* pObj);//{return 0;};//we are not using this one..
        //int ParseJsonData(const char* data);
        int ParseJsonData(const char* data,std::string &resTopic, std::string &resData);
      public:
        LinuxDomainSocketSrv(const char* sockpath,TopicPublisher::Publisher *ptr);
        ~LinuxDomainSocketSrv();
        int RunServer();
    };
} // namespace DomainSock
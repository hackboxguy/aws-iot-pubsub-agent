//server example is taken from: https://gist.github.com/alexandruc/2350954
//This class acts as a server for linux-domain-socket and it is expecting json strings from clients
//e.g :
//{
//    "topic": "test/topic_relay","data": { "position": 1, "powerstate": "on" }
//}
//after receiving json string from linux-domain-socket-client, topic and data is separated using cJSON lib
//and then topic and date will be pushed to a queue in publisher class for publishing
//clients use /tmp/aws-iot-demo-agent-ipc-node as linux-domain-socket-node.

#include "LinuxDomainSocketSrv.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <cjson/cJSON.h>

static const unsigned int nIncomingConnections = 5;

namespace DomainSock
{

LinuxDomainSocketSrv::LinuxDomainSocketSrv(const char* sockpath,TopicPublisher::Publisher *ptr)
{
        pPublisher=ptr;
        strncpy(socket_path,sockpath,SOCK_MAX_PATH);
        //set server thread properties
        ServerThread.subscribe_thread_callback(this);
        ServerThread.set_thread_properties(THREAD_TYPE_NOBLOCK,(void *)this);
        ServerThread.start_thread();
}
LinuxDomainSocketSrv::~LinuxDomainSocketSrv()
{
    ServerThread.stop_thread();
}
int LinuxDomainSocketSrv::RunServer()
{
    //create server side
    int s = 0;
    int s2 = 0;
    struct sockaddr_un local, remote;
    int len = 0;
    s = socket(AF_UNIX, SOCK_STREAM, 0);
    if( -1 == s )
    {
        //printf("Error on socket() call \n");
        return 1;
    }

    local.sun_family = AF_UNIX;
    strcpy( local.sun_path, socket_path );
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);
    if( bind(s, (struct sockaddr*)&local, len) != 0)
    {
        //printf("Error on binding socket \n");
        return 1;
    }
    if( listen(s, nIncomingConnections) != 0 )
    {
        //printf("Error on listen call \n");
        ;
    }

    bool bWaiting = true;
    while (bWaiting)
    {
        unsigned int sock_len = 0;
        //printf("Waiting for connection.... \n");
        if( (s2 = accept(s, (struct sockaddr*)&remote, &sock_len)) == -1 )
        {
            //printf("Error on accept() call \n");
            return 1;
        }
        //printf("Server connected \n");
        int data_recv = 0;
        char recv_buf[100];
        char send_buf[200];
        do{
            memset(recv_buf, 0, 100*sizeof(char));
            memset(send_buf, 0, 200*sizeof(char));
            data_recv = recv(s2, recv_buf, 100, 0);
            if(data_recv > 0)
            {
                //printf("Data received: %d : %s \n", data_recv, recv_buf);
                std::string strTopic,strData;
                if(ParseJsonData(recv_buf,strTopic,strData) ==0)
                {
                    //serialized the publish requests through publisher thread(external publish request may come from linux-domain-socket)
                    pPublisher->publishTopic(strTopic,strData);
                }
                //strcpy(send_buf, "Got message: ");
                //strcat(send_buf, recv_buf);
                if(strstr(recv_buf, "quit")!=0)
                {
                    //printf("Exit command received -> quitting \n");
                    bWaiting = false;
                    break;
                }
                //if( send(s2, send_buf, strlen(send_buf)*sizeof(char), 0) == -1 )
                //    printf("Error on send() call \n");
            }
            //else
            //    printf("Error on recv() call \n");
        }while(data_recv > 0);
        close(s2);
    }
    return 0;
}
int LinuxDomainSocketSrv::thread_callback_function(void* pUserData,ADThreadProducer* pObj)
{
    return RunServer();
}

int LinuxDomainSocketSrv::ParseJsonData(const char* data,std::string &resTopic, std::string &resData)
{
    cJSON *extern_data = cJSON_Parse(data);
    if (extern_data == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            printf("Error before: %s\n", error_ptr);
        }
        return -1;//invalid json data
    }

    //valid json data
    const cJSON *topic = cJSON_GetObjectItemCaseSensitive(extern_data, "topic");
    if (cJSON_IsString(topic) && (topic->valuestring != NULL))
    {
        //printf("topic is: \"%s\"\n", topic->valuestring);
        resTopic = topic->valuestring;
    }

    const cJSON *dataObj = cJSON_GetObjectItemCaseSensitive(extern_data, "data");
    //printf("data is: \"%s\"\n", cJSON_Print(dataObj));
    resData=cJSON_Print(dataObj);
    cJSON_Delete(extern_data);
    return 0;
}
} // namespace DomainSock
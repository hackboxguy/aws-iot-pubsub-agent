#pragma once

namespace DomainSock
{
    class LinuxDomainSocketSrv
    {
      public:
        LinuxDomainSocketSrv();
        int RunServer();
    };
} // namespace DomainSock
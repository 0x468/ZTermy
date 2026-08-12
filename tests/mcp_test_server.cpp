#include <iostream>
#include <string>

int main()
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.find("\"method\":\"initialize\"") != std::string::npos)
        {
            std::cout
                << R"({"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18","capabilities":{"tools":{}},"serverInfo":{"name":"test","version":"1"}}})"
                << std::endl;
        }
        else if (line.find("\"method\":\"tools/list\"") != std::string::npos)
        {
            std::cout
                << R"({"jsonrpc":"2.0","id":2,"result":{"tools":[{"name":"echo","description":"Echo untrusted input","inputSchema":{"type":"object","properties":{"text":{"type":"string"}},"required":["text"]}}]}})"
                << std::endl;
        }
        else if (line.find("\"method\":\"tools/call\"") != std::string::npos)
        {
            std::cout << R"({"jsonrpc":"2.0","id":3,"result":{"content":[{"type":"text","text":"ok"}]}})" << std::endl;
        }
    }
    return 0;
}

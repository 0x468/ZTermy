#include <iostream>
#include <string>

int main() noexcept
{
    try
    {
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (line.find(R"("method":"initialize")") != std::string::npos)
            {
                std::cout
                    << R"({"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18","capabilities":{"tools":{}},"serverInfo":{"name":"test","version":"1"}}})"
                    << '\n'
                    << std::flush;
            }
            else if (line.find(R"("method":"tools/list")") != std::string::npos)
            {
                std::cout
                    << R"({"jsonrpc":"2.0","id":2,"result":{"tools":[{"name":"echo","description":"Echo untrusted input","inputSchema":{"type":"object","properties":{"text":{"type":"string"}},"required":["text"]}},{"name":"slow","description":"Wait until cancelled","inputSchema":{"type":"object"}}]}})"
                    << '\n'
                    << std::flush;
            }
            else if (line.find(R"("method":"tools/call")") != std::string::npos
                     && line.find(R"("name":"slow")") != std::string::npos)
            {
                // The client cancellation path intentionally resolves this request locally.
            }
            else if (line.find(R"("method":"tools/call")") != std::string::npos)
            {
                std::cout << R"({"jsonrpc":"2.0","id":3,"result":{"content":[{"type":"text","text":"ok"}]}})" << '\n'
                          << std::flush;
            }
        }
    }
    catch (...)
    {
        return 1;
    }
    return 0;
}

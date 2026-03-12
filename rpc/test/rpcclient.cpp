#include "rpc/rpcclient.h"
#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include "service.pb.h"
#include <iostream>
auto utils::main_coro() -> MainCoroutine
{
    std::cout << "start" << std::endl;
    RpcClient server("127.0.0.1", 8888);
    rpc::EchoRequest req;
    req.set_data("hello");
    rpc::EchoResponse res;
    if (co_await server.call("echo", req, res))
    {
        std::cout << res.data() << std::endl;
    }
    co_return 0;
}
#include "rpc/rpcserver.h"
#include "coroutine/coroutine.h"
#include "coroutine/main.h"

#include "service.pb.h"
inline ::rpc::EchoResponse echo(::rpc::EchoRequest msg)
{

    ::rpc::EchoResponse res;
    res.set_data(std::move(msg.data()));
    return res;
}
auto utils::main_coro() -> Coroutine<int>
{
    RpcServer server("127.0.0.1", 8888);
    server.register_service("echo", echo);
    co_await server.start();
    co_return 0;
}
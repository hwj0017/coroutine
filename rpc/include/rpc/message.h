#include <google/protobuf/message.h>
namespace utils
{

using Message = ::google::protobuf::Message;
template <typename T>
concept IsMessage = std::is_base_of_v<Message, T>;

} // namespace utils
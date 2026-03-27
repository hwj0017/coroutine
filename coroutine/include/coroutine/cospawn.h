#pragma once
namespace utils
{
class Promise;
void co_spawn(Promise* call, bool yield = false);
} // namespace utils
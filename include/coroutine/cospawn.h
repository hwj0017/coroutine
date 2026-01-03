#pragma once
#include "handle.h"
namespace utils
{

void co_spawn(Handle handle, bool yield = false);
void release();
} // namespace utils
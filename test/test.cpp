void func() {}
template <typename T> void funcA(T value = {}) {}
int main() { return funcA<int>(func()); }
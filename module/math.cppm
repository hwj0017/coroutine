export module math; // 声明模块名为 math

export namespace math
{
// 这是一个简单的加法函数，带有 export 关键字表示对外公开
int add(int a, int b) { return a + b; }
} // namespace math
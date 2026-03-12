package main

import (
	"fmt"
	"sync"
	"time"
)

// --- 实验 1: Ping-Pong 延迟 (1v1 强同步) ---
// 测试单次上下文切换 + Channel 发送/接收的开销
func benchmarkPingPong(n int) {
	chanA := make(chan int) // 无缓冲 channel
	chanB := make(chan int)

	// 启动对端 goroutine
	go func(iters int, a, b chan int) {
		for i := 0; i < iters; i++ {
			<-a
			b <- 1
		}
	}(n, chanA, chanB)

	start := time.Now()

	for i := 0; i < n; i++ {
		chanA <- 1
		res := <-chanB
		_ = res // Go 中忽略变量直接用 _, 编译器通常不会过度优化掉 channel 接收动作，因为它有副作用
	}

	totalNs := time.Since(start).Nanoseconds()
	fmt.Printf("Ping-Pong: %d ns/op (per switch)\n", totalNs/(int64(n)*2))
}

// --- 实验 2: MPMC 吞吐量 (多生产者多消费者) ---
// 测试 Channel 在高竞争下的锁竞争或无锁性能
func benchmarkThroughput(totalMsgs, pCount, cCount int) {
	ch := make(chan int, 1024) // 有缓冲 channel

	cNum := totalMsgs / cCount
	pNum := totalMsgs / pCount

	var wg sync.WaitGroup

	start := time.Now()

	// 启动消费者
	for i := 0; i < cCount; i++ {
		wg.Add(1)
		go func(num int) {
			defer wg.Done()
			for j := 0; j < num; j++ {
				v := <-ch
				_ = v
			}
		}(cNum)
	}

	// 启动生产者
	for i := 0; i < pCount; i++ {
		wg.Add(1)
		go func(num int) {
			defer wg.Done()
			for j := 0; j < num; j++ {
				ch <- 1
			}
		}(pNum)
	}

	// 等待所有生产者和消费者完成
	wg.Wait()

	// 修正了 C++ 版本中的小 Bug：这里应该在 Wait 之后计算时间，否则测出的只是发送任务的时间
	elapsed := time.Since(start).Seconds()

	// 使用 float64 进行计算，防止执行太快 time == 0 导致除以 0 崩溃，且结果更精确
	msgsPerSec := float64(totalMsgs) / elapsed
	fmt.Printf("Processed %.0f msgs/sec\n", msgsPerSec)
}

func main() {
	fmt.Println("--- Starting Benchmarks (Go Goroutines) ---")

	// 1. 测试延迟
	benchmarkPingPong(10000000)

	// 2. 测试吞吐 (16 生产者, 16 消费者)
	// 结合我们之前聊过的，如果你的机器是 16 个逻辑核心，Go 默认的 GOMAXPROCS 就是 16，
	// 这 32 个 Goroutine 会非常均匀地分布在你的 CPU 上激烈竞争。
	benchmarkThroughput(10000000, 16, 16)
}

package main

import (
	"fmt"
	"runtime"
	"sync"
	"time"
)

// =====================================================================
// 实验 1: 海量协程创建与频繁 Yield (测试纯粹的状态机上下文切换开销)
// =====================================================================
func benchmarkYield(numRoutines, numYields int) {
	var wg sync.WaitGroup
	wg.Add(numRoutines)

	start := time.Now()

	for i := 0; i < numRoutines; i++ {
		go func() {
			defer wg.Done()
			// 循环主动让出执行权，模拟密集型调度
			for j := 0; j < numYields; j++ {
				runtime.Gosched()
			}
		}()
	}

	wg.Wait()
	elapsed := time.Since(start)
	totalSwitches := int64(numRoutines * numYields)

	fmt.Printf("[1] Yield Context Switch Benchmark\n")
	fmt.Printf("    Goroutines created : %d\n", numRoutines)
	fmt.Printf("    Yields per routine : %d\n", numYields)
	fmt.Printf("    Total switches     : %d\n", totalSwitches)
	fmt.Printf("    Time per switch    : %d ns\n\n", elapsed.Nanoseconds()/totalSwitches)
}

// =====================================================================
// 实验 2: Ping-Pong 延迟 (测试无缓冲 Channel 1v1 强同步下的唤醒开销)
// =====================================================================
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
		_ = res
	}

	totalNs := time.Since(start).Nanoseconds()
	fmt.Printf("[2] Ping-Pong Latency Benchmark\n")
	fmt.Printf("    Iterations       : %d\n", n)
	// 一次完整的 Ping-Pong 包含两次切换 (A->B, B->A)
	fmt.Printf("    Ping-Pong latency: %d ns/op (per switch)\n\n", totalNs/(int64(n)*2))
}

// =====================================================================
// 实验 3: MPMC 吞吐量 (测试有缓冲 Channel 在多协程激烈竞争下的表现)
// =====================================================================
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

	wg.Wait()
	elapsed := time.Since(start).Seconds()
	msgsPerSec := float64(totalMsgs) / elapsed

	fmt.Printf("[3] MPMC Throughput Benchmark\n")
	fmt.Printf("    Producers : %d\n", pCount)
	fmt.Printf("    Consumers : %d\n", cCount)
	fmt.Printf("    Total Msgs: %d\n", totalMsgs)
	fmt.Printf("    Throughput: %.0f msgs/sec\n\n", msgsPerSec)
}

func main() {
	// ------------------------------------------------------------------
	// 【关键配置】单核 vs 多核
	// ------------------------------------------------------------------
	// 如果你的无栈协程库是单线程调度器（Event Loop 模式），
	// 请取消下面这行的注释，限制 Go 只使用 1 个逻辑 CPU。
	// 这样才能保证对比环境的绝对公平（不引入操作系统线程间的锁竞争）。
	//
	// runtime.GOMAXPROCS(1)
	// ------------------------------------------------------------------

	fmt.Printf("=== Go Goroutine Benchmark Suite ===\n")
	fmt.Printf("Current GOMAXPROCS (Logical Cores used): %d\n", runtime.GOMAXPROCS(0))
	fmt.Printf("------------------------------------\n\n")

	// 运行实验
	benchmarkYield(1000000, 100)          // 100万协程，各切换100次
	benchmarkPingPong(10000000)           // 1000万次互相发送接收
	benchmarkThroughput(10000000, 16, 16) // 16生产16消费，共处理1000万消息
}

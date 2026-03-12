package main

import (
	"fmt"
	"runtime"
	"sync"
	"time"
)

func main() {
	// 1. 强制 GC 确保起点干净
	runtime.GC()

	var m1, m2 runtime.MemStats
	runtime.ReadMemStats(&m1)

	count := 1000
	var wg sync.WaitGroup
	wg.Add(count)

	stop := make(chan struct{})

	// 2. 记录创建前的内存
	beforeAlloc := m1.Sys

	for i := 0; i < count; i++ {
		go func() {
			wg.Done()
			<-stop // 让协程保持阻塞，不退出
		}()
	}

	// 等待所有协程启动完成
	wg.Wait()

	// 给系统一点点稳定时间
	time.Sleep(100 * time.Millisecond)

	// 3. 记录创建后的内存
	runtime.ReadMemStats(&m2)
	afterAlloc := m2.Sys

	totalUsed := afterAlloc - beforeAlloc
	avgUsed := totalUsed / uint64(count)

	fmt.Printf("--- 测试报告 (数量: %d) ---\n", count)
	fmt.Printf("总新增内存占用 (Sys): %v KB\n", totalUsed/1024)
	fmt.Printf("平均每个协程占用: %v 字节 (约 %.2f KB)\n", avgUsed, float64(avgUsed)/1024)
	fmt.Printf("当前运行中的协程数: %d\n", runtime.NumGoroutine())

	// 清理阶段
	close(stop)
}

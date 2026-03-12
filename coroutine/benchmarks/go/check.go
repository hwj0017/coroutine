package main

import (
	"fmt"
	"runtime"
	"sync"
	"time"
)

const cSwitch = 100000000 // 1亿次切换

func main() {
	// 关键：限制在单核运行，模拟单线程下的协程切点开销
	runtime.GOMAXPROCS(1)

	var wg sync.WaitGroup
	wg.Add(2)

	start := time.Now()

	// 启动两个相互竞争/让步的协程
	task := func() {
		defer wg.Done()
		for i := 0; i < cSwitch/2; i++ {
			runtime.Gosched() // 主动让出执行权
		}
	}

	go task()
	go task()

	wg.Wait()

	dur := time.Since(start)
	ms := dur.Milliseconds()
	ns := dur.Nanoseconds()

	fmt.Printf("Total Cost: %d ms\n", ms)
	fmt.Printf("Per Switch (approx): %d ns\n", ns/int64(cSwitch))

	perf := float64(cSwitch) / float64(ms) / 10.0
	fmt.Printf("Performance: %.1f w/s\n", perf)
	fmt.Println("Done")
}

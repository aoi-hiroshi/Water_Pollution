# Adaptive Thread Pool

`water::concurrency::ThreadPool` is the bounded producer-consumer executor used
by the future Muduo server. It depends only on the C++20 standard library.

## Lifecycle

1. Construct with `ThreadPoolOptions`.
2. Call `start()` once from the owning thread.
3. Submit work with `post`, `submit`, `trySubmit`, `submitFor`,
   `submitPackaged`, or `submitStoppable`.
4. Call `shutdown(Drain)` during normal service shutdown.

The pool is not restartable. `start()`, `reserve()`, `shutdown()`, and object
destruction must be controlled by the pool owner, not by a worker task.

## Capacity and backpressure

- `core_threads` are created by `start()` and remain alive while the pool runs.
- When `active_threads + queued_tasks` exceeds current capacity, elastic
  workers are created up to `max_threads`.
- Elastic workers retire after `idle_timeout` without work.
- The queue is bounded by `queue_capacity`.
- `post`, `submit`, and `trySubmit` never wait for queue space. Muduo I/O
  callbacks should use these APIs so an EventLoop is never blocked.
- `postFor` and `submitFor` are intended for non-I/O producer threads that can
  tolerate a bounded wait. A task running inside this same pool should also
  avoid a blocking submission, because all workers could otherwise wait for
  queue space that only those workers can create.

## Futures and cancellation

`submit` binds a callable and its arguments with `std::bind`, wraps it in a
`std::packaged_task`, and returns the associated `std::future`. Callable
exceptions are delivered by `future::get()`.

`submitStoppable` expects a callable whose first argument is `std::stop_token`.
`shutdown(CancelPending)` requests that token. Cancellation is cooperative: an
already running task must inspect the token and return. Futures belonging to
discarded queued tasks become ready with `future_errc::broken_promise`.

## Example

```cpp
using namespace std::chrono_literals;
using water::concurrency::ThreadPool;

ThreadPool pool({
    .name = "inference",
    .core_threads = 2,
    .max_threads = 8,
    .queue_capacity = 256,
    .idle_timeout = 30s,
});
pool.start();

auto result = pool.submit([](int left, int right) {
    return left + right;
}, 20, 22);

auto cancellable = pool.submitStoppable([](std::stop_token token) {
    while (!token.stop_requested()) {
        // Process one bounded unit of work.
    }
});

const int value = result.get();
pool.shutdown();
```

## Operational metrics

`statistics()` returns live/active/peak workers, queue usage, accepted,
completed, rejected and cancelled tasks, uncaught fire-and-forget exceptions,
and worker creation failures. The server metrics endpoint can expose these
values without changing the thread-pool API.

#ifndef AMXXRPC_QUEUE_H
#define AMXXRPC_QUEUE_H

// Bounded mutex + condition_variable queue (design/11 §6).
// One mutex per queue; no nested queue locks. Push is non-blocking (TryPush) so
// neither the I/O thread nor the main thread can be blocked by a full queue.

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

template <typename T>
class Queue {
public:
	explicit Queue(size_t capacity) : m_capacity(capacity ? capacity : 1) {}

	Queue(const Queue&) = delete;
	Queue& operator=(const Queue&) = delete;

	// Returns false when the queue is full or closed.
	bool TryPush(T item) {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_closed || m_items.size() >= m_capacity)
			return false;
		m_items.push_back(std::move(item));
		m_cv.notify_one();
		return true;
	}

	// Returns false when the queue is empty.
	bool TryPop(T& out) {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_items.empty())
			return false;
		out = std::move(m_items.front());
		m_items.pop_front();
		return true;
	}

	// Blocks until an item is available or the queue is closed.
	bool WaitPop(T& out) {
		std::unique_lock<std::mutex> lock(m_mutex);
		m_cv.wait(lock, [this] { return m_closed || !m_items.empty(); });
		if (m_items.empty())
			return false;
		out = std::move(m_items.front());
		m_items.pop_front();
		return true;
	}

	void Close() {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_closed = true;
		m_cv.notify_all();
	}

	size_t Size() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_items.size();
	}

	size_t Capacity() const { return m_capacity; }

	bool IsClosed() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_closed;
	}

private:
	mutable std::mutex m_mutex;
	std::condition_variable m_cv;
	std::deque<T> m_items;
	size_t m_capacity;
	bool m_closed = false;
};

#endif // AMXXRPC_QUEUE_H

#pragma once

#include <vector>
#include <list>
#include <type_traits>
#include <stdexcept>
#include "hashmap.hpp"

namespace Granite
{

struct RenderInfo
{
	// Plain function pointer so we can portably sort on it,
	// and the rendering function is kind of supposed to be a more
	// pure function anyways.
	// Adjacent render infos which share instance key will be batched together.
	void (*render)(RenderInfo **infos, unsigned instance_count) = nullptr;

	// RenderInfos with same key can be instanced.
	Hash instance_key = 0;

	// Sorting key.
	// Lower sorting keys will appear earlier.
	uint64_t sorting_key = 0;

	// RenderInfo objects cannot be deleted.
	// Classes which inherit from this class just be trivially destructible.
	// Classes which inherit data here are supposed to live temporarily and
	// should only hold POD data.
	// Dynamic allocation can be made from the RenderQueue.
	~RenderInfo() = default;
};

class RenderQueue
{
public:
	enum { BlockSize = 256 * 1024 };

	template <typename T, typename... P>
	T &emplace(P&&... p)
	{
		static_assert(std::is_trivially_destructible<T>::value, "Dispatchable type is not trivially destructible!");
		void *buffer = allocate(sizeof(T), alignof(T));
		if (!buffer)
			throw std::bad_alloc();

		T *t = new(buffer) T(std::forward<P>(p)...);
		enqueue(t);
		return *t;
	}

	template <typename T>
	T *allocate_multiple(size_t n)
	{
		static_assert(std::is_trivially_destructible<T>::value, "Dispatchable type is not trivially destructible!");
		void *buffer = allocate(sizeof(T) * n, alignof(T));
		if (!buffer)
			throw std::bad_alloc();

		T *t = new(buffer) T[n]();
		for (size_t i = 0; i < n; i++)
			enqueue(&t[i]);

		return t;
	}

	void *allocate(size_t size, size_t alignment = 64);
	void enqueue(RenderInfo *render_info);
	void combine_render_info(const RenderQueue &queue);
	void reset();
	void reset_and_reclaim();

	RenderInfo **get_queue() const
	{
		return queue;
	}

	size_t get_queue_count() const
	{
		return count;
	}

	void sort();
	void dispatch();
	void dispatch(size_t begin, size_t end);

private:
	struct Block
	{
		std::vector<uint8_t> buffer;
		uintptr_t ptr = 0;
		uintptr_t begin = 0;
		uintptr_t end = 0;

		Block(size_t size)
		{
			buffer.resize(size);
			begin = reinterpret_cast<uintptr_t>(buffer.data());
			end = reinterpret_cast<uintptr_t>(buffer.data()) + size;
			reset();
		}

		void operator=(const Block &) = delete;
		Block(const Block &) = delete;
		Block(Block &&) = default;
		Block &operator=(Block &&) = default;

		void reset()
		{
			ptr = begin;
		}
	};

	using Chain = std::list<Block>;
	Chain blocks;
	Chain::iterator current = std::end(blocks);

	RenderInfo **queue = nullptr;
	size_t count = 0;
	size_t capacity = 0;

	void *allocate_from_block(Block &block, size_t size, size_t alignment);
	Chain::iterator insert_block();
};
}
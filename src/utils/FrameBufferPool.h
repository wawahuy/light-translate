#pragma once
#include <vector>
#include <mutex>

class FrameBufferPool
{
public:
    static FrameBufferPool& Instance()
    {
        static FrameBufferPool instance;
        return instance;
    }

    std::vector<uint8_t> Acquire(size_t size)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it)
        {
            if (it->capacity() >= size)
            {
                auto buf = std::move(*it);
                m_buffers.erase(it);
                buf.resize(size);
                return buf;
            }
        }
        return std::vector<uint8_t>(size);
    }

    void Release(std::vector<uint8_t>&& buf)
    {
        // Don't keep empty vectors in the pool
        if (buf.capacity() == 0) return;

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_buffers.size() < 4) // Limit to 4 buffers in pool
        {
            m_buffers.push_back(std::move(buf));
        }
    }

private:
    FrameBufferPool() = default;
    ~FrameBufferPool() = default;
    std::mutex m_mutex;
    std::vector<std::vector<uint8_t>> m_buffers;
};

//
// jalloc.hpp - Just an Allocator™
// A high-performance, thread-safe memory allocator for C/C++
//
// Features:
// - Thread-safe & high-performance memory allocation
// - Multi-tiered allocation strategy for different sizes
// - SIMD-optimized memory operations
// - Automatic memory coalescing and return-to-OS policies
//
// Version: 1.0.0
// Author: alpluspluss
// Updated: 10/28/2024
// Created: 10/22/2024
// License: MIT
//
// This project is no longer maintained. Please feel free to fork and modify it.
// FYI: This allocator works best with small to medium-sized allocations (>= 4KB).
//

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>

#if defined(__x86_64__)
    #include <immintrin.h>
    #include <emmintrin.h>
    #include <xmmintrin.h>
    #include <smmintrin.h>
    #include <tmmintrin.h>
    #ifdef __AVX2__
        #include <avx2intrin.h>
    #endif
    #ifdef __AVX512F__
        #include <avx512fintrin.h>
    #endif

    #ifdef _MSC_VER
        #include <intrin.h>
    #endif

    // CPU Feature Detection functions
    //--------------------------------------------------------------------------
    // Detects availability of AVX2 and AVX-512 instructions at runtime
    // Returns: true if the feature is available, false otherwise
    //--------------------------------------------------------------------------
    #ifdef __GNUC__
        #include <cpuid.h>
        ALWAYS_INLINE static bool cpu_has_avx2()
        {
            unsigned int eax, ebx, ecx, edx;
            __get_cpuid(7, &eax, &ebx, &ecx, &edx);
            return (ebx & bit_AVX2) != 0;
        }

        ALWAYS_INLINE static bool cpu_has_avx512f()
        {
            unsigned int eax, ebx, ecx, edx;
            __get_cpuid(7, &eax, &ebx, &ecx, &edx);
            return (ebx & bit_AVX512F) != 0;
        }
    #elif defined(_MSC_VER)
        ALWAYS_INLINE static bool cpu_has_avx2()
        {
            int cpuInfo[4];
            __cpuid(cpuInfo, 7);
            return (cpuInfo[1] & (1 << 5)) != 0;
        }

        ALWAYS_INLINE static bool cpu_has_avx512f()
        {
            int cpuInfo[4];
            __cpuid(cpuInfo, 7);
            return (cpuInfo[1] & (1 << 16)) != 0;
        }
    #endif

    //--------------------------------------------------------------------------
    // SIMD Operation Definitions
    // Provides unified interface for different SIMD instruction sets
    //--------------------------------------------------------------------------
    #ifdef __AVX512F__
        // AVX-512: 64-byte vector operations
        #define VECTOR_WIDTH 64
        #define STREAM_STORE_VECTOR(addr, val) _mm512_stream_si512((__m512i*)(addr), val)
        #define LOAD_VECTOR(addr) _mm512_loadu_si512((const __m512i*)(addr))
        #define STORE_VECTOR(addr, val) _mm512_storeu_si512((__m512i*)(addr), val)
        #define SET_ZERO_VECTOR() _mm512_setzero_si512()
    #elif defined(__AVX2__)
        // AVX2: 32-byte vector operations
        #define VECTOR_WIDTH 32
        #define STREAM_STORE_VECTOR(addr, val) _mm256_stream_si256((__m256i*)(addr), val)
        #define LOAD_VECTOR(addr) _mm256_loadu_si256((const __m256i*)(addr))
        #define STORE_VECTOR(addr, val) _mm256_storeu_si256((__m256i*)(addr), val)
        #define SET_ZERO_VECTOR() _mm256_setzero_si256()
    #else
        // SSE: 16-byte vector operations (fallback)
        #define VECTOR_WIDTH 16
        #define STREAM_STORE_VECTOR(addr, val) _mm_stream_si128((__m128i*)(addr), val)
        #define LOAD_VECTOR(addr) _mm_loadu_si128((const __m128i*)(addr))
        #define STORE_VECTOR(addr, val) _mm_storeu_si128((__m128i*)(addr), val)
        #define SET_ZERO_VECTOR() _mm_setzero_si128()
    #endif

    // Common operations available across all x86_64 platforms
    #define STREAM_STORE_64(addr, val) _mm_stream_si64((__int64*)(addr), val)
    #define CPU_PAUSE() _mm_pause()
    #define MEMORY_FENCE() _mm_sfence()
    #define CUSTOM_PREFETCH(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
    #define PREFETCH_WRITE(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
    #define PREFETCH_READ(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_NTA)

#elif defined(__arm__) || defined(__aarch64__)
    // ARM/AArch64 SIMD support using NEON
    #include <arm_neon.h>
    #ifdef __clang__
        #include <arm_acle.h>
    #endif

    // ARM NEON: 16-byte vector operations
    #define VECTOR_WIDTH 16

    #if defined(__aarch64__)
        // 64-bit ARM specific optimizations
        #define STREAM_STORE_VECTOR(addr, val) vst1q_u8((uint8_t*)(addr), val)
        #define LOAD_VECTOR(addr) vld1q_u8((const uint8_t*)(addr))
        #define STORE_VECTOR(addr, val) vst1q_u8((uint8_t*)(addr), val)
        #define SET_ZERO_VECTOR() vdupq_n_u8(0)
        #define STREAM_STORE_64(addr, val) vst1_u64((uint64_t*)(addr), vcreate_u64(val))
        #define MEMORY_FENCE() __dmb(SY)

        // ARM-specific prefetch hints
        #define CUSTOM_PREFETCH(addr) __asm__ volatile("prfm pldl1keep, [%0]" : : "r" (addr))
        #define PREFETCH_WRITE(addr) __asm__ volatile("prfm pstl1keep, [%0]" : : "r" (addr))
        #define PREFETCH_READ(addr) __asm__ volatile("prfm pldl1strm, [%0]" : : "r" (addr))
    #else
        // 32-bit ARM fallbacks
        #define STREAM_STORE_VECTOR(addr, val) vst1q_u8((uint8_t*)(addr), val)
        #define LOAD_VECTOR(addr) vld1q_u8((const uint8_t*)(addr))
        #define STORE_VECTOR(addr, val) vst1q_u8((uint8_t*)(addr), val)
        #define SET_ZERO_VECTOR() vdupq_n_u8(0)
        #define STREAM_STORE_64(addr, val) *((int64_t*)(addr)) = val
        #define MEMORY_FENCE() __dmb(SY)
        #define CUSTOM_PREFETCH(addr) __pld(reinterpret_cast<const char*>(addr))
        #define PREFETCH_WRITE(addr) __pld(reinterpret_cast<const char*>(addr))
        #define PREFETCH_READ(addr) __pld(reinterpret_cast<const char*>(addr))
    #endif

    #define CPU_PAUSE() __yield()

#else
    // Generic fallback for unsupported architectures
    // Provides basic functionality without SIMD optimizations
    #define VECTOR_WIDTH 8
    #define STREAM_STORE_VECTOR(addr, val) *((int64_t*)(addr)) = val
    #define LOAD_VECTOR(addr) *((const int64_t*)(addr))
    #define STORE_VECTOR(addr, val) *((int64_t*)(addr)) = val
    #define SET_ZERO_VECTOR() 0
    #define STREAM_STORE_64(addr, val) *((int64_t*)(addr)) = val
    #define CPU_PAUSE() ((void)0)
    #define MEMORY_FENCE() std::atomic_thread_fence(std::memory_order_seq_cst)
    #define CUSTOM_PREFETCH(addr) ((void)0)
    #define PREFETCH_WRITE(addr) ((void)0)
    #define PREFETCH_READ(addr) ((void)0)
#endif

// Compiler-specific optimizations and attributes
#if defined(__GNUC__) || defined(__clang__)
    // GCC/Clang specific optimizations
    #define LIKELY(x) __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
    #define ALWAYS_INLINE [[gnu::always_inline]] inline
    #define ALIGN_TO(x) __attribute__((aligned(x)))

    #if defined(__clang__)
        // Clang-specific optimizations
        #define HAVE_BUILTIN_ASSUME(x) __builtin_assume(x)
        #define HAVE_BUILTIN_ASSUME_ALIGNED(x, a) __builtin_assume_aligned(x, a)
        #define NO_SANITIZE_ADDRESS __attribute__((no_sanitize("address")))
        #define VECTORIZE_LOOP _Pragma("clang loop vectorize(enable) interleave(enable)")
#define UNROLL_LOOP _Pragma("clang loop unroll(full)")
#else
        // GCC-specific fallbacks
        #define HAVE_BUILTIN_ASSUME(x) ((void)0)
        #define HAVE_BUILTIN_ASSUME_ALIGNED(x, a) (x)
        #define NO_SANITIZE_ADDRESS
        #define VECTORIZE_LOOP _Pragma("GCC ivdep")
        #define UNROLL_LOOP _Pragma("GCC unroll 8")
#endif
#else
    // Generic fallbacks for other compilers
    #define LIKELY(x) (x)
    #define UNLIKELY(x) (x)
    #define ALWAYS_INLINE inline
    #define ALIGN_TO(x)
    #define HAVE_BUILTIN_ASSUME(x) ((void)0)
    #define HAVE_BUILTIN_ASSUME_ALIGNED(x, a) (x)
    #define NO_SANITIZE_ADDRESS
    #define VECTORIZE_LOOP
    #define CUSTOM_PREFETCH(addr) ((void)0)
#endif

// Platform-specific memory management operations
#ifdef _WIN32
    #include <malloc.h>
    #include <windows.h>
    // Windows virtual memory management
    #define MAP_MEMORY(size) \
        VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
    #define UNMAP_MEMORY(ptr, size) VirtualFree(ptr, 0, MEM_RELEASE)
    #ifndef MAP_FAILED
        #define MAP_FAILED nullptr
    #endif
    #define ALIGNED_ALLOC(alignment, size) _aligned_malloc(size, alignment)
    #define ALIGNED_FREE(ptr) _aligned_free(ptr)
#elif defined(__APPLE__)
    #include <mach/mach.h>
#include <sys/mman.h>

#define MAP_MEMORY(size) \
        mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
    #define UNMAP_MEMORY(ptr, size) munmap(ptr, size)
    #define ALIGNED_ALLOC(alignment, size) aligned_alloc(alignment, size)
    #define ALIGNED_FREE(ptr) free(ptr)

#else
    // POSIX-compliant systems (Linux, BSD, etc.)
    #include <sched.h>
    #include <unistd.h>
    #include <sys/mman.h>
    #include <thread>

    #define MAP_MEMORY(size) \
        mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
    #define UNMAP_MEMORY(ptr, size) munmap(ptr, size)
    #define ALIGNED_ALLOC(alignment, size) aligned_alloc(alignment, size)
    #define ALIGNED_FREE(ptr) free(ptr)
#endif


// Allocator constants
// Hardware constants
static constexpr size_t CACHE_LINE_SIZE = 64; //Changable to 32 | 64
static constexpr size_t PG_SIZE = 4096;

// Block header
static constexpr size_t TINY_LARGE_THRESHOLD = 64;
static constexpr size_t SMALL_LARGE_THRESHOLD = 256;
static constexpr size_t ALIGNMENT = CACHE_LINE_SIZE;
static constexpr size_t LARGE_THRESHOLD = 1024 * 1024;
static constexpr size_t MAX_CACHED_BLOCKS = 32;
static constexpr size_t MAX_CACHE_SIZE = 64 * 1024 * 1024;
static constexpr size_t MIN_CACHE_BLOCK = 4 * 1024;
static constexpr size_t MAX_CACHE_BLOCK = 16 * 1024 * 1024;
static constexpr auto MAX_SIZE_RATIO = 1.25;

static constexpr size_t CACHE_SIZE = 32;
static constexpr size_t SIZE_CLASSES = 32;
static constexpr size_t TINY_CLASSES = 8;
static constexpr size_t MAX_POOLS = 8;

// Safety flags
static constexpr uint64_t SIZE_MASK = 0x0000FFFFFFFFFFFF;
static constexpr uint64_t CLASS_MASK = 0x00FF000000000000;
static constexpr uint64_t MMAP_FLAG = 1ULL << 62;
static constexpr uint64_t COALESCED_FLAG = 1ULL << 61;
static constexpr uint64_t HEADER_MAGIC = 0xDEADBEEF12345678;
static constexpr uint64_t MAGIC_MASK = 0xF000000000000000;
static constexpr uint64_t MAGIC_VALUE = 0xA000000000000000;
static constexpr uint64_t THREAD_OWNER_MASK = 0xFFFF000000000000;

struct size_class
{
    uint16_t size;
    uint16_t slot_size;
    uint16_t blocks;
    uint16_t slack;
};

static constexpr size_t get_alignment_for_size(const size_t size) noexcept
{
    return size <= CACHE_LINE_SIZE
               ? CACHE_LINE_SIZE
               : size >= PG_SIZE
                     ? PG_SIZE
                     : 1ULL << (64 - __builtin_clzll(size - 1));
}

constexpr std::array<size_class, 32> size_classes = []
{
    std::array<size_class, 32> classes{};
    for (size_t i = 0; i < 32; ++i)
    {
        const size_t size = 1ULL << (i + 3);
        const size_t alignment = get_alignment_for_size(size); // Use new function
        const size_t slot = (size + alignment - 1) & ~(alignment - 1);
        classes[i] = {
            static_cast<uint16_t>(size),
            static_cast<uint16_t>(slot),
            static_cast<uint16_t>(PG_SIZE / slot),
            static_cast<uint16_t>(slot - size)
        };
    }
    return classes;
}();

struct thread_cache_t
{
    struct cached_block
    {
        void* ptr;
        uint8_t size_class;
    };

    struct size_class_cache
    {
        cached_block blocks[CACHE_SIZE];
        size_t count;
    };

    alignas(CACHE_LINE_SIZE) size_class_cache caches[SIZE_CLASSES]{};

    ALWAYS_INLINE
    void *get(const uint8_t size_class) noexcept
    {
        auto &[blocks, count] = caches[size_class];
        if (LIKELY(count > 0))
        {
            PREFETCH_READ(&blocks[count - 2]);
            return blocks[--count].ptr;
        }
        return nullptr;
    }

    ALWAYS_INLINE
    bool put(void *ptr, const uint8_t size_class) noexcept
    {
        auto &cache = caches[size_class];
        if (LIKELY(cache.count < CACHE_SIZE))
        {
            cache.blocks[cache.count].ptr = ptr;
            cache.blocks[cache.count].size_class = size_class;
            ++cache.count;
            return true;
        }
        return false;
    }

    ALWAYS_INLINE
    void clear() noexcept
    {
        for (auto&[blocks, count] : caches)
            count = 0;
    }
};

ALWAYS_INLINE
static void prefetch_for_read(const void* addr) noexcept
{
    CUSTOM_PREFETCH(addr);
}

template<size_t stride = 64>
ALWAYS_INLINE
static void prefetch_range(const void* addr, const size_t size) noexcept
{
    auto ptr = static_cast<const char*>(addr);

    for (const char* end = ptr + size; ptr < end; ptr += stride)
        CUSTOM_PREFETCH(ptr);
}

template<typename T>
ALWAYS_INLINE
static void stream_store(void* dst, const T& value) noexcept
{
    STREAM_STORE_64(dst, static_cast<int64_t>(value));
}

#if defined(__x86_64__)
    static ALWAYS_INLINE size_t count_trailing_zeros(uint64_t x)
    {
        #ifdef _MSC_VER
            return _tzcnt_u64(x);
        #else
             return __builtin_ctzll(x);
        #endif
    }

    static ALWAYS_INLINE void memory_fence()
    {
    _mm_mfence();
    }

    static ALWAYS_INLINE void prefetch(const void* addr)
    {
    _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T0);
    }
#elif defined(__arm__) || defined(__aarch64__)
    ALWAYS_INLINE
    static size_t count_trailing_zeros(const uint64_t x)
    {
        return __builtin_ctzll(x);
    }
    ALWAYS_INLINE
    static void memory_fence()
    {
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
    ALWAYS_INLINE
    static void prefetch(const void* addr)
    {
        prefetch_for_read(addr);
    }
#else
    static ALWAYS_INLINE size_t count_trailing_zeros(uint64_t x)
    {
        return __builtin_ctzll(x);
    }

    static ALWAYS_INLINE void memory_fence()
    {
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    static ALWAYS_INLINE void prefetch(const void*) {}
#endif

ALWAYS_INLINE
static bool is_base_aligned(const void *ptr) noexcept
{
    return (reinterpret_cast<uintptr_t>(ptr) & (ALIGNMENT - 1)) == 0;
}

class Jallocator
{
    struct bitmap
    {
        static constexpr size_t bits_per_word = 64;
        static constexpr size_t words_per_bitmap = PG_SIZE / (CACHE_LINE_SIZE * 8);

        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> words[words_per_bitmap];

        bitmap() noexcept
        {
            for (auto& word : words)
                word = ~0ULL;
        }

        ALWAYS_INLINE
        size_t find_free_block(const size_t size) noexcept
        {
            const size_t alignment = get_alignment_for_size(size);
            const size_t align_mask = (alignment / bits_per_word) - 1;
            #if defined(__AVX512F__)
                for (size_t i = 0; i < words_per_bitmap; i += 8)
                {
                    if ((i & align_mask) != 0)
                        continue;

                    __m512i v = _mm512_loadu_si512((__m512i*)(words + i));
                    __mmask8 mask = _mm512_movepi64_mask(v);
                    if (mask)
                    {
                        const int lane = __builtin_ctz(mask);
                        const uint64_t word = words[i + lane].load(std::memory_order_relaxed);
                        const size_t bit = count_trailing_zeros(word);
                        const size_t block_offset = (i + lane) * 64 + bit;
                        if (block_offset * CACHE_LINE_SIZE % alignment == 0)
                            return block_offset;
                    }
                }
            #elif defined(__AVX2__)
                for (size_t i = 0; i < words_per_bitmap; i += 4)
                {
                    __m256i v = _mm256_loadu_si256((__m256i*)(words + i));
                    __m256i zero = _mm256_setzero_si256();
                    __m256i cmp = _mm256_cmpeq_epi64(v, zero);
                    uint32_t mask = ~_mm256_movemask_epi8(cmp);
                    if (mask != 0)
                        return i * 64 + __builtin_ctzll(mask);
                }
            #elif defined(__aarch64__)
                for (size_t i = 0; i < words_per_bitmap; i += 2)
                {
                    uint64x2_t v = vld1q_u64(reinterpret_cast<const uint64_t *>(words + i));
                    uint64x2_t zero = vdupq_n_u64(0);
                    if (vgetq_lane_u64(vceqq_u64(v, zero), 0) != -1ULL)
                        return i * 64 + __builtin_ctzll(words[i].load(std::memory_order_relaxed));
                    if (vgetq_lane_u64(vceqq_u64(v, zero), 1) != -1ULL)
                        return (i + 1) * 64 + __builtin_ctzll(words[i+1].load(std::memory_order_relaxed));
                }
            #endif

            VECTORIZE_LOOP
            for (size_t i = 0; i < words_per_bitmap; ++i)
            {
                if ((i & align_mask) != 0)
                    continue;

                if (i + 1 < words_per_bitmap)
                    prefetch(&words[i + 1]);

                uint64_t expected = words[i].load(std::memory_order_relaxed);
                while (expected != 0)
                {
                    const size_t bit = count_trailing_zeros(expected);
                    const size_t block_offset = i * bits_per_word + bit;

                    if (const uint64_t desired = expected & ~(1ULL << bit);
                        words[i].compare_exchange_weak(
                            expected, desired,
                            std::memory_order_acquire,
                            std::memory_order_relaxed))
                    {
                        memory_fence();
                        return block_offset;
                    }
                }
            }
            return ~static_cast<size_t>(0);
        }

        ALWAYS_INLINE
        void mark_free(const size_t index) noexcept
        {
            const size_t word_idx = index / bits_per_word;
            const size_t bit_idx = index % bits_per_word;
            prefetch(&words[word_idx]);
            words[word_idx].fetch_or(1ULL << bit_idx, std::memory_order_release);
        }

        ALWAYS_INLINE
        bool is_completely_free() const noexcept
        {
            for (size_t i = 0; i < words_per_bitmap; ++i)
            {
                if (i + 1 < words_per_bitmap)
                    prefetch(&words[i + 1]);
                if (words[i].load(std::memory_order_relaxed) != ~0ULL)
                    return false;
            }
            return true;
        }
    };

    struct alignas(ALIGNMENT) block_header
    {
        // Bit field layout:
        // [63]    - Free flag
        // [62]    - Memory mapped flag
        // [61]    - Coalesced flag
        // [56-48] - Size class
        // [47-0]  - Block size
        uint64_t data;
        uint64_t magic;
        block_header* prev_physical;
        block_header* next_physical;

        void init(const size_t sz, const uint8_t size_class, const bool is_free,
                  block_header *prev = nullptr, block_header *next = nullptr) noexcept
        {
            if (UNLIKELY(sz > (1ULL << 47)))
            {
                magic = 0;
                data = 0;
                prev_physical = nullptr;
                next_physical = nullptr;
                return;
            }
            magic = HEADER_MAGIC;
            encode(sz, size_class, is_free);
            prev_physical = prev;
            next_physical = next;
        }

        ALWAYS_INLINE
        void encode(const size_t size, const uint8_t size_class, const bool is_free) noexcept
        {
            data = (size & SIZE_MASK) |
               (static_cast<uint64_t>(size_class) << 48) |
               (static_cast<uint64_t>(is_free) << 63) |
               MAGIC_VALUE;
        }

        ALWAYS_INLINE
        bool is_valid() const noexcept
        {
            if (LIKELY(magic == HEADER_MAGIC &&
                (data & MAGIC_MASK) == MAGIC_VALUE))
            {
                return size() <= (1ULL << 47) &&
                       (size_class() < SIZE_CLASSES || size_class() == 255);
            }
            return false;
        }

        ALWAYS_INLINE
        void set_free(const bool is_free) noexcept
        {
            data = (data & ~(1ULL << 63)) | static_cast<uint64_t>(is_free) << 63;
        }

        ALWAYS_INLINE
        void set_memory_mapped(const bool is_mmap) noexcept
        {
            data = (data & ~MMAP_FLAG) | static_cast<uint64_t>(is_mmap) << 62;
        }

        size_t size() const noexcept
        {
            return data & SIZE_MASK;
        }
        uint8_t size_class() const noexcept
        {
            return (data & CLASS_MASK) >> 48;
        }
        bool is_free() const noexcept
        {
            return data & 1ULL << 63;
        }
        bool is_memory_mapped() const noexcept
        {
            return data & MMAP_FLAG;
        }

        // Check if the block is perfectly aligned to the cache line size (64B)
        // and verify if the pointer is corrupted or not
        // The performance trade-offs are worth it
        // Please note in mind that this DOES NOT check
        // 1. Perfectly-aligned corrupted pointers
        // 2. Maliciously-aligned pointers
        // 3.
        // Modified alignment check
        static bool is_aligned(const void *ptr) noexcept
        {
            if (!is_base_aligned(ptr))
                return false;

            const auto header = reinterpret_cast<const block_header *>(
                static_cast<const char *>(ptr) - sizeof(block_header));

            if (!is_base_aligned(header))
                return false;

            if (header->magic != HEADER_MAGIC)
                return false;

            const size_t size_alignment = get_alignment_for_size(header->size());
            return (reinterpret_cast<uintptr_t>(ptr) & (size_alignment - 1)) == 0;
        }

        bool try_coalesce() noexcept
        {
            if (is_memory_mapped() || size_class() < TINY_CLASSES)
                return false;

            auto coalesced = false;

            if (next_physical && next_physical->is_free())
            {
                const size_t combined_size = size() + next_physical->size() + sizeof(block_header);
                next_physical = next_physical->next_physical;
                if (next_physical)
                    next_physical->prev_physical = this;
                encode(combined_size, size_class(), true);
                set_coalesced(true);
                coalesced = true;
            }

            if (prev_physical && prev_physical->is_free())
            {
                const size_t combined_size = size() + prev_physical->size() + sizeof(block_header);
                prev_physical->next_physical = next_physical;
                if (next_physical)
                    next_physical->prev_physical = prev_physical;
                prev_physical->encode(combined_size, prev_physical->size_class(), true);
                prev_physical->set_coalesced(true);
                coalesced = true;
            }
            return coalesced;
        }

        ALWAYS_INLINE
        void set_coalesced(const bool is_coalesced) noexcept
        {
            data = (data & ~COALESCED_FLAG) | (static_cast<uint64_t>(is_coalesced) << 61);
        }

        ALWAYS_INLINE
        bool is_coalesced() const noexcept
        {
            return data & COALESCED_FLAG;
        }
    };

    struct alignas(PG_SIZE) pool
    {
        static constexpr size_t MIN_RETURN_SIZE = 64 * 1024;
        static constexpr auto MEM_USAGE_THRESHOLD = 0.2;
        bitmap bitmap;
        uint8_t memory[PG_SIZE - sizeof(bitmap)]{};

        ALWAYS_INLINE
        void *allocate(const size_class &sc) noexcept
        {
            if (const size_t index = bitmap.find_free_block(sc.size);
                index != ~static_cast<size_t>(0))
            {
                return memory + index * sc.slot_size;
            }
            return nullptr;
        }

        ALWAYS_INLINE
        void deallocate(void* ptr, const size_class& sc) noexcept
        {
            const size_t offset = static_cast<uint8_t*>(ptr) - memory;
            bitmap.mark_free(offset / sc.slot_size);
        }

        ALWAYS_INLINE
        bool is_completely_free() const noexcept
        {
            return bitmap.is_completely_free();
        }

        ALWAYS_INLINE
        void return_memory() noexcept
        {
            size_t free_space = 0;
            #if defined(__AVX512F__)
                const auto* current = reinterpret_cast<block_header*>(memory);
                __m512i sum = _mm512_setzero_si512();

                while (current)
                {
                    if (current->is_free())
                        sum = _mm512_add_epi64(sum, _mm512_set1_epi64(current->size()));
                    current = current->next_physical;
                }
                free_space = _mm512_reduce_add_epi64(sum);

            #elif defined(__AVX2__)
                const auto* current = reinterpret_cast<block_header*>(memory);
                // 4 accumulators gives the best throughput here
                __m256i sum1 = _mm256_setzero_si256();
                __m256i sum2 = _mm256_setzero_si256();
                __m256i sum3 = _mm256_setzero_si256();
                __m256i sum4 = _mm256_setzero_si256();

                while (current)
                {
                    block_header* next = current->next_physical;
                    if (next)
                        _mm_prefetch(reinterpret_cast<const char*>(next), _MM_HINT_T0);
                    // this is to reduce depend on chains
                    if (current->is_free())
                    {
                        __m256i size_vec = _mm256_set1_epi64x(current->size());
                        sum1 = _mm256_add_epi64(sum1, size_vec);
                        size_vec = _mm256_set1_epi64x(current->size() >> 1);
                        sum2 = _mm256_add_epi64(sum2, size_vec);
                        size_vec = _mm256_set1_epi64x(current->size() >> 2);
                        sum3 = _mm256_add_epi64(sum3, size_vec);
                        size_vec = _mm256_set1_epi64x(current->size() >> 3);
                        sum4 = _mm256_add_epi64(sum4, size_vec);
                    }
                    current = next;
                }

                // mash the accumulators together
                sum1 = _mm256_add_epi64(sum1, _mm256_slli_epi64(sum2, 1));
                sum3 = _mm256_add_epi64(sum3, _mm256_slli_epi64(sum4, 3));
                sum1 = _mm256_add_epi64(sum1, _mm256_slli_epi64(sum3, 2));

                // caclulate the total horizontal sum with the least possible instructions
                __m128i sum_low = _mm256_extracti128_si256(sum1, 0);
                __m128i sum_high = _mm256_extracti128_si256(sum1, 1);
                __m128i sum = _mm_add_epi64(sum_low, sum_high);
                sum = _mm_add_epi64(sum, _mm_shuffle_epi32(sum, _MM_SHUFFLE(1,0,3,2)));
                free_space = _mm_cvtsi128_si64(sum);

            #elif defined(__aarch64__)
                auto* current = reinterpret_cast<block_header*>(memory);
                uint64x2_t sum1 = vdupq_n_u64(0);
                uint64x2_t sum2 = vdupq_n_u64(0);
                uint64x2_t sum3 = vdupq_n_u64(0);
                uint64x2_t sum4 = vdupq_n_u64(0);

                while (current)
                {
                    block_header* next = current->next_physical;
                    if (next)
                    {
                       prefetch(next);
                    }

                    if (current->is_free())
                    {
                        const uint64_t size = current->size();
                        sum1 = vaddq_u64(sum1, vdupq_n_u64(size));
                        sum2 = vaddq_u64(sum2, vdupq_n_u64(size >> 1));
                        sum3 = vaddq_u64(sum3, vdupq_n_u64(size >> 2));
                        sum4 = vaddq_u64(sum4, vdupq_n_u64(size >> 3));
                    }
                    current = next;
                }

                const uint64x2_t sum_12 = vaddq_u64(sum1, vshlq_n_u64(sum2, 1));
                const uint64x2_t sum_34 = vaddq_u64(sum3, vshlq_n_u64(sum4, 3));
                const uint64x2_t final_sum = vaddq_u64(sum_12, vshlq_n_u64(sum_34, 2));

                free_space = vgetq_lane_u64(final_sum, 0) + vgetq_lane_u64(final_sum, 1);

            #else
                const auto* current = reinterpret_cast<block_header*>(memory);
                while (current)
                {
                    if (current->is_free())
                        free_space += current->size();
                    current = current->next_physical;
                }
            #endif

            if (free_space >= MIN_RETURN_SIZE &&
                static_cast<double>(free_space) / PG_SIZE >= (1.0 - MEM_USAGE_THRESHOLD))
            {

                current = reinterpret_cast<block_header*>(memory);
                while (current)
                {
                    if (current->is_free() && current->is_coalesced())
                    {
                        void* block_start = current + 1;
                        auto page_start = reinterpret_cast<void*>(
                            (reinterpret_cast<uintptr_t>(block_start) + PG_SIZE - 1) & ~(PG_SIZE - 1));
                        auto page_end = reinterpret_cast<void*>(
                            reinterpret_cast<uintptr_t>(block_start) + current->size() & ~(PG_SIZE - 1));

                        if (page_end > page_start)
                        {
                            #ifdef _WIN32
                                VirtualAlloc(page_start,
                                           reinterpret_cast<char*>(page_end) -
                                           reinterpret_cast<char*>(page_start),
                                           MEM_RESET,
                                           PAGE_READWRITE);
                            #elif defined(__APPLE__)
                                madvise(page_start,
                                       static_cast<char*>(page_end) -
                                       static_cast<char*>(page_start),
                                       MADV_FREE);
                            #else
                                madvise(page_start,
                                       reinterpret_cast<char*>(page_end) -
                                       reinterpret_cast<char*>(page_start),
                                       MADV_DONTNEED);
                            #endif
                        }
                    }
                    current = current->next_physical;
                }
            }
        }
    };

    struct tiny_block_manager
    {
        struct alignas(PG_SIZE) tiny_pool
        {
            bitmap bitmap;
            alignas(ALIGNMENT) uint8_t memory[PG_SIZE - sizeof(bitmap)]{};

            ALWAYS_INLINE
            void* allocate_tiny(const uint8_t size_class) noexcept
            {
                const size_t size = (size_class + 1) << 3;
                const size_t slot_size = (size + sizeof(block_header) + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
                const size_t max_blocks = (PG_SIZE - sizeof(bitmap)) / slot_size;

                if (const size_t index = bitmap.find_free_block(size);
                    index != ~static_cast<size_t>(0) && index < max_blocks)
                {
                    uint8_t *block = memory + index * slot_size;
                    if (block + slot_size <= memory + (PG_SIZE - sizeof(bitmap)))
                        return block;
                }
                return nullptr;
            }

            ALWAYS_INLINE
            void deallocate_tiny(void *ptr, const uint8_t size_class) noexcept
            {
                const size_t size = (size_class + 1) << 3;
                const size_t slot_size = (size + sizeof(block_header) + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
                const size_t offset = static_cast<uint8_t *>(ptr) - memory;
                const size_t index = offset / slot_size;

                if (index * slot_size < PG_SIZE - sizeof(bitmap))
                    bitmap.mark_free(index);
            }
        };
    };

    struct pool_manager
    {
        struct pool_entry
        {
            pool* p;
            size_t used_blocks;
        };

        alignas(CACHE_LINE_SIZE) pool_entry pools[SIZE_CLASSES][MAX_POOLS]{};
        size_t pool_count[SIZE_CLASSES]{};

        ALWAYS_INLINE
        void* allocate(const uint8_t size_class) noexcept
        {
            const auto& sc = size_classes[size_class];

            for (size_t i = 0; i < pool_count[size_class]; ++i)
            {
                auto&[p, used_blocks] = pools[size_class][i];
                if (void* ptr = p->allocate(sc))
                {
                    ++used_blocks;
                    return ptr;
                }
            }

            if (pool_count[size_class] < MAX_POOLS)
            {
                auto* new_pool = new (std::align_val_t{PG_SIZE}) pool();

                auto&[p, used_blocks] = pools[size_class][pool_count[size_class]];
                p = new_pool;
                used_blocks = 1;

                if (void* ptr = new_pool->allocate(sc))
                {
                    ++pool_count[size_class];
                    return ptr;
                }
                delete new_pool;
            }

            return nullptr;
        }

        ALWAYS_INLINE
        void deallocate(void* ptr, const uint8_t size_class) noexcept
        {
            if (UNLIKELY((reinterpret_cast<uintptr_t>(ptr) & ~(PG_SIZE-1)) == 0))
                return;

            const auto& sc = size_classes[size_class];
            if (UNLIKELY(size_class >= SIZE_CLASSES))
                return;

            for (size_t i = 0; i < pool_count[size_class]; ++i)
            {
                auto& entry = pools[size_class][i];
                const auto* pool_start = reinterpret_cast<const char*>(entry.p);

                if (const auto* pool_end = pool_start + PG_SIZE; ptr >= pool_start && ptr < pool_end)
                {
                    entry.p->deallocate(ptr, sc);
                    if (--entry.used_blocks == 0)
                    {
                        delete entry.p;
                        entry = pools[size_class][--pool_count[size_class]];
                    }
                    return;
                }
            }
        }

        ~pool_manager()
        {
            for (size_t sc = 0; sc < SIZE_CLASSES; ++sc)
            {
                const size_t count = pool_count[sc];
                VECTORIZE_LOOP
                for (size_t i = 0; i < count; ++i)
                    delete pools[sc][i].p;
            }
        }
    };

    struct alignas(CACHE_LINE_SIZE) large_block_cache_t
    {
        struct alignas(CACHE_LINE_SIZE) cache_entry
        {
            std::atomic<void *> ptr{nullptr};
            size_t size{0};
            uint64_t last_use{0};
        };

        struct alignas(CACHE_LINE_SIZE) size_bucket
        {
            static constexpr size_t BUCKET_SIZE = 4;
            std::atomic<size_t> count{0};
            cache_entry entries[BUCKET_SIZE];
        };

        static constexpr size_t NUM_BUCKETS = 8; // 4KB to 512KB
        alignas(CACHE_LINE_SIZE) size_bucket buckets[NUM_BUCKETS];
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> total_cached{0};

        ALWAYS_INLINE
        static uint64_t get_timestamp() noexcept
        {
#if defined(__x86_64__)
#if defined(_MSC_VER)
                    return __rdtsc();
#else
                    unsigned int aux;
                    return __rdtscp(&aux);
#endif
#elif defined(__aarch64__)
            // Use CNTVCT_EL0 (Virtual Count Register) for ARM64
            uint64_t timestamp;
#if defined(_MSC_VER)
                    timestamp = _ReadStatusReg(ARM64_CNTVCT_EL0);
#else
            asm volatile("mrs %0, cntvct_el0" : "=r"(timestamp));
#endif
            return timestamp;
#else
                // Fallback for other architectures
                return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
        }

        ALWAYS_INLINE
        static void memory_fence() noexcept
        {
#if defined(__x86_64__)
                _mm_mfence();
#elif defined(__aarch64__)
            __asm__ volatile("dmb sy" ::: "memory");
#else
                std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
        }

        ALWAYS_INLINE
        static void prefetch(const void *ptr) noexcept
        {
#if defined(__x86_64__)
                _mm_prefetch(static_cast<const char*>(ptr), _MM_HINT_T0);
#elif defined(__aarch64__)
            PREFETCH_READ(ptr); // Read, high temporal locality
#endif
        }

        ALWAYS_INLINE
        static size_t get_bucket_index(size_t size) noexcept
        {
#if defined(__x86_64__)
                return (63 - __builtin_clzll(size > MIN_CACHE_BLOCK ? size - 1 : MIN_CACHE_BLOCK)) - 11;
#elif defined(__aarch64__)
            return (63 - __builtin_clzll(size > MIN_CACHE_BLOCK ? size - 1 : MIN_CACHE_BLOCK)) - 11;
#else
                size_t index = 0;
                size_t threshold = MIN_CACHE_BLOCK;
                while (threshold < size && index < NUM_BUCKETS - 1)
                {
                    threshold <<= 1;
                    ++index;
                }
                return index;
#endif
        }

        ALWAYS_INLINE
        void *get_cached_block(size_t size) noexcept
        {
            if (UNLIKELY(size < MIN_CACHE_BLOCK || size > MAX_CACHE_BLOCK))
                return nullptr;

            const size_t bucket_idx = get_bucket_index(size);
            if (UNLIKELY(bucket_idx >= NUM_BUCKETS))
                return nullptr;

            auto &bucket = buckets[bucket_idx];
            const size_t count = bucket.count.load(std::memory_order_acquire);

            // Prefetch the bucket entries
#if defined(__x86_64__) || defined(__aarch64__)
            prefetch(&bucket.entries[0]);
            if (count > 1) prefetch(&bucket.entries[1]);
#endif

            for (size_t i = 0; i < count; ++i)
            {
                auto &entry = bucket.entries[i];
                void *expected = entry.ptr.load(std::memory_order_relaxed);

                if (expected && entry.size >= size && entry.size <= size * MAX_SIZE_RATIO)
                {
                    if (entry.ptr.compare_exchange_strong(expected, nullptr,
                                                          std::memory_order_acquire, std::memory_order_relaxed))
                    {
                        bucket.count.fetch_sub(1, std::memory_order_release);
                        total_cached.fetch_sub(entry.size, std::memory_order_relaxed);
                        memory_fence();
                        return expected;
                    }
                }
            }
            return nullptr;
        }

        ALWAYS_INLINE bool cache_block(void *ptr, size_t size) noexcept
        {
            if (UNLIKELY(size < MIN_CACHE_BLOCK || size > MAX_CACHE_BLOCK))
                return false;

            const size_t bucket_idx = get_bucket_index(size);
            if (UNLIKELY(bucket_idx >= NUM_BUCKETS))
                return false;

            if (const size_t current_total = total_cached.load(std::memory_order_relaxed);
                current_total + size > MAX_CACHE_SIZE)
                return false;

            auto &[count2, entries] = buckets[bucket_idx];

            if (const size_t count = count2.load(std::memory_order_acquire); count < size_bucket::BUCKET_SIZE)
            {
                auto &entry = entries[count];

                if (void *expected = nullptr; entry.ptr.compare_exchange_strong(expected, ptr,
                    std::memory_order_release, std::memory_order_relaxed))
                {
                    entry.size = size;
                    entry.last_use = get_timestamp();
                    count2.fetch_add(1, std::memory_order_release);
                    total_cached.fetch_add(size, std::memory_order_relaxed);
                    return true;
                }
            }

            // ReSharper disable once CppDFAUnreadVariable
            auto oldest_time = UINT64_MAX;
            size_t oldest_idx = 0;

#if defined(__x86_64__) || defined(__aarch64__)
#if defined(__x86_64__) && defined(__AVX2__)
                    __m256i oldest = _mm256_set1_epi64x(UINT64_MAX);
                    __m256i indices = _mm256_setr_epi64x(0, 1, 2, 3);

                    for (size_t i = 0; i < BUCKET_SIZE; i += 4)
                    {
                        __m256i times = _mm256_setr_epi64x(
                            bucket.entries[i].last_use,
                            bucket.entries[i+1].last_use,
                            bucket.entries[i+2].last_use,
                            bucket.entries[i+3].last_use
                        );
                        __m256i mask = _mm256_cmpgt_epi64(oldest, times);
                        oldest = _mm256_blendv_epi8(oldest, times, mask);
                        indices = _mm256_blendv_epi8(indices,
                                 _mm256_setr_epi64x(i, i+1, i+2, i+3), mask);
                    }

                    uint64_t tmp_times[4], tmp_indices[4];
                    _mm256_storeu_si256((__m256i*)tmp_times, oldest);
                    _mm256_storeu_si256((__m256i*)tmp_indices, indices);

                    for (int i = 0; i < 4; i++)
                    {
                        if (tmp_times[i] < oldest_time)
                        {
                            oldest_time = tmp_times[i];
                            oldest_idx = tmp_indices[i];
                        }
                    }
#elif defined(__aarch64__)
            uint64x2_t oldest = vdupq_n_u64(UINT64_MAX);
            uint64x2_t idx = vdupq_n_u64(0);
            idx = vsetq_lane_u64(0, idx, 0);

            VECTORIZE_LOOP
            for (size_t i = 0; i < size_bucket::BUCKET_SIZE; i += 2)
            {
                uint64x2_t times = vld1q_u64(&entries[i].last_use);
                uint64x2_t curr_idx = vdupq_n_u64(0);
                curr_idx = vsetq_lane_u64(i, curr_idx, 0);
                uint64x2_t mask = vcltq_u64(times, oldest);
                oldest = vbslq_u64(mask, times, oldest);
                idx = vbslq_u64(mask, curr_idx, idx);
            }

            uint64_t tmp_times[2], tmp_indices[2];
            vst1q_u64(tmp_times, oldest);
            vst1q_u64(tmp_indices, idx);

            if (tmp_times[0] < tmp_times[1])
            {
                // ReSharper disable once CppDFAUnusedValue
                oldest_time = tmp_times[0];
                oldest_idx = tmp_indices[0];
            } else
            {
                // ReSharper disable once CppDFAUnusedValue
                oldest_time = tmp_times[1];
                oldest_idx = tmp_indices[1];
            }
#endif
#else
                VECTORIZE_LOOP
                for (size_t i = 0; i < BUCKET_SIZE; ++i)
                {
                    if (bucket.entries[i].last_use < oldest_time)
                    {
                        oldest_time = bucket.entries[i].last_use;
                        oldest_idx = i;
                    }
                }
#endif

            auto &oldest2 = entries[oldest_idx];
            void *expected = oldest2.ptr.load(std::memory_order_relaxed);

            if (expected && size <= oldest2.size * MAX_SIZE_RATIO)
            {
                if (oldest2.ptr.compare_exchange_strong(expected, ptr,
                                                        std::memory_order_release, std::memory_order_relaxed))
                {
                    total_cached.fetch_sub(oldest2.size, std::memory_order_relaxed);
                    oldest2.size = size;
                    oldest2.last_use = get_timestamp();
                    total_cached.fetch_add(size, std::memory_order_relaxed);
                    return true;
                }
            }

            return false;
        }

        ALWAYS_INLINE
        void clear() noexcept
        {
            for (auto &bucket: buckets)
            {
                const size_t count = bucket.count.load(std::memory_order_acquire);
                for (size_t i = 0; i < count; ++i)
                {
                    auto &[ptr1, size, last_use] = bucket.entries[i];
                    if (void *ptr = ptr1.exchange(nullptr, std::memory_order_release))
                    {
                        const size_t total_size = size + sizeof(block_header);
                        const size_t alloc_size = total_size <= PG_SIZE
                                                      ? PG_SIZE
                                                      : (total_size + PG_SIZE - 1) & ~(PG_SIZE - 1);

                        UNMAP_MEMORY(static_cast<char*>(ptr) - sizeof(block_header),
                                     alloc_size);
                    }
                }
                bucket.count.store(0, std::memory_order_release);
            }
            total_cached.store(0, std::memory_order_release);
        }
    };

    static thread_local thread_cache_t thread_cache_;
    static thread_local pool_manager pool_manager_;
    static thread_local large_block_cache_t large_block_cache_;
    static thread_local std::array<tiny_block_manager::tiny_pool *,
        TINY_CLASSES> tiny_pools_;

    ALWAYS_INLINE
    static void* allocate_tiny(const size_t size) noexcept
    {
        if (UNLIKELY(size == 0))
            return nullptr;

        const uint8_t size_class = (size - 1) >> 3;
        if (UNLIKELY(size_class >= TINY_CLASSES))
            return nullptr;

        if (auto *tiny_pool = tiny_pools_[size_class])
        {
            void *ptr = nullptr;
            ptr = tiny_pool->allocate_tiny(size_class);

            if (LIKELY(ptr != nullptr))
            {
                if (UNLIKELY(!is_base_aligned(ptr)))
                    return nullptr;

                auto *header = new(ptr) block_header();
                header->init(size, size_class, false);

                void *user_ptr = static_cast<char *>(ptr) + sizeof(block_header);
                if (UNLIKELY(!is_base_aligned(user_ptr)))
                    return nullptr;

                return user_ptr;
            }
        }

        static std::mutex init_mutex;
        std::lock_guard lock(init_mutex);

        if (!tiny_pools_[size_class])
        {
            try
            {
                tiny_pools_[size_class] = new(std::align_val_t{PG_SIZE})
                        tiny_block_manager::tiny_pool();
                return allocate_tiny(size);
            } catch (...)
            {
                return nullptr;
            }
        }

        return nullptr;
    }

    ALWAYS_INLINE
    static void* allocate_small(const size_t size) noexcept
    {
        const uint8_t size_class = (size - 1) >> 3;

        if (void* cached = thread_cache_.get(size_class))
        {
            auto* header = reinterpret_cast<block_header*>(
                static_cast<char*>(cached) - sizeof(block_header));

            if (LIKELY(header->is_valid()))
            {
                header->set_free(false);
                return cached;
            }
            return nullptr;
        }

        if (void* ptr = pool_manager_.allocate(size_class); LIKELY(ptr))
        {
            auto* header = new (ptr) block_header();
            header->encode(size, size_class, false);
            return static_cast<char*>(ptr) + sizeof(block_header);
        }

        return nullptr;
    }

    ALWAYS_INLINE
    static void* allocate_medium(const size_t size, const uint8_t size_class) noexcept
    {
        if (void* cached = thread_cache_.get(size_class))
        {
            auto* header = reinterpret_cast<block_header*>(
                static_cast<char*>(cached) - sizeof(block_header));
            header->set_free(false);
            return cached;
        }

        if (void* ptr = pool_manager_.allocate(size_class); LIKELY(ptr))
        {
            auto* header = new (ptr) block_header();
            header->encode(size, size_class, false);
            return static_cast<char*>(ptr) + sizeof(block_header);
        }

        return nullptr;
    }
    ALWAYS_INLINE
    static void* allocate_large(const size_t size) noexcept
    {
        if (void* cached = large_block_cache_.get_cached_block(size))
        {
            auto* header = reinterpret_cast<block_header*>(
                static_cast<char*>(cached) - sizeof(block_header));
            header->set_free(false);
            return cached;
        }

        constexpr size_t header_size = (sizeof(block_header) + CACHE_LINE_SIZE - 1)
                                       & ~(CACHE_LINE_SIZE - 1);

        if (size <= PG_SIZE - header_size)
        {
            void *ptr = MAP_MEMORY(PG_SIZE);
            if (LIKELY(ptr))
            {
                auto *header = new(ptr) block_header();
                header->init(size, 255, false);
                header->set_memory_mapped(true);
                return static_cast<char *>(ptr) + header_size;
            }
            return nullptr;
        }

        const size_t total_pages = (size + header_size + PG_SIZE - 1) >> 12;
        const size_t allocation_size = total_pages << 12;

        void *ptr = MAP_MEMORY(allocation_size);
        if (UNLIKELY(!ptr))
            return nullptr;

        auto* header = new (ptr) block_header();
        header->init(size, 255, false);
        header->set_memory_mapped(true);
        return static_cast<char *>(ptr) + header_size;
    }

    ALWAYS_INLINE
    static void thread_cleanup()
    {
        cleanup();
    }

    ALWAYS_INLINE
    static void register_thread_cleanup()
    {
        thread_local struct Cleanup
        {
            ~Cleanup()
            {
                cleanup();
            }
        } cleanup;
    }

public:
    ALWAYS_INLINE
    static void* allocate(const size_t size) noexcept
    {
        register_thread_cleanup();
        if (UNLIKELY(size == 0 || size > (1ULL << 47)))
            return nullptr;

        if (LIKELY(size <= TINY_LARGE_THRESHOLD))
        {
            const uint8_t size_class = (size - 1) >> 3;
            if (UNLIKELY(size_class >= TINY_CLASSES))
                return nullptr;

            if (auto *tiny_pool = tiny_pools_[size_class])
            {
                if (void *ptr = tiny_pool->allocate_tiny(size_class))
                {
                    auto *header = new(ptr) block_header();
                    header->init(size, size_class, false);
                    return static_cast<char *>(ptr) + sizeof(block_header);
                }
            }

            static std::mutex init_mutex;
            std::lock_guard lock(init_mutex);
            if (!tiny_pools_[size_class])
            {
                try
                {
                    tiny_pools_[size_class] = new(std::align_val_t{PG_SIZE})
                            tiny_block_manager::tiny_pool();
                    return allocate(size);
                } catch (...)
                {
                    return nullptr;
                }
            }
            return nullptr;
        }

        if (LIKELY(size >= PG_SIZE))
            return allocate_large(size);

        if (size <= SMALL_LARGE_THRESHOLD)
        {
            const uint8_t size_class = (size - 1) >> 3;
            if (void *cached = thread_cache_.get(size_class))
            {
                auto *header = reinterpret_cast<block_header *>(static_cast<char *>(cached) - sizeof(block_header));
                header->set_free(false);
                return cached;
            }
            return allocate_small(size);
        }

        const size_t size_class = 31 - __builtin_clz(size - 1);
        return allocate_medium(size, size_class);
    }

    ALWAYS_INLINE
    static void deallocate(void* ptr) noexcept
    {
        if (!ptr)
            return;
        if (UNLIKELY(!block_header::is_aligned(ptr)))
            return;

        auto* header = reinterpret_cast<block_header*>(
            static_cast<char*>(ptr) - sizeof(block_header));

        if (UNLIKELY(!header->is_valid()))
            return;

        if (UNLIKELY((reinterpret_cast<uintptr_t>(ptr) & ~(PG_SIZE-1)) == 0))
            return;

        const uint8_t size_class = header->size_class();
        if (UNLIKELY(size_class >= SIZE_CLASSES && size_class != 255))
            return;

        if (size_class < TINY_CLASSES)
        {
            if (header->is_free())
                return;

            if (auto* tiny_pool = tiny_pools_[size_class])
            {
                header->set_free(true);
                tiny_pool->deallocate_tiny(
                    static_cast<char*>(ptr) - sizeof(block_header),
                    size_class
                );
            }
            return;
        }

        if (UNLIKELY(size_class == 255))
        {
            if (large_block_cache_.cache_block(ptr, header->size()))
                return;
            void* block = static_cast<char*>(ptr) - sizeof(block_header);
            if (header->is_memory_mapped())
            {
                const size_t total_size = header->size() + sizeof(block_header);
                // Determine if we need one or more pages
                const size_t allocation_size = total_size <= PG_SIZE
                                                   ? PG_SIZE
                                                   : (total_size + PG_SIZE - 1) & ~(PG_SIZE - 1);
                UNMAP_MEMORY(block, allocation_size);
            } else
            {
                free(block);
            }
            return;
        }

        if (header->is_free())
            return;

        if (thread_cache_.put(ptr, size_class))
        {
            header->set_free(true);
            return;
        }

        header->set_free(true);
        if (header->try_coalesce())
        {
            auto* pool_start = reinterpret_cast<void*>(
                reinterpret_cast<uintptr_t>(header) & ~(PG_SIZE - 1));
            static_cast<pool*>(pool_start)->return_memory();
        }

        void* block = static_cast<char*>(ptr) - sizeof(block_header);
        pool_manager_.deallocate(block, size_class);
    }

    ALWAYS_INLINE NO_SANITIZE_ADDRESS
    static void* reallocate(void* ptr, const size_t new_size) noexcept
    {
        if (UNLIKELY(!ptr))
            return allocate(new_size);

        if (UNLIKELY(!block_header::is_aligned(ptr)))
            return nullptr;

        if (UNLIKELY(new_size == 0))
        {
            deallocate(ptr);
            return nullptr;
        }
        // cool thing I just learned
        // header is 100% not null if the ptr is aligned!
        const auto* header = reinterpret_cast<block_header*>(
            static_cast<char*>(ptr) - sizeof(block_header));

        if (UNLIKELY(!header))
            return nullptr;

        if (UNLIKELY(!header->is_valid()))
            return nullptr;

        if (UNLIKELY(header->size() > (1ULL << 47)))
            return nullptr;

        const size_t old_size = header->size();
        const uint8_t old_class = header->size_class();

#if defined(__clang__)
        HAVE_BUILTIN_ASSUME(old_class <= SIZE_CLASSES);
#endif

        if (old_class < TINY_CLASSES)
        {
            if (const size_t max_tiny_size = (old_class + 1) << 3; new_size <= max_tiny_size)
                return ptr;
        }
        else if (old_class < SIZE_CLASSES)
        {
            if (const size_t max_size = size_classes[old_class].size; new_size <= max_size)
                return ptr;
        }

        if (UNLIKELY(header->is_memory_mapped()))
        {
            #ifdef __linux__
            void* block = static_cast<char*>(ptr) - sizeof(block_header);
            const size_t new_total = new_size + sizeof(block_header);
            const size_t old_total = old_size + sizeof(block_header);
            void* new_block = mremap(block, old_total, new_total, MREMAP_MAYMOVE);
            if (new_block != MAP_FAILED)
            {
                auto* new_header = reinterpret_cast<block_header*>(new_block);
                new_header->encode(new_size, 255, false);
                new_header->set_memory_mapped(true);
                return static_cast<char*>(new_block) + sizeof(block_header);
            }
            #endif
        }

        void* new_ptr = allocate(new_size);
        if (UNLIKELY(!new_ptr))
            return nullptr;

        if (const size_t copy_size = old_size < new_size ? old_size : new_size; copy_size <= 32)
        {
            std::memcpy(new_ptr, ptr, copy_size);
        }
        else if (copy_size >= 4096)
        {
            prefetch_range(ptr, copy_size);
            prefetch_range(new_ptr, copy_size);

            const auto dst = static_cast<char*>(new_ptr);
            const auto src = static_cast<const char*>(ptr);

            #if defined(__AVX512F__)
                for (size_t i = 0; i < copy_size; i += 64)
                {
                    __m512i v = _mm512_loadu_si512((const __m512i*)(src + i));
                    _mm512_stream_si512((__m512i*)(dst + i), v);
                }
            #elif defined(__AVX2__)
                for (size_t i = 0; i < copy_size; i += 32)
                {
                    __m256i v = _mm256_loadu_si256((const __m256i*)(src + i));
                    _mm256_stream_si256((__m256i*)(dst + i), v);
                }
            #elif defined(__aarch64__)
                for (size_t i = 0; i < copy_size; i += 64)
                {
                    auto src_bytes = reinterpret_cast<const uint8_t*>(src + i);
                    auto dst_bytes = reinterpret_cast<uint8_t*>(dst + i);
                    uint8x16x4_t v = vld4q_u8(src_bytes);
                    vst4q_u8(dst_bytes, v);
                }
            #endif
            memory_fence();

            for (size_t i = 0; i < copy_size; i += 8)
                stream_store(dst + i,
                    *reinterpret_cast<const int64_t*>(src + i));
            memory_fence();
        }
        else
        {
            #if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
                #if __GLIBC_PREREQ(2, 14)
                    __memcpy_chk(new_ptr, ptr, copy_size, copy_size);
                #else
                    std::memcpy(new_ptr, ptr, copy_size);
                #endif
            #else
                std::memcpy(new_ptr, ptr, copy_size);
            #endif
        }

        deallocate(ptr);
        return new_ptr;
    }

    ALWAYS_INLINE NO_SANITIZE_ADDRESS
    static void* callocate(const size_t num, const size_t size) noexcept
    {
        if (UNLIKELY(num == 0 || size == 0))
            return nullptr;

        if (UNLIKELY(num > SIZE_MAX / size))
            return nullptr;

        size_t total_size = num * size;
        void* ptr = allocate(total_size);

        if (UNLIKELY(!ptr))
            return nullptr;

        if (total_size <= 32)
        {
            std::memset(ptr, 0, total_size);
            return ptr;
        }

        #ifdef __linux__
            if (total_size >= 4096)
            {
                char* page_aligned = reinterpret_cast<char*>(
                    (reinterpret_cast<uintptr_t>(ptr) + 4095) & ~4095ULL);
                size_t prefix = page_aligned - static_cast<char*>(ptr);
                size_t suffix = (total_size - prefix) & 4095;
                size_t middle = total_size - prefix - suffix;

                auto zero_range = [](void* dst, size_t size)
                {
                    #if defined(__AVX512F__)
                        auto* aligned_dst = reinterpret_cast<__m512i*>((reinterpret_cast<uintptr_t>(dst) + 63) & ~63ULL);
                        size_t pre = reinterpret_cast<char*>(aligned_dst) - static_cast<char*>(dst);
                        if (pre) std::memset(dst, 0, pre);

                        __m512i zero = _mm512_setzero_si512();
                        for (size_t i = 0; i < (size - pre) / 64; ++i)
                            _mm512_stream_si512(aligned_dst + i, zero);

                        size_t post = (size - pre) & 63;
                        if (post) std::memset(reinterpret_cast<char*>(aligned_dst + (size - pre) / 64), 0, post);

                    #elif defined(__AVX2__)
                        auto* aligned_dst = reinterpret_cast<__m256i*>((reinterpret_cast<uintptr_t>(dst) + 31) & ~31ULL);
                        size_t pre = reinterpret_cast<char*>(aligned_dst) - static_cast<char*>(dst);
                        if (pre) std::memset(dst, 0, pre);

                        __m256i zero = _mm256_setzero_si256();
                        for (size_t i = 0; i < (size - pre) / 32; ++i)
                            _mm256_stream_si256(aligned_dst + i, zero);

                        size_t post = (size - pre) & 31;
                        if (post) std::memset(reinterpret_cast<char*>(aligned_dst + (size - pre) / 32), 0, post);

                    #elif defined(__aarch64__)
                        auto* aligned_dst = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(dst) + 15) & ~15ULL);
                        size_t pre = aligned_dst - static_cast<uint8_t*>(dst);
                        if (pre) std::memset(dst, 0, pre);

                        uint8x16x4_t zero = { vdupq_n_u8(0), vdupq_n_u8(0), vdupq_n_u8(0), vdupq_n_u8(0) };
                        for (size_t i = 0; i < (size - pre) / 64; ++i)
                            vst4q_u8(aligned_dst + i * 64, zero);

                        size_t post = (size - pre) & 63;
                        if (post) std::memset(aligned_dst + ((size - pre) / 64) * 64, 0, post);
                    #else
                        std::memset(dst, 0, size);
                    #endif
                };

                if (prefix)
                    zero_range(ptr, prefix);

                if (middle)
                {
                    if (madvise(page_aligned, middle, MADV_DONTNEED) == 0)
                    {
                        if (suffix)
                            zero_range(page_aligned + middle, suffix);
                        memory_fence();
                        return ptr;
                    }
                }
            }
        #endif

        auto dst = static_cast<char*>(ptr);
        #if defined(__AVX512F__)
            __m512i zero = _mm512_setzero_si512();
            auto* aligned_dst = reinterpret_cast<__m512i*>((reinterpret_cast<uintptr_t>(dst) + 63) & ~63ULL);
            size_t pre = reinterpret_cast<char*>(aligned_dst) - dst;
            if (pre) std::memset(dst, 0, pre);

            size_t blocks = (total_size - pre) / 64;
            for (size_t i = 0; i < blocks; ++i)
                _mm512_stream_si512(aligned_dst + i, zero);

            size_t remain = (total_size - pre) & 63;
            if (remain)
                std::memset(reinterpret_cast<char*>(aligned_dst + blocks), 0, remain);

        #elif defined(__AVX2__)
            __m256i zero = _mm256_setzero_si256();
            auto* aligned_dst = reinterpret_cast<__m256i*>((reinterpret_cast<uintptr_t>(dst) + 31) & ~31ULL);
            size_t pre = reinterpret_cast<char*>(aligned_dst) - dst;
            if (pre) std::memset(dst, 0, pre);

            size_t blocks = (total_size - pre) / 32;
            for (size_t i = 0; i < blocks; ++i)
                _mm256_stream_si256(aligned_dst + i, zero);

            size_t remain = (total_size - pre) & 31;
            if (remain)
                std::memset(reinterpret_cast<char*>(aligned_dst + blocks), 0, remain);

        #elif defined(__aarch64__)
            uint8x16x4_t zero = { vdupq_n_u8(0), vdupq_n_u8(0), vdupq_n_u8(0), vdupq_n_u8(0) };
            auto* dst_bytes = reinterpret_cast<uint8_t*>(dst);
            auto* aligned_dst = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(dst_bytes) + 15) & ~15ULL);
            size_t pre = aligned_dst - dst_bytes;
            if (pre) std::memset(dst_bytes, 0, pre);

            size_t blocks = (total_size - pre) / 64;
            for (size_t i = 0; i < blocks; ++i)
                vst4q_u8(aligned_dst + i * 64, zero);

        if (size_t remain = (total_size - pre) & 63)
                std::memset(aligned_dst + blocks * 64, 0, remain);

        #else
            if (const size_t align = reinterpret_cast<uintptr_t>(dst) & 7)
            {
                std::memset(dst, 0, align);
                dst += align;
                total_size -= align;
            }

            const size_t blocks = total_size >> 3;
            for (size_t i = 0; i < blocks; ++i)
                stream_store(dst + (i << 3), 0LL);

            if (const size_t remain = total_size & 7)
                std::memset(dst + (blocks << 3), 0, remain);
        #endif

        memory_fence();
        return ptr;
    }

    ALWAYS_INLINE
    static void cleanup() noexcept
    {
        large_block_cache_.clear();
        thread_cache_.clear();

        for (auto*& pool : tiny_pools_)
        {
            if (pool)
            {
                operator delete(pool, std::align_val_t{PG_SIZE});
                pool = nullptr;
            }
        }
    }
};

// Initialization
thread_local thread_cache_t Jallocator::thread_cache_{};
thread_local Jallocator::pool_manager Jallocator::pool_manager_{};
thread_local Jallocator::large_block_cache_t Jallocator::large_block_cache_{};
thread_local std::array<Jallocator::tiny_block_manager::tiny_pool *,
    TINY_CLASSES>
Jallocator::tiny_pools_{};

// C API
#ifndef __cplusplus
{
 extern  "C"
{

}
    ALWAYS_INLINE
    void* malloc(const size_t size)
    {
        return Jallocator::allocate(size);
    }

    ALWAYS_INLINE
    void free(void* ptr)
    {
        Jallocator::deallocate(ptr);
    }

    ALWAYS_INLINE
    void* realloc(void* ptr, const size_t new_size)
    {
        return Jallocator::reallocate(ptr, new_size);
    }

    ALWAYS_INLINE
    void* calloc(const size_t num, const size_t size)
    {
        return Jallocator::callocate(num, size);
    }

    ALWAYS_INLINE
    void cleanup()
    {
        Jallocator::cleanup();
    }
}
#endif

// inline void* operator new(size_t __sz)
// {
//     return Jallocator::allocate(__sz);
// }
//
// inline void* operator new[](size_t __sz)
// {
//     return Jallocator::allocate(__sz);
// }
//
// inline void operator delete(void* __p) noexcept
// {
//     Jallocator::deallocate(__p);
// }
//
// inline void operator delete[](void* __p) noexcept
// {
//     Jallocator::deallocate(__p);
// }

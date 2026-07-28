#ifndef DEMON_C_APP_H
#define DEMON_C_APP_H

#include <stdint.h>

static inline uint64_t demon_syscall0(uint64_t number) {
    register uint64_t rax __asm__("rax") = number;
    __asm__ volatile("int $0x80" : "+a"(rax) : : "memory", "cc");
    return rax;
}

static inline uint64_t demon_syscall1(uint64_t number, uint64_t first) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = first;
    __asm__ volatile("int $0x80" : "+a"(rax) : "D"(rdi) : "memory", "cc");
    return rax;
}

static inline uint64_t demon_syscall2(uint64_t number, uint64_t first,
                                      uint64_t second) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = first;
    register uint64_t rsi __asm__("rsi") = second;
    __asm__ volatile("int $0x80" : "+a"(rax) : "D"(rdi), "S"(rsi)
                     : "memory", "cc");
    return rax;
}

static inline uint64_t demon_syscall3(uint64_t number, uint64_t first,
                                      uint64_t second, uint64_t third) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = first;
    register uint64_t rsi __asm__("rsi") = second;
    register uint64_t rdx __asm__("rdx") = third;
    __asm__ volatile("int $0x80" : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx) : "memory", "cc");
    return rax;
}

static inline uint64_t demon_syscall4(uint64_t number, uint64_t first,
                                      uint64_t second, uint64_t third,
                                      uint64_t fourth) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = first;
    register uint64_t rsi __asm__("rsi") = second;
    register uint64_t rdx __asm__("rdx") = third;
    register uint64_t r10 __asm__("r10") = fourth;
    __asm__ volatile("int $0x80" : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10)
                     : "memory", "cc");
    return rax;
}

static inline uint64_t demon_write(const void *bytes, uint64_t length) {
    return demon_syscall2(0u, (uint64_t)(uintptr_t)bytes, length);
}

static inline uint64_t demon_yield(void) { return demon_syscall0(1u); }
static inline uint64_t demon_exit(uint64_t status) {
    return demon_syscall1(2u, status);
}
static inline uint64_t demon_ticks(void) { return demon_syscall0(3u); }
static inline uint64_t demon_service_open(uint64_t service) {
    return demon_syscall1(5u, service);
}
static inline uint64_t demon_handle_close(uint64_t handle) {
    return demon_syscall1(8u, handle);
}
static inline uint64_t demon_input_poll(uint64_t handle, void *event) {
    return demon_syscall2(20u, handle, (uint64_t)(uintptr_t)event);
}
static inline uint64_t demon_getpid(void) { return demon_syscall0(4u); }
static inline uint64_t demon_channel_create(const char *name, uint64_t length) {
    return demon_syscall2(14u, (uint64_t)(uintptr_t)name, length);
}
static inline uint64_t demon_channel_connect(const char *name, uint64_t length) {
    return demon_syscall2(15u, (uint64_t)(uintptr_t)name, length);
}
static inline uint64_t demon_channel_send(uint64_t channel, const void *message,
                                          uint64_t length) {
    return demon_syscall3(16u, channel, (uint64_t)(uintptr_t)message, length);
}
static inline uint64_t demon_channel_receive(uint64_t channel, void *message,
                                             uint64_t capacity, uint64_t nonblocking) {
    return demon_syscall4(17u, channel, (uint64_t)(uintptr_t)message,
                          capacity, nonblocking);
}
static inline uint64_t demon_surface_create(uint64_t factory, uint64_t width,
                                            uint64_t height) {
    return demon_syscall3(22u, factory, width, height);
}
static inline uint64_t demon_surface_write(uint64_t surface, const void *pixels,
                                           uint64_t count, uint64_t offset) {
    return demon_syscall4(23u, surface, (uint64_t)(uintptr_t)pixels, count, offset);
}
static inline uint64_t demon_surface_share(uint64_t surface, uint64_t channel) {
    return demon_syscall2(24u, surface, channel);
}
static inline uint64_t demon_surface_damage(uint64_t surface, uint64_t x,
                                            uint64_t y, uint64_t width,
                                            uint64_t height) {
    register uint64_t rax __asm__("rax") = 28u;
    register uint64_t rdi __asm__("rdi") = surface;
    register uint64_t rsi __asm__("rsi") = x;
    register uint64_t rdx __asm__("rdx") = y;
    register uint64_t r10 __asm__("r10") = width;
    register uint64_t r8 __asm__("r8") = height;
    __asm__ volatile("int $0x80" : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10), "r"(r8)
                     : "memory", "cc");
    return rax;
}

#endif

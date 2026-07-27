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

#endif

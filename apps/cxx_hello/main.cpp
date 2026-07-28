// Wave 0 proof-of-concept for the EDE port: a real C++ MAKO-ABI app --
// heap allocation via `new`, a class with a virtual destructor (exercises
// vtable emission + __cxa_pure_virtual linkage), and console output via the
// same demon_write() every native C app already uses. Once this boots and
// prints correctly, the toolchain path (g++ -> cxx_runtime.o -> nostdlib
// link -> MAKO-ABI ELF) is proven end to end, and later waves can build
// actual EDE components on top of it instead of re-deriving this from
// scratch per component.
#include <demon/c_app.h>
#include <demon/cxx_runtime.h>

namespace {

class Greeter {
   public:
    explicit Greeter(uint64_t pid) : pid_(pid) {}
    virtual ~Greeter() = default;
    virtual void Announce() const {
        demon_write("CXX_RUNTIME_HELLO pid=", 22u);
        // demon_write() only takes raw bytes, no formatting helper exists
        // in demon/c_app.h yet -- print the PID digit-by-digit rather than
        // pull in a formatting dependency this proof-of-concept doesn't
        // need.
        char digits[21];
        int index = 21;
        uint64_t value = pid_;
        do {
            digits[--index] = static_cast<char>('0' + value % 10u);
            value /= 10u;
        } while (value != 0u);
        demon_write(&digits[index], static_cast<uint64_t>(21 - index));
        demon_write("\n", 1u);
    }

   private:
    uint64_t pid_;
};

}  // namespace

extern "C" uint64_t cxx_hello_main(void) {
    const uint64_t pid = demon_getpid();
    // Heap-allocate through operator new (cxx_runtime.cpp's bump arena) and
    // call through a vtable -- both were unusable before Wave 0.
    Greeter *greeter = new Greeter(pid);
    greeter->Announce();
    demon_write("CXX_RUNTIME_ARENA_REMAINING=", 29u);
    char digits[21];
    int index = 21;
    uint64_t remaining = cxx_runtime_arena_remaining();
    do {
        digits[--index] = static_cast<char>('0' + remaining % 10u);
        remaining /= 10u;
    } while (remaining != 0u);
    demon_write(&digits[index], static_cast<uint64_t>(21 - index));
    demon_write("\n", 1u);
    delete greeter;
    demon_write("CXX_RUNTIME_OK\n", 16u);
    return 0u;
}

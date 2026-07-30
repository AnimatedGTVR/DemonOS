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
#include <vector>
#include <string>

namespace {

void write_u64(uint64_t value) {
    char digits[21];
    int index = 21;
    do {
        digits[--index] = static_cast<char>('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    demon_write(&digits[index], static_cast<uint64_t>(21 - index));
}

class Greeter {
   public:
    explicit Greeter(uint64_t pid) : pid_(pid) {}
    virtual ~Greeter() = default;
    virtual void Announce() const {
        demon_write("CXX_RUNTIME_HELLO pid=", 22u);
        write_u64(pid_);
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
    write_u64(cxx_runtime_arena_remaining());
    demon_write("\n", 1u);
    delete greeter;
    demon_write("CXX_RUNTIME_OK\n", 16u);

    // Real delete/free (Wave 0 revisit, at the user's request to firm up
    // C++ support before attempting a real port of ede-panel's C++
    // source): allocate and free a Greeter again to prove the arena
    // actually reclaims the block rather than just growing forever. A
    // mismatch here is a real regression, not a cosmetic detail, so it's
    // an exit-code assertion (see kernel.c's scheduler_reap check on this
    // process) exactly like every other MAKO-ABI self-test in this
    // codebase, not just a printed line nothing checks.
    const uint64_t before_reuse = cxx_runtime_arena_remaining();
    Greeter *throwaway = new Greeter(pid);
    delete throwaway;
    const uint64_t after_reuse = cxx_runtime_arena_remaining();
    if (before_reuse != after_reuse) return 121u;
    demon_write("CXX_RUNTIME_FREE_REUSE_OK\n", 27u);

    // std::vector<int> and std::string (this project's own freestanding
    // shims -- include/vector, include/string -- real libstdc++'s headers
    // don't build under -ffreestanding -fno-exceptions -nostdlib).
    std::vector<int> numbers;
    for (int i = 1; i <= 5; ++i) numbers.push_back(i * i);
    int sum = 0;
    for (size_t i = 0; i < numbers.size(); ++i) sum += numbers[i];
    if (sum != 55) return 122u;  // 1+4+9+16+25
    demon_write("CXX_STL_VECTOR_SUM=", 20u);
    write_u64(static_cast<uint64_t>(sum));
    demon_write("\n", 1u);

    std::string text = "Equinox";
    text += " Desktop";
    text += " DemonOS";
    text += " Environment";
    if (text != "Equinox Desktop DemonOS Environment") return 123u;
    demon_write("CXX_STL_STRING_LENGTH=", 23u);
    write_u64(text.size());
    demon_write("\n", 1u);
    demon_write("CXX_STL_STRING_TEXT=", 21u);
    demon_write(text.c_str(), text.size());
    demon_write("\n", 1u);
    demon_write("CXX_STL_OK\n", 11u);

    return 0u;
}

#include <patch_runtime/MemUtils.h>
#include <windows.h>
#include <xlog/xlog.h>
#include <cstring>

void write_mem(unsigned addr, const void* data, unsigned size)
{
    DWORD old_protect;

    if (!VirtualProtect(reinterpret_cast<void*>(addr), size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        xlog::warn("VirtualProtect failed: addr {:x} size {:x} error {}", addr, size, GetLastError());
    }
    std::memcpy(reinterpret_cast<void*>(addr), data, size);
    VirtualProtect(reinterpret_cast<void*>(addr), size, old_protect, nullptr);
}

void unprotect_mem(void* ptr, unsigned len)
{
    DWORD old_protect;
    if (!VirtualProtect(ptr, len, PAGE_EXECUTE_READWRITE, &old_protect)) {
        xlog::warn("VirtualProtect failed: addr {} size {:x} error {}", ptr, len, GetLastError());
    }
}

extern "C" size_t subhook_disasm(void *src, int32_t *reloc_op_offset);

// Required by the vendored subhook (a Ridgeline-local modification adds a
// callback at the unknown-opcode site so an injecting DLL can decide whether
// to abort or just log). We log + continue: subhook will fall back to its
// default behavior of refusing to install the hook in question.
extern "C" void subhook_unk_opcode_handler(uint8_t* opcode)
{
    xlog::warn("subhook: unknown opcode 0x{:02X} at {}", *opcode, static_cast<void*>(opcode));
}

size_t get_instruction_len(void* ptr)
{
    return subhook_disasm(ptr, nullptr);
}

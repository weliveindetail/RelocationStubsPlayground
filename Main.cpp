#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <tuple>
#include <vector>

static_assert(sizeof(void*) == 8, "64bit mode only");

void ErrorExit(const char* pszMsg)
{
    printf("Error! %s with error code of %ld.\n", pszMsg, GetLastError());
    __debugbreak();
    exit(0);
}

extern "C" void func()
{
    printf("func called correctly\n");
}

using stubFunc_t = void (*)();

std::tuple<stubFunc_t, uint32_t> createStubFunction_AbsoluteJmpRax(uint8_t* const mem, uint32_t offset)
{
    uint8_t* stubAddr = mem + offset;

   #if !NDEBUG
    *(mem+offset) = 0xCC; // int3
    offset++;
   #endif

    *(mem+offset+ 0) = 0x48; // mov
    *(mem+offset+ 1) = 0xb8; // rax
    // placeholder for 64-bit target address

    *(mem+offset+10) = 0xff; // jmp
    *(mem+offset+11) = 0xe0; // rax

    return std::make_tuple(reinterpret_cast<stubFunc_t>(stubAddr), offset + 2);
}

std::tuple<stubFunc_t, uint32_t> createStubFunction_IpRelativeJmp(uint8_t* const mem, uint32_t offset)
{
    uint8_t* stubAddr = mem + offset;

   #if !NDEBUG
    *(mem+offset) = 0xCC; // int3
    offset++;
   #endif

    *(mem+offset+ 0) = 0xff; // jump *0(%rip)
    *(mem+offset+ 1) = 0x25;
    *(mem+offset+ 2) = 0;
    *(mem+offset+ 3) = 0;
    *(mem+offset+ 4) = 0;
    *(mem+offset+ 5) = 0;
    // placeholder for 64-bit target address

    return std::make_tuple(reinterpret_cast<stubFunc_t>(stubAddr), offset + 6);
}

void applyRelocation(uint64_t value, uint8_t* baseAddress, uint32_t offset)
{
    baseAddress += offset;
    *(baseAddress+0) = (uint8_t)((value & 0x00000000000000FF) >>  0);
    *(baseAddress+1) = (uint8_t)((value & 0x000000000000FF00) >>  8);
    *(baseAddress+2) = (uint8_t)((value & 0x0000000000FF0000) >> 16);
    *(baseAddress+3) = (uint8_t)((value & 0x00000000FF000000) >> 24);
    *(baseAddress+4) = (uint8_t)((value & 0x000000FF00000000) >> 32);
    *(baseAddress+5) = (uint8_t)((value & 0x0000FF0000000000) >> 40);
    *(baseAddress+6) = (uint8_t)((value & 0x00FF000000000000) >> 48);
    *(baseAddress+7) = (uint8_t)((value & 0xFF00000000000000) >> 56);

    return;
}

int main()
{
    SYSTEM_INFO sSysInfo;
    GetSystemInfo(&sSysInfo);

    // Reserve one page in the virtual address space
    LPVOID lpvBase = VirtualAlloc(
        nullptr,                    // System selects address
        sSysInfo.dwPageSize,        // Size of allocation
        MEM_COMMIT | MEM_RESERVE,   // Allocate and commit reserved pages
        PAGE_EXECUTE);              // Protection = executable

    if (!lpvBase)
        ErrorExit("VirtualAlloc reserve failed");

    // Switch pages to read/write mode
    DWORD oldProtectMode;
    if (!VirtualProtect(lpvBase, sSysInfo.dwPageSize, PAGE_READWRITE, &oldProtectMode))
        ErrorExit("VirtualProtect failed");

    std::vector<stubFunc_t> stubs(2);
    std::vector<uint32_t> offsets(2);
    {
        // Construct stub functions
        uint8_t* const mem = reinterpret_cast<uint8_t* const>(lpvBase);

        std::tie(stubs[0], offsets[0]) = createStubFunction_AbsoluteJmpRax(mem, 0);
        std::tie(stubs[1], offsets[1]) = createStubFunction_IpRelativeJmp(mem, 16);

        // Resolve relocations
        uint64_t finalFuncAddr = reinterpret_cast<uint64_t>(&func);
        for (int i = 0; i < 2; i++)
        {
            applyRelocation(finalFuncAddr, mem, offsets[i]);
        }
    }

    // Switch pages back to executable mode
    DWORD dummyProtectMode;
    if (!VirtualProtect(lpvBase, sSysInfo.dwPageSize, oldProtectMode, &dummyProtectMode))
        ErrorExit("VirtualProtect failed");

    // Test relocations
    for (int i = 0; i < 2; i++)
    {
        stubs[i]();
        printf("func returned correctly\n");
    }

    // Release the block of pages when you are finished using them.
    if (!VirtualFree(lpvBase, sSysInfo.dwPageSize, MEM_DECOMMIT))
        ErrorExit("VirtualFree failed");

    // Print all text and keep terminal open
    fflush(stdout);
    getchar();
    return 0;
}

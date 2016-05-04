#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

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

    // Construct stub function
    uint8_t* const mem = reinterpret_cast<uint8_t* const>(lpvBase);
    {
        #ifdef NDEBUG
          *mem = 0x90; // nop
        #else
          *mem = 0xCC; // int3 (debug break)
        #endif

        *(mem+ 1) = 0x48; // mov
        *(mem+ 2) = 0xb8; // rax
        
        // 64-bit address that would be replaced during relocation
        // resolution by the effective absolute address of "func"
        uint64_t funcAddr = reinterpret_cast<uint64_t>(&func);

        *(mem+ 3) = (uint8_t)((funcAddr & 0x00000000000000FF) >>  0);
        *(mem+ 4) = (uint8_t)((funcAddr & 0x000000000000FF00) >>  8);
        *(mem+ 5) = (uint8_t)((funcAddr & 0x0000000000FF0000) >> 16);
        *(mem+ 6) = (uint8_t)((funcAddr & 0x00000000FF000000) >> 24);
        *(mem+ 7) = (uint8_t)((funcAddr & 0x000000FF00000000) >> 32);
        *(mem+ 8) = (uint8_t)((funcAddr & 0x0000FF0000000000) >> 40);
        *(mem+ 9) = (uint8_t)((funcAddr & 0x00FF000000000000) >> 48);
        *(mem+10) = (uint8_t)((funcAddr & 0xFF00000000000000) >> 56);

        *(mem+11) = 0xff; // jmp
        *(mem+12) = 0xe0; // rax
    }

    // Switch pages back to executable mode
    DWORD dummyProtectMode;
    if (!VirtualProtect(lpvBase, sSysInfo.dwPageSize, oldProtectMode, &dummyProtectMode))
        ErrorExit("VirtualProtect failed");
    
    // Cast and call stub function
    using stubFunc_t = void (*)();
    auto stubFunc = reinterpret_cast<stubFunc_t>(mem);

    stubFunc();
    printf("func returned correctly\n");

    // Release the block of pages when you are finished using them.
    if (!VirtualFree(lpvBase, sSysInfo.dwPageSize, MEM_DECOMMIT))
        ErrorExit("VirtualFree failed");

    // Print all text and keep terminal open
    fflush(stdout);
    getchar();
    return 0;
}

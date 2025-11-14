#include <Windows.h>
#include <stdio.h>
#include <TlHelp32.h>

#define debug

#ifdef debug
#define log(x, ...) printf(x, ##__VA_ARGS__)
#define error(x) printf("[Error] %s", x)
#endif

typedef struct {
    char* dllPath;
    DWORD pid;
} inject;

/*
pipe name: mond
listening exit code: mond
*/

HANDLE initPipeLine() {

    HANDLE pipe = CreateNamedPipeA(
        "\\\\.\\pipe\\mond",
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        255,
        0,
        0,
        NULL
    );

    if (pipe == INVALID_HANDLE_VALUE) {
        error("Couldnt make pipe");
        return 0;
    }

    return pipe;
}

BOOL pipeWaitForClient(HANDLE pipe) {
    if (pipe == INVALID_HANDLE_VALUE) {
        error("invalid pipe");
        return 0;
    }

    BOOL connected = ConnectNamedPipe(pipe, NULL);
    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
        error("ConnectNamedPipe failed");
        return 0;
    }

    log("[+] Client connected to pipe\n");

    while (1) {
        DWORD bytesAvailable = 0;

        if (!PeekNamedPipe(pipe, NULL, 0, NULL, &bytesAvailable, NULL)) {
            Sleep(10);
            continue;
        }

        if (bytesAvailable == 0) {
            Sleep(10);
            continue;
        }

        char buf[256];
        DWORD read;
        if (ReadFile(pipe, buf, sizeof(buf) - 1, &read, NULL)) {
            buf[read] = '\0';
            log("[+] DLL: \"%s\"\n", buf);

            if (strcmp(buf, "mond") == 0) {
                log("[+] Exit code received\n");
                return 1;
            }
        }
        else {
            error("ReadFile failed");
            return 0;
        }
    }

    return 1;
}

DWORD enumProcess(const char* target) {
    wchar_t* wTarget;

    if (!target) {
        error("invalid target name");
        return 0;
    }

    int required = MultiByteToWideChar(CP_UTF8, 0, target, -1, NULL, 0);
    if (required == 0) return 0;

    wTarget = malloc(required * sizeof(wchar_t));
    if (!wTarget) return 0;

    if (!MultiByteToWideChar(CP_UTF8, 0, target, -1, wTarget, required)) {
        free(wTarget);
        return 0;
    }

    HANDLE snap;
    PROCESSENTRY32W procEntry;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);
    procEntry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snap, &procEntry)) {
        while (Process32NextW(snap, &procEntry)) {
            if (wcscmp(wTarget, procEntry.szExeFile) == 0) {
                CloseHandle(snap);
                log("[+] Found target process %d\n", procEntry.th32ProcessID);
                return procEntry.th32ProcessID;
            }
        }
    }

    return 0;
}

BOOL checkDllsize(const char* dllpath) {
    if (!dllpath) {
        error("Please provide a dll path");
        return 0;
    }
    HANDLE file = CreateFileA(dllpath, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        error("Please provide a valid dll file");
        return 0;
    }
    CloseHandle(file);
    return 1;
}

BOOL injectProcess(inject info) {

    HANDLE targetProcess;
    DWORD target = info.pid;
    const char* dllPath = info.dllPath;

    targetProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, target);
    if (targetProcess == INVALID_HANDLE_VALUE) {
        error("couldnt open process");
        return 0;
    }

    log("[!] Injecting process \n");

    BOOL valid = checkDllsize(dllPath);
    if (!valid) {
        CloseHandle(targetProcess);
        return 0;
    }

    SIZE_T dllSize = strlen(dllPath) + 1;

    LPVOID allocated = VirtualAllocEx(
        targetProcess,
        NULL,
        dllSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE
    );

    if (!allocated) {
        error("Couldnt allocate memory");
        CloseHandle(targetProcess);
        return 0;
    }

    log("[+] Allocated memory %p\n", allocated);

    if (!WriteProcessMemory(targetProcess, allocated, dllPath, strlen(dllPath), &(SIZE_T){0})) {
        error("Couldnt Write memory");
        CloseHandle(targetProcess);
        return 0;
    }

    log("[+] Wrote to memory %p\n", allocated);
    log("[!] Creating Thread \n");

    if (CreateRemoteThreadEx(targetProcess, NULL, 0, (LPTHREAD_START_ROUTINE)LoadLibraryA, allocated, 0, NULL, NULL) == INVALID_HANDLE_VALUE) {
        error("Couldnt Make thread");
        CloseHandle(targetProcess);
        return 0;
    }

    log("[+] Created Thread , Executing .. \n");

    return 1;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        error("Please provide <proc> <dll>");
        return 0;
    }

    HANDLE pipe = initPipeLine();
    if (!pipe) return 0;

    inject info;
    info.pid = enumProcess(argv[1]);
    info.dllPath = argv[2];

    HANDLE pipeThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)pipeWaitForClient, pipe, 0, NULL);
    HANDLE injectorThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)injectProcess, &info, 0, NULL);

    HANDLE threads[2] = { pipeThread, injectorThread };

    WaitForMultipleObjects(2, threads, TRUE, INFINITE);

    return 1;
}

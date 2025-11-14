#include <Windows.h>

HANDLE initPipe() {
    HANDLE pipe;

    pipe = CreateFileA(
        "\\\\.\\pipe\\mond",
        GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (pipe == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    return pipe;
}

void messagePipe(HANDLE pipe, const char* message) {
    BOOL r;
    DWORD bytes;

    if (pipe == INVALID_HANDLE_VALUE || message == NULL) return;
    if (strlen(message) > 255) return;

    r = WriteFile(
        pipe,
        message,
        strlen(message),
        &bytes,
        NULL
    );

    if (r == FALSE) return;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {

    switch (reason) {

    case DLL_PROCESS_ATTACH:
        HANDLE pipe = initPipe();
        messagePipe(pipe, "Hello from dll");
        messagePipe(pipe, "mond");


        CloseHandle(pipe);
        break;

        return TRUE;
    }
}

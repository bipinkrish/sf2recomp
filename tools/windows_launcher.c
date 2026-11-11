// Windows entry point for the portable converter. Paths and arguments stay UTF-16.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

static const DWORD stream_ids[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};

static int fail(const wchar_t *message, BOOL cli) {
    HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    if (cli) {
        if (error && error != INVALID_HANDLE_VALUE) {
            char utf8[512];
            DWORD written;
            int length = WideCharToMultiByte(CP_UTF8, 0, message, -1, utf8, sizeof utf8, NULL, NULL);
            if (length) WriteFile(error, utf8, length - 1, &written, NULL);
        }
    } else {
        MessageBoxW(NULL, message, L"SF2Recomp", MB_OK | MB_ICONERROR);
    }
    return 1;
}

static HANDLE inherit_stream(DWORD id, DWORD access) {
    HANDLE stream = GetStdHandle(id);
    HANDLE duplicate;
    HANDLE self = GetCurrentProcess();
    if (stream && stream != INVALID_HANDLE_VALUE && GetFileType(stream) != FILE_TYPE_UNKNOWN &&
        DuplicateHandle(self, stream, self, &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS))
        return duplicate;
    SECURITY_ATTRIBUTES security = {.nLength = sizeof security, .bInheritHandle = TRUE};
    return CreateFileW(L"NUL", access, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                       OPEN_EXISTING, 0, NULL);
}

static int launch(BOOL cli, const wchar_t *arguments) {
    HANDLE streams[3];
    for (int i = 0; i < 3; i++) streams[i] = GetStdHandle(stream_ids[i]);
    BOOL attached = cli && AttachConsole(ATTACH_PARENT_PROCESS);
    // AttachConsole can replace redirected handles. Keep the caller's pipes and files.
    for (int i = 0; i < 3; i++) {
        if (streams[i] && streams[i] != INVALID_HANDLE_VALUE && GetFileType(streams[i]) != FILE_TYPE_UNKNOWN)
            SetStdHandle(stream_ids[i], streams[i]);
    }

    wchar_t root[32768];
    DWORD length = GetModuleFileNameW(NULL, root, ARRAYSIZE(root));
    if (!length || length >= ARRAYSIZE(root))
        return fail(L"Cannot locate the SF2Recomp folder.\r\n", cli);
    wchar_t *separator = wcsrchr(root, L'\\');
    if (!separator) return fail(L"Cannot locate the SF2Recomp folder.\r\n", cli);
    *separator = 0;

    wchar_t python[32768], command[32768];
    int count = swprintf(python, ARRAYSIZE(python), L"%ls\\python\\python.exe", root);
    if (count < 0 || count >= ARRAYSIZE(python))
        return fail(L"The SF2Recomp path is too long.\r\n", cli);
    count = swprintf(command, ARRAYSIZE(command),
                     L"\"%ls\" -I -u -X utf8 \"%ls\\tools\\release_entry.py\" %ls", python, root, arguments);
    if (count < 0 || count >= ARRAYSIZE(command))
        return fail(L"The SF2Recomp command line is too long.\r\n", cli);

    STARTUPINFOW startup = {.cb = sizeof startup};
    if (cli) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = inherit_stream(STD_INPUT_HANDLE, GENERIC_READ);
        startup.hStdOutput = inherit_stream(STD_OUTPUT_HANDLE, GENERIC_WRITE);
        startup.hStdError = inherit_stream(STD_ERROR_HANDLE, GENERIC_WRITE);
    }
    PROCESS_INFORMATION process;
    BOOL started = CreateProcessW(python, command, NULL, NULL, cli, attached ? 0 : CREATE_NO_WINDOW,
                                  NULL, NULL, &startup, &process);
    if (cli) {
        CloseHandle(startup.hStdInput);
        CloseHandle(startup.hStdOutput);
        CloseHandle(startup.hStdError);
    }
    if (!started)
        return fail(L"Cannot start the bundled Python. Extract the entire SF2Recomp archive.\r\n", cli);
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    if (!cli && code) return fail(L"Cannot open the converter window.\r\n", FALSE);
    return (int)code;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments, int show) {
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    BOOL cli = argc > 1;
    for (int i = 1; i < argc; i++)
        if (!wcscmp(argv[i], L"--gui")) cli = FALSE;
    LocalFree(argv);
    return launch(cli, arguments);
}

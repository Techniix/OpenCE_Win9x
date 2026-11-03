/*
IMPORT_CHECK.C

Which of a program's imports this Windows cannot give it: every DLL the
program names, loaded, and every function it imports from each, looked up,
as Windows' loader would when it starts the program (which says only that
one is missing, not which). Built without a C runtime, as win98_probe.c is.

	import_check.exe [program]

The program is halo.exe beside this one if none is named. The report is
import_check.txt beside this one, and a message box says how many are
missing.
*/

#include <windows.h>

#define REPORT_NAME "import_check.txt"

static HANDLE report;
static char line[1024];

static void report_line(void)
{
	DWORD written;

	WriteFile(report, line, (DWORD)lstrlenA(line), &written, NULL);
	WriteFile(report, "\r\n", 2, &written, NULL);
}

/* this program's folder, with its separator */
static void own_folder(char *path, DWORD size)
{
	DWORD length = GetModuleFileNameA(NULL, path, size);

	while (length > 0 && path[length - 1] != '\\')
		length--;
	path[length] = '\0';
}

/* the command line's first argument after the program's, or NULL */
static char *first_argument(char *command_line)
{
	char *at = command_line;

	if (*at == '"')
	{
		at++;
		while (*at && *at != '"')
			at++;
		if (*at)
			at++;
	}
	else
	{
		while (*at && *at != ' ' && *at != '\t')
			at++;
	}
	while (*at == ' ' || *at == '\t')
		at++;
	if (!*at)
		return NULL;
	if (*at == '"')
	{
		char *end;

		at++;
		for (end = at; *end && *end != '"'; end++)
			;
		*end = '\0';
	}
	return at;
}

static BYTE *read_whole(const char *path, DWORD *size)
{
	HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	BYTE *data;
	DWORD read = 0;

	if (file == INVALID_HANDLE_VALUE)
		return NULL;
	*size = GetFileSize(file, NULL);
	data = (BYTE *)HeapAlloc(GetProcessHeap(), 0, *size ? *size : 1);
	if (data && (!ReadFile(file, data, *size, &read, NULL) || read != *size))
		data = NULL;
	CloseHandle(file);
	return data;
}

/* a relative address's place in the file, or NULL */
static BYTE *at_rva(BYTE *image, DWORD image_size, IMAGE_NT_HEADERS32 *headers, DWORD rva)
{
	IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(headers);
	WORD index;

	for (index = 0; index < headers->FileHeader.NumberOfSections; index++, section++)
	{
		DWORD extent = section->Misc.VirtualSize > section->SizeOfRawData ? section->Misc.VirtualSize :
			section->SizeOfRawData;

		if (rva >= section->VirtualAddress && rva < section->VirtualAddress + extent)
		{
			DWORD offset = rva - section->VirtualAddress + section->PointerToRawData;

			return offset < image_size ? image + offset : NULL;
		}
	}
	return NULL;
}

static int check(const char *program, int *missing_total, int *checked_total)
{
	DWORD size;
	BYTE *image = read_whole(program, &size);
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
	IMAGE_NT_HEADERS32 *headers;
	IMAGE_IMPORT_DESCRIPTOR *descriptor;
	OSVERSIONINFOA version;

	version.dwOSVersionInfoSize = sizeof(version);
	GetVersionExA(&version);
	wsprintfA(line, "Windows %lu.%lu.%lu %s", version.dwMajorVersion, version.dwMinorVersion,
		version.dwBuildNumber & 0xFFFF, version.szCSDVersion);
	report_line();
	wsprintfA(line, "program: %s", program);
	report_line();
	if (!image || size < sizeof(*dos) || dos->e_magic != IMAGE_DOS_SIGNATURE ||
		(DWORD)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS32) > size)
	{
		lstrcpyA(line, "cannot read the program, or it is not a Windows program");
		report_line();
		return 0;
	}
	headers = (IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
	if (headers->Signature != IMAGE_NT_SIGNATURE ||
		headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
	{
		lstrcpyA(line, "not a 32-bit Windows program");
		report_line();
		return 0;
	}
	wsprintfA(line, "needs Windows %u.%u (subsystem %u.%u)", headers->OptionalHeader.MajorOperatingSystemVersion,
		headers->OptionalHeader.MinorOperatingSystemVersion, headers->OptionalHeader.MajorSubsystemVersion,
		headers->OptionalHeader.MinorSubsystemVersion);
	report_line();
	descriptor = (IMAGE_IMPORT_DESCRIPTOR *)at_rva(image, size, headers,
		headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
	for (; descriptor && descriptor->Name; descriptor++)
	{
		const char *dll = (const char *)at_rva(image, size, headers, descriptor->Name);
		DWORD thunks = descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk : descriptor->FirstThunk;
		IMAGE_THUNK_DATA32 *thunk = (IMAGE_THUNK_DATA32 *)at_rva(image, size, headers, thunks);
		HMODULE module;
		int missing = 0, checked = 0;

		if (!dll)
			continue;
		module = LoadLibraryA(dll);
		if (!module)
		{
			wsprintfA(line, "%s: MISSING (cannot load it, error %lu)", dll, GetLastError());
			report_line();
		}
		for (; thunk && thunk->u1.AddressOfData; thunk++)
		{
			char ordinal_name[32];
			const char *name;

			if (thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG32)
			{
				wsprintfA(ordinal_name, "#%lu", thunk->u1.Ordinal & 0xFFFF);
				name = ordinal_name;
			}
			else
			{
				IMAGE_IMPORT_BY_NAME *by_name = (IMAGE_IMPORT_BY_NAME *)at_rva(image, size, headers,
					thunk->u1.AddressOfData);

				if (!by_name)
					continue;
				name = (const char *)by_name->Name;
			}
			checked++;
			if (!module || !GetProcAddress(module, name[0] == '#' ? (LPCSTR)(thunk->u1.Ordinal & 0xFFFF) : name))
			{
				wsprintfA(line, "%s: missing %s", dll, name);
				report_line();
				missing++;
			}
		}
		wsprintfA(line, "%s: %d of %d found", dll, checked - missing, checked);
		report_line();
		*missing_total += missing;
		*checked_total += checked;
	}
	return 1;
}

void WINAPI check_entry(void)
{
	char folder[MAX_PATH], program[MAX_PATH], report_path[MAX_PATH], message[256];
	char *argument = first_argument(GetCommandLineA());
	int missing = 0, checked = 0;

	own_folder(folder, MAX_PATH);
	if (argument)
		lstrcpynA(program, argument, MAX_PATH);
	else
	{
		lstrcpyA(program, folder);
		lstrcatA(program, "halo.exe");
	}
	lstrcpyA(report_path, folder);
	lstrcatA(report_path, REPORT_NAME);
	report = CreateFileA(report_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (check(program, &missing, &checked))
	{
		wsprintfA(line, "%d of %d imports missing", missing, checked);
		report_line();
		wsprintfA(message, "%d of %d imports missing.\n\nThe list is in %s.", missing, checked, report_path);
	}
	else
	{
		wsprintfA(message, "Cannot check %s.\n\nThe report is in %s.", program, report_path);
	}
	CloseHandle(report);
	MessageBoxA(NULL, message, "OpenCE import check", missing ? MB_ICONWARNING : MB_ICONINFORMATION);
	ExitProcess(missing ? 1 : 0);
}

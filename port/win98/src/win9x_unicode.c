/*
WIN9X_UNICODE.C

The Windows functions the platform layer calls that Windows 95, 98 and Me
have in name only (their wide-character functions, which fail, and NT's
security, which a FAT disk has no use for) or not at all (Windows 2000's and
XP's), defined here under the same names as win9x_compat.c's are. On
Windows NT each calls the system's own, found by name; on Windows 9x the
wide-character ones call the ANSI ones, with names in the system's code page
(a name it cannot write fails, as it would on the disk), and the security
ones do nothing and succeed: everything on Windows 9x is its one user's.
*/

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <aclapi.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "../include/win9x_compat.h"

static int windows_nt(void)
{
	static int nt = -1;

	if (nt < 0)
		nt = (GetVersion() & 0x80000000) == 0;
	return nt;
}

/* the system's own, by name (the import this file's definition replaces) */
static FARPROC system_function(const char *module, const char *name)
{
	HMODULE library = GetModuleHandleA(module);

	if (!library)
		library = LoadLibraryA(module);
	return library ? GetProcAddress(library, name) : NULL;
}

#define SYSTEM_FUNCTION(module, type, name) \
	static type system_##name; \
	if (!system_##name) \
		system_##name = (type)system_function(module, #name)

/* ---------- names: wide to the code page and back */

enum
{
	NAME_SIZE = MAX_PATH * 2
};

/* (NULL to NULL; false if it does not fit or the code page lacks a
character) */
static int ansi_name(const wchar_t *wide, char *ansi, char **result)
{
	BOOL lossy = FALSE;

	if (!wide)
	{
		*result = NULL;
		return 1;
	}
	if (!WideCharToMultiByte(CP_ACP, 0, wide, -1, ansi, NAME_SIZE, NULL, &lossy) || lossy)
	{
		SetLastError(ERROR_INVALID_NAME);
		return 0;
	}
	*result = ansi;
	return 1;
}

static void wide_name(const char *ansi, wchar_t *wide, int size)
{
	if (!MultiByteToWideChar(CP_ACP, 0, ansi, -1, wide, size) && size > 0)
		wide[0] = L'\0';
}

/* ---------- files */

HANDLE WINAPI CreateFileW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation,
	DWORD flags, HANDLE template_file)
{
	typedef HANDLE (WINAPI *proc)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
	char buffer[NAME_SIZE], *ansi;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, CreateFileW);
		return system_CreateFileW(path, access, share, security, creation, flags, template_file);
	}
	if (!ansi_name(path, buffer, &ansi))
		return INVALID_HANDLE_VALUE;
	/* (no access lists to read or write, and no descriptors to give) */
	access &= ~(WRITE_DAC | WRITE_OWNER | READ_CONTROL | ACCESS_SYSTEM_SECURITY);
	return CreateFileA(ansi, access, share, NULL, creation, flags, template_file);
}

BOOL WINAPI CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES security)
{
	typedef BOOL (WINAPI *proc)(LPCWSTR, LPSECURITY_ATTRIBUTES);
	char buffer[NAME_SIZE], *ansi;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, CreateDirectoryW);
		return system_CreateDirectoryW(path, security);
	}
	return ansi_name(path, buffer, &ansi) && CreateDirectoryA(ansi, NULL);
}

BOOL WINAPI RemoveDirectoryW(LPCWSTR path)
{
	typedef BOOL (WINAPI *proc)(LPCWSTR);
	char buffer[NAME_SIZE], *ansi;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, RemoveDirectoryW);
		return system_RemoveDirectoryW(path);
	}
	return ansi_name(path, buffer, &ansi) && RemoveDirectoryA(ansi);
}

BOOL WINAPI DeleteFileW(LPCWSTR path)
{
	typedef BOOL (WINAPI *proc)(LPCWSTR);
	char buffer[NAME_SIZE], *ansi;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, DeleteFileW);
		return system_DeleteFileW(path);
	}
	return ansi_name(path, buffer, &ansi) && DeleteFileA(ansi);
}

DWORD WINAPI GetFileAttributesW(LPCWSTR path)
{
	typedef DWORD (WINAPI *proc)(LPCWSTR);
	char buffer[NAME_SIZE], *ansi;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, GetFileAttributesW);
		return system_GetFileAttributesW(path);
	}
	return ansi_name(path, buffer, &ansi) ? GetFileAttributesA(ansi) : INVALID_FILE_ATTRIBUTES;
}

/* (Windows 95's kernel32.dll has no GetFileAttributesExA either: what
FindFirstFileA says of the file) */
static BOOL file_attributes_ansi(const char *path, WIN32_FILE_ATTRIBUTE_DATA *data)
{
	typedef BOOL (WINAPI *proc)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
	SYSTEM_FUNCTION("kernel32.dll", proc, GetFileAttributesExA);
	WIN32_FIND_DATAA found;
	HANDLE search;

	if (system_GetFileAttributesExA)
		return system_GetFileAttributesExA(path, GetFileExInfoStandard, data);
	if (strpbrk(path, "*?"))
	{
		SetLastError(ERROR_INVALID_NAME);
		return FALSE;
	}
	search = FindFirstFileA(path, &found);
	if (search == INVALID_HANDLE_VALUE)
		return FALSE;
	FindClose(search);
	data->dwFileAttributes = found.dwFileAttributes;
	data->ftCreationTime = found.ftCreationTime;
	data->ftLastAccessTime = found.ftLastAccessTime;
	data->ftLastWriteTime = found.ftLastWriteTime;
	data->nFileSizeHigh = found.nFileSizeHigh;
	data->nFileSizeLow = found.nFileSizeLow;
	return TRUE;
}

BOOL WINAPI GetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID information)
{
	if (level != GetFileExInfoStandard)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	return file_attributes_ansi(path, (WIN32_FILE_ATTRIBUTE_DATA *)information);
}

BOOL WINAPI GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID information)
{
	typedef BOOL (WINAPI *proc)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
	char buffer[NAME_SIZE], *ansi;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, GetFileAttributesExW);
		return system_GetFileAttributesExW(path, level, information);
	}
	return ansi_name(path, buffer, &ansi) && GetFileAttributesExA(ansi, level, information);
}

/* (Windows 9x has MoveFileExA in name only: over a file, a delete first) */
static BOOL move_file_ansi(const char *from, const char *to, DWORD flags)
{
	if (flags & MOVEFILE_DELAY_UNTIL_REBOOT)
	{
		SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
		return FALSE;
	}
	if (MoveFileA(from, to))
		return TRUE;
	if ((flags & MOVEFILE_REPLACE_EXISTING) && GetFileAttributesA(to) != INVALID_FILE_ATTRIBUTES)
	{
		if (!DeleteFileA(to))
			return FALSE;
		return MoveFileA(from, to);
	}
	return FALSE;
}

BOOL WINAPI MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags)
{
	typedef BOOL (WINAPI *proc)(LPCSTR, LPCSTR, DWORD);

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, MoveFileExA);
		return system_MoveFileExA(from, to, flags);
	}
	return move_file_ansi(from, to, flags);
}

BOOL WINAPI MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags)
{
	typedef BOOL (WINAPI *proc)(LPCWSTR, LPCWSTR, DWORD);
	char from_buffer[NAME_SIZE], to_buffer[NAME_SIZE], *from_ansi, *to_ansi;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, MoveFileExW);
		return system_MoveFileExW(from, to, flags);
	}
	return ansi_name(from, from_buffer, &from_ansi) && ansi_name(to, to_buffer, &to_ansi) &&
		move_file_ansi(from_ansi, to_ansi, flags);
}

static void find_data_wide(const WIN32_FIND_DATAA *ansi, WIN32_FIND_DATAW *wide)
{
	wide->dwFileAttributes = ansi->dwFileAttributes;
	wide->ftCreationTime = ansi->ftCreationTime;
	wide->ftLastAccessTime = ansi->ftLastAccessTime;
	wide->ftLastWriteTime = ansi->ftLastWriteTime;
	wide->nFileSizeHigh = ansi->nFileSizeHigh;
	wide->nFileSizeLow = ansi->nFileSizeLow;
	wide->dwReserved0 = ansi->dwReserved0;
	wide->dwReserved1 = ansi->dwReserved1;
	wide_name(ansi->cFileName, wide->cFileName, MAX_PATH);
	wide_name(ansi->cAlternateFileName, wide->cAlternateFileName, 14);
}

HANDLE WINAPI FindFirstFileW(LPCWSTR pattern, LPWIN32_FIND_DATAW data)
{
	typedef HANDLE (WINAPI *proc)(LPCWSTR, LPWIN32_FIND_DATAW);
	char buffer[NAME_SIZE], *ansi;
	WIN32_FIND_DATAA found;
	HANDLE search;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, FindFirstFileW);
		return system_FindFirstFileW(pattern, data);
	}
	if (!ansi_name(pattern, buffer, &ansi))
		return INVALID_HANDLE_VALUE;
	search = FindFirstFileA(ansi, &found);
	if (search != INVALID_HANDLE_VALUE)
		find_data_wide(&found, data);
	return search;
}

BOOL WINAPI FindNextFileW(HANDLE search, LPWIN32_FIND_DATAW data)
{
	typedef BOOL (WINAPI *proc)(HANDLE, LPWIN32_FIND_DATAW);
	WIN32_FIND_DATAA found;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, FindNextFileW);
		return system_FindNextFileW(search, data);
	}
	if (!FindNextFileA(search, &found))
		return FALSE;
	find_data_wide(&found, data);
	return TRUE;
}

FILE *__cdecl _wfopen(const wchar_t *path, const wchar_t *mode)
{
	typedef FILE *(__cdecl *proc)(const wchar_t *, const wchar_t *);
	char buffer[NAME_SIZE], *ansi;
	char mode_ansi[16];
	size_t index;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("msvcrt.dll", proc, _wfopen);
		return system__wfopen(path, mode);
	}
	if (!ansi_name(path, buffer, &ansi))
		return NULL;
	for (index = 0; index + 1 < sizeof(mode_ansi) && mode[index]; index++)
		mode_ansi[index] = (char)mode[index];
	mode_ansi[index] = '\0';
	return fopen(ansi, mode_ansi);
}

/* ---------- the process and its module */

DWORD WINAPI GetModuleFileNameW(HMODULE module, LPWSTR path, DWORD size)
{
	typedef DWORD (WINAPI *proc)(HMODULE, LPWSTR, DWORD);
	char ansi[NAME_SIZE];
	DWORD length;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, GetModuleFileNameW);
		return system_GetModuleFileNameW(module, path, size);
	}
	length = GetModuleFileNameA(module, ansi, sizeof(ansi));
	if (!length || !size)
		return 0;
	length = (DWORD)MultiByteToWideChar(CP_ACP, 0, ansi, (int)length, path, (int)size);
	if (!length)
	{
		/* (as Windows XP's: cut short, terminated, the size returned) */
		MultiByteToWideChar(CP_ACP, 0, ansi, (int)size - 1, path, (int)size);
		path[size - 1] = L'\0';
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return size;
	}
	if (length >= size)
	{
		path[size - 1] = L'\0';
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return size;
	}
	path[length] = L'\0';
	return length;
}

HMODULE WINAPI GetModuleHandleW(LPCWSTR name)
{
	typedef HMODULE (WINAPI *proc)(LPCWSTR);
	char buffer[NAME_SIZE], *ansi;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, GetModuleHandleW);
		return system_GetModuleHandleW(name);
	}
	return ansi_name(name, buffer, &ansi) ? GetModuleHandleA(ansi) : NULL;
}

LPWSTR WINAPI GetCommandLineW(void)
{
	typedef LPWSTR (WINAPI *proc)(void);
	static wchar_t *line;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, GetCommandLineW);
		return system_GetCommandLineW();
	}
	if (!line)
	{
		const char *ansi = GetCommandLineA();
		int size = MultiByteToWideChar(CP_ACP, 0, ansi, -1, NULL, 0);
		wchar_t *wide = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (size_t)(size > 0 ? size : 1) * sizeof(wchar_t));

		if (!wide)
			return (LPWSTR)L"";
		wide_name(ansi, wide, size > 0 ? size : 1);
		line = wide;
	}
	return line;
}

/* (the size with the terminator if it does not fit, its length without if
it does, 0 if there is none) */
DWORD WINAPI GetEnvironmentVariableW(LPCWSTR name, LPWSTR value, DWORD size)
{
	typedef DWORD (WINAPI *proc)(LPCWSTR, LPWSTR, DWORD);
	char buffer[NAME_SIZE], *ansi;
	char ansi_value[4096];
	DWORD length;
	int needed;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, GetEnvironmentVariableW);
		return system_GetEnvironmentVariableW(name, value, size);
	}
	if (!ansi_name(name, buffer, &ansi))
		return 0;
	length = GetEnvironmentVariableA(ansi, ansi_value, sizeof(ansi_value));
	if (!length || length >= sizeof(ansi_value))
		return 0;
	needed = MultiByteToWideChar(CP_ACP, 0, ansi_value, -1, NULL, 0);
	if (needed <= 0)
		return 0;
	if ((DWORD)needed > size)
		return (DWORD)needed;
	MultiByteToWideChar(CP_ACP, 0, ansi_value, -1, value, (int)size);
	return (DWORD)needed - 1;
}

BOOL WINAPI CreateProcessW(LPCWSTR application, LPWSTR command_line, LPSECURITY_ATTRIBUTES process_security,
	LPSECURITY_ATTRIBUTES thread_security, BOOL inherit, DWORD flags, LPVOID environment, LPCWSTR directory,
	LPSTARTUPINFOW startup, LPPROCESS_INFORMATION information)
{
	typedef BOOL (WINAPI *proc)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
		LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
	char application_buffer[NAME_SIZE], directory_buffer[NAME_SIZE], line[4096];
	char *application_ansi, *directory_ansi;
	STARTUPINFOA startup_ansi;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, CreateProcessW);
		return system_CreateProcessW(application, command_line, process_security, thread_security, inherit, flags,
			environment, directory, startup, information);
	}
	/* (a wide environment would need its own conversion: none is given) */
	if ((environment && (flags & CREATE_UNICODE_ENVIRONMENT)) || (startup && (startup->lpDesktop || startup->lpTitle)))
	{
		SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
		return FALSE;
	}
	if (!ansi_name(application, application_buffer, &application_ansi) ||
		!ansi_name(directory, directory_buffer, &directory_ansi))
	{
		return FALSE;
	}
	if (command_line && !WideCharToMultiByte(CP_ACP, 0, command_line, -1, line, sizeof(line), NULL, NULL))
		return FALSE;
	memset(&startup_ansi, 0, sizeof(startup_ansi));
	if (startup)
		memcpy(&startup_ansi, startup, sizeof(startup_ansi));
	startup_ansi.cb = sizeof(startup_ansi);
	return CreateProcessA(application_ansi, command_line ? line : NULL, NULL, NULL, inherit,
		flags & ~CREATE_UNICODE_ENVIRONMENT, environment, directory_ansi, &startup_ansi, information);
}

/* ---------- named objects */

HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES security, BOOL manual, BOOL initial, LPCWSTR name)
{
	typedef HANDLE (WINAPI *proc)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
	char buffer[NAME_SIZE], *ansi;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, CreateEventW);
		return system_CreateEventW(security, manual, initial, name);
	}
	return ansi_name(name, buffer, &ansi) ? CreateEventA(NULL, manual, initial, ansi) : NULL;
}

HANDLE WINAPI OpenEventW(DWORD access, BOOL inherit, LPCWSTR name)
{
	typedef HANDLE (WINAPI *proc)(DWORD, BOOL, LPCWSTR);
	char buffer[NAME_SIZE], *ansi;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, OpenEventW);
		return system_OpenEventW(access, inherit, name);
	}
	return ansi_name(name, buffer, &ansi) ? OpenEventA(access, inherit, ansi) : NULL;
}

HANDLE WINAPI CreateMutexW(LPSECURITY_ATTRIBUTES security, BOOL owned, LPCWSTR name)
{
	typedef HANDLE (WINAPI *proc)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
	char buffer[NAME_SIZE], *ansi;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("kernel32.dll", proc, CreateMutexW);
		return system_CreateMutexW(security, owned, name);
	}
	return ansi_name(name, buffer, &ansi) ? CreateMutexA(NULL, owned, ansi) : NULL;
}

/* ---------- messages (Windows 9x has MessageBoxW, but not everywhere
with a font for it: the code page's, as everything else here) */

int WINAPI MessageBoxW(HWND window, LPCWSTR text, LPCWSTR caption, UINT type)
{
	typedef int (WINAPI *proc)(HWND, LPCWSTR, LPCWSTR, UINT);
	char text_ansi[2048], caption_ansi[256];

	if (windows_nt())
	{
		SYSTEM_FUNCTION("user32.dll", proc, MessageBoxW);
		return system_MessageBoxW(window, text, caption, type);
	}
	if (!text || !WideCharToMultiByte(CP_ACP, 0, text, -1, text_ansi, sizeof(text_ansi), "?", NULL))
		text_ansi[0] = '\0';
	text_ansi[sizeof(text_ansi) - 1] = '\0';
	if (!caption || !WideCharToMultiByte(CP_ACP, 0, caption, -1, caption_ansi, sizeof(caption_ansi), "?", NULL))
		caption_ansi[0] = '\0';
	caption_ansi[sizeof(caption_ansi) - 1] = '\0';
	return MessageBoxA(window, text_ansi, caption ? caption_ansi : NULL, type);
}

/* ---------- threads */

BOOL WINAPI SwitchToThread(void)
{
	typedef BOOL (WINAPI *proc)(void);
	SYSTEM_FUNCTION("kernel32.dll", proc, SwitchToThread);

	if (system_SwitchToThread)
		return system_SwitchToThread();
	Sleep(0);
	return TRUE;
}

/* (Windows 9x has none: the lock taken, waiting if it must, which a try
allows its caller to see as having had it free; OpenCE's only trier,
RtlTryEnterCriticalSection, xbox_kernel.c, holds no other lock as it tries) */
BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION section)
{
	typedef BOOL (WINAPI *proc)(LPCRITICAL_SECTION);
	SYSTEM_FUNCTION("kernel32.dll", proc, TryEnterCriticalSection);

	if (system_TryEnterCriticalSection)
		return system_TryEnterCriticalSection(section);
	EnterCriticalSection(section);
	return TRUE;
}

BOOL WINAPI GetProcessTimes(HANDLE process, LPFILETIME created, LPFILETIME exited, LPFILETIME kernel,
	LPFILETIME user)
{
	typedef BOOL (WINAPI *proc)(HANDLE, LPFILETIME, LPFILETIME, LPFILETIME, LPFILETIME);
	SYSTEM_FUNCTION("kernel32.dll", proc, GetProcessTimes);

	if (windows_nt() && system_GetProcessTimes)
		return system_GetProcessTimes(process, created, exited, kernel, user);
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

/* ---------- security: on Windows 9x, a token of one user, everyone's
(S-1-1-0), whose access lists are made and given and change nothing */

static const SID_IDENTIFIER_AUTHORITY world_authority = { SECURITY_WORLD_SID_AUTHORITY };
static HANDLE const token_9x = (HANDLE)(LONG_PTR)0x9999;

BOOL WINAPI OpenProcessToken(HANDLE process, DWORD access, PHANDLE token)
{
	typedef BOOL (WINAPI *proc)(HANDLE, DWORD, PHANDLE);

	if (windows_nt())
	{
		SYSTEM_FUNCTION("advapi32.dll", proc, OpenProcessToken);
		return system_OpenProcessToken(process, access, token);
	}
	*token = token_9x;
	return TRUE;
}

BOOL WINAPI GetTokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS type, LPVOID information, DWORD size,
	PDWORD length)
{
	typedef BOOL (WINAPI *proc)(HANDLE, TOKEN_INFORMATION_CLASS, LPVOID, DWORD, PDWORD);
	struct
	{
		TOKEN_USER user;
		SID sid;
	} *user = (void *)information;

	if (windows_nt())
	{
		SYSTEM_FUNCTION("advapi32.dll", proc, GetTokenInformation);
		return system_GetTokenInformation(token, type, information, size, length);
	}
	if (token != token_9x || type != TokenUser)
	{
		SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
		return FALSE;
	}
	*length = sizeof(*user);
	if (size < sizeof(*user))
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}
	memset(user, 0, sizeof(*user));
	user->sid.Revision = SID_REVISION;
	user->sid.SubAuthorityCount = 1;
	user->sid.IdentifierAuthority = world_authority;
	user->sid.SubAuthority[0] = SECURITY_WORLD_RID;
	user->user.User.Sid = &user->sid;
	return TRUE;
}

BOOL WINAPI EqualSid(PSID first, PSID second)
{
	typedef BOOL (WINAPI *proc)(PSID, PSID);

	if (windows_nt())
	{
		SYSTEM_FUNCTION("advapi32.dll", proc, EqualSid);
		return system_EqualSid(first, second);
	}
	return first && second && !memcmp(first, second, sizeof(SID));
}

BOOL WINAPI InitializeAcl(PACL acl, DWORD size, DWORD revision)
{
	typedef BOOL (WINAPI *proc)(PACL, DWORD, DWORD);

	if (windows_nt())
	{
		SYSTEM_FUNCTION("advapi32.dll", proc, InitializeAcl);
		return system_InitializeAcl(acl, size, revision);
	}
	if (size < sizeof(ACL))
		return FALSE;
	memset(acl, 0, sizeof(ACL));
	acl->AclRevision = (BYTE)revision;
	acl->AclSize = (WORD)size;
	return TRUE;
}

BOOL WINAPI AddAccessAllowedAce(PACL acl, DWORD revision, DWORD access, PSID sid)
{
	typedef BOOL (WINAPI *proc)(PACL, DWORD, DWORD, PSID);

	if (windows_nt())
	{
		SYSTEM_FUNCTION("advapi32.dll", proc, AddAccessAllowedAce);
		return system_AddAccessAllowedAce(acl, revision, access, sid);
	}
	return TRUE;
}

BOOL WINAPI InitializeSecurityDescriptor(PSECURITY_DESCRIPTOR descriptor, DWORD revision)
{
	typedef BOOL (WINAPI *proc)(PSECURITY_DESCRIPTOR, DWORD);

	if (windows_nt())
	{
		SYSTEM_FUNCTION("advapi32.dll", proc, InitializeSecurityDescriptor);
		return system_InitializeSecurityDescriptor(descriptor, revision);
	}
	memset(descriptor, 0, sizeof(SECURITY_DESCRIPTOR));
	return TRUE;
}

BOOL WINAPI SetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR descriptor, BOOL present, PACL acl, BOOL defaulted)
{
	typedef BOOL (WINAPI *proc)(PSECURITY_DESCRIPTOR, BOOL, PACL, BOOL);

	if (windows_nt())
	{
		SYSTEM_FUNCTION("advapi32.dll", proc, SetSecurityDescriptorDacl);
		return system_SetSecurityDescriptorDacl(descriptor, present, acl, defaulted);
	}
	return TRUE;
}

/* (Windows 2000's: NT 4 has none, and goes without the protection) */
BOOL WINAPI SetSecurityDescriptorControl(PSECURITY_DESCRIPTOR descriptor, SECURITY_DESCRIPTOR_CONTROL mask,
	SECURITY_DESCRIPTOR_CONTROL bits)
{
	typedef BOOL (WINAPI *proc)(PSECURITY_DESCRIPTOR, SECURITY_DESCRIPTOR_CONTROL, SECURITY_DESCRIPTOR_CONTROL);
	SYSTEM_FUNCTION("advapi32.dll", proc, SetSecurityDescriptorControl);

	if (windows_nt() && system_SetSecurityDescriptorControl)
		return system_SetSecurityDescriptorControl(descriptor, mask, bits);
	return TRUE;
}

DWORD WINAPI SetNamedSecurityInfoW(LPWSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION information, PSID owner,
	PSID group, PACL dacl, PACL sacl)
{
	typedef DWORD (WINAPI *proc)(LPWSTR, SE_OBJECT_TYPE, SECURITY_INFORMATION, PSID, PSID, PACL, PACL);
	SYSTEM_FUNCTION("advapi32.dll", proc, SetNamedSecurityInfoW);

	if (windows_nt() && system_SetNamedSecurityInfoW)
		return system_SetNamedSecurityInfoW(name, type, information, owner, group, dacl, sacl);
	return ERROR_SUCCESS;
}

DWORD WINAPI SetSecurityInfo(HANDLE handle, SE_OBJECT_TYPE type, SECURITY_INFORMATION information, PSID owner,
	PSID group, PACL dacl, PACL sacl)
{
	typedef DWORD (WINAPI *proc)(HANDLE, SE_OBJECT_TYPE, SECURITY_INFORMATION, PSID, PSID, PACL, PACL);
	SYSTEM_FUNCTION("advapi32.dll", proc, SetSecurityInfo);

	if (windows_nt() && system_SetSecurityInfo)
		return system_SetSecurityInfo(handle, type, information, owner, group, dacl, sacl);
	return ERROR_SUCCESS;
}

/* ---------- network adapters (Windows 95 has no iphlpapi.dll; Windows 98
has GetBestInterface; GetAdaptersAddresses is Windows XP's) */

ULONG WINAPI GetAdaptersAddresses(ULONG family, ULONG flags, PVOID reserved, PIP_ADAPTER_ADDRESSES addresses,
	PULONG size)
{
	typedef ULONG (WINAPI *proc)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);
	SYSTEM_FUNCTION("iphlpapi.dll", proc, GetAdaptersAddresses);

	if (system_GetAdaptersAddresses)
		return system_GetAdaptersAddresses(family, flags, reserved, addresses, size);
	return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI GetBestInterface(IPAddr address, PDWORD index)
{
	typedef DWORD (WINAPI *proc)(IPAddr, PDWORD);
	SYSTEM_FUNCTION("iphlpapi.dll", proc, GetBestInterface);

	if (system_GetBestInterface)
		return system_GetBestInterface(address, index);
	return ERROR_NOT_SUPPORTED;
}

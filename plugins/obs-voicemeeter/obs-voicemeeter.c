#include <obs-module.h>
#include <stdio.h>

#include <media-io/audio-math.h>
#include <math.h>
/*
#include <libloaderapi.h>
#include <winreg.h>
#include <winerror.h>
*/
#include <windows.h>
#include "VoicemeeterRemote.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("rematrix-filter", "en-US")

#define blog(level, msg, ...) blog(level, "obs-voicemeeter: " msg, ##__VA_ARGS__)

#define MT_ obs_module_text

#ifndef MAX_AUDIO_SIZE
#ifndef AUDIO_OUTPUT_FRAMES
#define	AUDIO_OUTPUT_FRAMES 1024
#endif
#define	MAX_AUDIO_SIZE (AUDIO_OUTPUT_FRAMES * sizeof(float))
#endif // !MAX_AUDIO_SIZE

enum {
	voicemeeter_normal = 1,
	voicemeeter_banana = 2,
	voicemeeter_potato = 3,
};

static char uninstDirKey[] = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

#define INSTALLER_UNINST_KEY   "VB:Voicemeeter {17359A74-1236-5467}"

void RemoveNameInPath(char * szPath)
{
	long ll;
	ll = (long)strlen(szPath);
	while ((ll > 0) && (szPath[ll] != '\\')) ll--;
	if (szPath[ll] == '\\') szPath[ll] = 0;
}

#ifndef KEY_WOW64_32KEY
#define KEY_WOW64_32KEY 0x0200
#endif

BOOL __cdecl RegistryGetVoicemeeterFolderA(char * szDir)
{
	char szKey[256];
	char sss[1024];
	DWORD nnsize = 1024;
	HKEY hkResult;
	LONG rep;
	DWORD pptype = REG_SZ;
	sss[0] = 0;

	// build Voicemeeter uninstallation key
	strcpy(szKey, uninstDirKey);
	strcat(szKey, "\\");
	strcat(szKey, INSTALLER_UNINST_KEY);

	// open key
	rep = RegOpenKeyExA(HKEY_LOCAL_MACHINE, szKey, 0, KEY_READ, &hkResult);
	if (rep != ERROR_SUCCESS) {
		// if not present we consider running in 64bit mode and force to read 32bit registry
		rep = RegOpenKeyExA(HKEY_LOCAL_MACHINE, szKey, 0, KEY_READ | KEY_WOW64_32KEY, &hkResult);
	}

	if (rep != ERROR_SUCCESS) {
		blog(LOG_ERROR, "error %i reading registry", rep);
		return FALSE;
	}
	// read uninstall from program path
	rep = RegQueryValueExA(hkResult, "UninstallString", 0, &pptype, (unsigned char *)sss, &nnsize);
	RegCloseKey(hkResult);
	


	if (pptype != REG_SZ) {
		blog(LOG_INFO, "pptype %u, %u", pptype, REG_SZ);
		return FALSE;
	}
	if (rep != ERROR_SUCCESS) {
		blog(LOG_ERROR, "error %i getting value", rep);
		return FALSE;
	}
	// remove name to get the path only
	RemoveNameInPath(sss);
	if (nnsize > 512) nnsize = 512;
	strncpy(szDir, sss, nnsize);

	return TRUE;
}

static T_VBVMR_INTERFACE iVMR;
static HMODULE G_H_Module = NULL;
static int application = 0;

long InitializeDLLInterfaces(void)
{
	char szDllName[1024] = {0};
	memset(&iVMR, 0, sizeof(T_VBVMR_INTERFACE));

	if (RegistryGetVoicemeeterFolderA(szDllName) == FALSE) {
		// voicemeeter not installed?
		blog(LOG_INFO, "voicemeeter does not appear to be installed");
		return -100;
	}

	//use right dll w/ bitness
	if (sizeof(void*) == 8) strcat(szDllName, "\\VoicemeeterRemote64.dll");
	else strcat(szDllName, "\\VoicemeeterRemote.dll");

	// Load Dll
	G_H_Module = LoadLibraryA(szDllName);
	if (G_H_Module == NULL) {
		blog(LOG_INFO, ".dll failed to load");
		return -101;
	}

	// Get function pointers
	iVMR.VBVMR_Login = (T_VBVMR_Login)GetProcAddress(G_H_Module, "VBVMR_Login");
	iVMR.VBVMR_Logout = (T_VBVMR_Logout)GetProcAddress(G_H_Module, "VBVMR_Logout");
	iVMR.VBVMR_RunVoicemeeter = (T_VBVMR_RunVoicemeeter)GetProcAddress(G_H_Module, "VBVMR_RunVoicemeeter");
	iVMR.VBVMR_GetVoicemeeterType = (T_VBVMR_GetVoicemeeterType)GetProcAddress(G_H_Module, "VBVMR_GetVoicemeeterType");
	iVMR.VBVMR_GetVoicemeeterVersion = (T_VBVMR_GetVoicemeeterVersion)GetProcAddress(G_H_Module, "VBVMR_GetVoicemeeterVersion");

	iVMR.VBVMR_IsParametersDirty = (T_VBVMR_IsParametersDirty)GetProcAddress(G_H_Module, "VBVMR_IsParametersDirty");
	iVMR.VBVMR_GetParameterFloat = (T_VBVMR_GetParameterFloat)GetProcAddress(G_H_Module, "VBVMR_GetParameterFloat");
	iVMR.VBVMR_GetParameterStringA = (T_VBVMR_GetParameterStringA)GetProcAddress(G_H_Module, "VBVMR_GetParameterStringA");
	iVMR.VBVMR_GetParameterStringW = (T_VBVMR_GetParameterStringW)GetProcAddress(G_H_Module, "VBVMR_GetParameterStringW");
	iVMR.VBVMR_GetLevel = (T_VBVMR_GetLevel)GetProcAddress(G_H_Module, "VBVMR_GetLevel");
	iVMR.VBVMR_GetMidiMessage = (T_VBVMR_GetMidiMessage)GetProcAddress(G_H_Module, "VBVMR_GetMidiMessage");

	iVMR.VBVMR_SetParameterFloat = (T_VBVMR_SetParameterFloat)GetProcAddress(G_H_Module, "VBVMR_SetParameterFloat");
	iVMR.VBVMR_SetParameters = (T_VBVMR_SetParameters)GetProcAddress(G_H_Module, "VBVMR_SetParameters");
	iVMR.VBVMR_SetParametersW = (T_VBVMR_SetParametersW)GetProcAddress(G_H_Module, "VBVMR_SetParametersW");
	iVMR.VBVMR_SetParameterStringA = (T_VBVMR_SetParameterStringA)GetProcAddress(G_H_Module, "VBVMR_SetParameterStringA");
	iVMR.VBVMR_SetParameterStringW = (T_VBVMR_SetParameterStringW)GetProcAddress(G_H_Module, "VBVMR_SetParameterStringW");

	iVMR.VBVMR_Output_GetDeviceNumber = (T_VBVMR_Output_GetDeviceNumber)GetProcAddress(G_H_Module, "VBVMR_Output_GetDeviceNumber");
	iVMR.VBVMR_Output_GetDeviceDescA = (T_VBVMR_Output_GetDeviceDescA)GetProcAddress(G_H_Module, "VBVMR_Output_GetDeviceDescA");
	iVMR.VBVMR_Output_GetDeviceDescW = (T_VBVMR_Output_GetDeviceDescW)GetProcAddress(G_H_Module, "VBVMR_Output_GetDeviceDescW");
	iVMR.VBVMR_Input_GetDeviceNumber = (T_VBVMR_Input_GetDeviceNumber)GetProcAddress(G_H_Module, "VBVMR_Input_GetDeviceNumber");
	iVMR.VBVMR_Input_GetDeviceDescA = (T_VBVMR_Input_GetDeviceDescA)GetProcAddress(G_H_Module, "VBVMR_Input_GetDeviceDescA");
	iVMR.VBVMR_Input_GetDeviceDescW = (T_VBVMR_Input_GetDeviceDescW)GetProcAddress(G_H_Module, "VBVMR_Input_GetDeviceDescW");

	iVMR.VBVMR_AudioCallbackRegister = (T_VBVMR_AudioCallbackRegister)GetProcAddress(G_H_Module, "VBVMR_AudioCallbackRegister");
	iVMR.VBVMR_AudioCallbackStart = (T_VBVMR_AudioCallbackStart)GetProcAddress(G_H_Module, "VBVMR_AudioCallbackStart");
	iVMR.VBVMR_AudioCallbackStop = (T_VBVMR_AudioCallbackStop)GetProcAddress(G_H_Module, "VBVMR_AudioCallbackStop");
	iVMR.VBVMR_AudioCallbackUnregister = (T_VBVMR_AudioCallbackUnregister)GetProcAddress(G_H_Module, "VBVMR_AudioCallbackUnregister");

	// Check pointers are valid
	if (iVMR.VBVMR_Login == NULL) return -1;
	if (iVMR.VBVMR_Logout == NULL) return -2;
	if (iVMR.VBVMR_RunVoicemeeter == NULL) return -2;
	if (iVMR.VBVMR_GetVoicemeeterType == NULL) return -3;
	if (iVMR.VBVMR_GetVoicemeeterVersion == NULL) return -4;
	if (iVMR.VBVMR_IsParametersDirty == NULL) return -5;
	if (iVMR.VBVMR_GetParameterFloat == NULL) return -6;
	if (iVMR.VBVMR_GetParameterStringA == NULL) return -7;
	if (iVMR.VBVMR_GetParameterStringW == NULL) return -8;
	if (iVMR.VBVMR_GetLevel == NULL) return -9;
	if (iVMR.VBVMR_SetParameterFloat == NULL) return -10;
	if (iVMR.VBVMR_SetParameters == NULL) return -11;
	if (iVMR.VBVMR_SetParametersW == NULL) return -12;
	if (iVMR.VBVMR_SetParameterStringA == NULL) return -13;
	if (iVMR.VBVMR_SetParameterStringW == NULL) return -14;
	if (iVMR.VBVMR_GetMidiMessage == NULL) return -15;

	if (iVMR.VBVMR_Output_GetDeviceNumber == NULL) return -30;
	if (iVMR.VBVMR_Output_GetDeviceDescA == NULL) return -31;
	if (iVMR.VBVMR_Output_GetDeviceDescW == NULL) return -32;
	if (iVMR.VBVMR_Input_GetDeviceNumber == NULL) return -33;
	if (iVMR.VBVMR_Input_GetDeviceDescA == NULL) return -34;
	if (iVMR.VBVMR_Input_GetDeviceDescW == NULL) return -35;

	if (iVMR.VBVMR_AudioCallbackRegister == NULL) return -40;
	if (iVMR.VBVMR_AudioCallbackStart == NULL) return -41;
	if (iVMR.VBVMR_AudioCallbackStop == NULL) return -42;
	if (iVMR.VBVMR_AudioCallbackUnregister == NULL) return -43;

	return 0;
}


bool obs_module_load(void)
{
	int ret = InitializeDLLInterfaces();
	if (ret != 0) {
		blog(LOG_INFO, ".dll failed to be initalized");
		return false;
	}

	ret = iVMR.VBVMR_Login();
	switch (ret) {
	case 0:
		blog(LOG_INFO, "client logged in");
		break;
	case 1:
		blog(LOG_INFO, "attempting to open voicemeeter");
		ret = iVMR.VBVMR_RunVoicemeeter(voicemeeter_banana);
		if (ret == 0) {
			blog(LOG_INFO, "successfully opened banana");
			break;
		}
		ret = iVMR.VBVMR_RunVoicemeeter(voicemeeter_normal);
		if (ret == 0) {
			blog(LOG_INFO, "successfully opened basic");
			break;
		}
		blog(LOG_INFO, "failed to open voicemeeter");
		return false;
	case -1:
		blog(LOG_ERROR, "cannot get client");
		return false;
	case -2:
		blog(LOG_ERROR, "unexpected login");
		return false;
	}

	iVMR.VBVMR_GetVoicemeeterType(&application);
	switch (application) {
	case voicemeeter_banana:
		blog(LOG_INFO, "running voicemeeter banana");
		break;
	case voicemeeter_normal:
		blog(LOG_INFO, "running voicemeeter");
		break;
	}



	return true;
}

void obs_module_unload()
{
	blog(LOG_INFO, "client logging out");
	iVMR.VBVMR_Logout();
}
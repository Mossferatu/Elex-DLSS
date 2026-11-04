#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include "../src/indicator.h"
#include "../third_party/minhook/include/MinHook.h"
#define REQUIRE(x) do{if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void log_line(const char *format,...){va_list args;va_start(args,format);vprintf(format,args);va_end(args);putchar('\n');}
static const wchar_t key[]=L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore";

int wmain(int argc,wchar_t **argv){
    REQUIRE(argc==2);
    wchar_t ini[32768];REQUIRE(GetFullPathNameW(argv[1],32768,ini,NULL)>0);
    DWORD before=0,before_size=4,before_type=0;
    LSTATUS before_status=RegGetValueW(HKEY_LOCAL_MACHINE,key,L"ShowDlssIndicator",RRF_RT_REG_DWORD,&before_type,&before,&before_size);
    DWORD unrelated_size=0;
    LSTATUS unrelated=RegGetValueW(HKEY_LOCAL_MACHINE,key,L"ElexIndicatorTestMissingValue",RRF_RT_ANY,NULL,NULL,&unrelated_size);
    REQUIRE(WritePrivateProfileStringW(L"DLSS",L"Indicator",L"On",ini));
    REQUIRE(SUCCEEDED(elex_indicator_install(ini,log_line)));
    DWORD value=0,type=0,size=4;
    REQUIRE(RegGetValueW(HKEY_LOCAL_MACHINE,key,L"ShowDlssIndicator",RRF_RT_REG_DWORD,&type,&value,&size)==ERROR_SUCCESS);
    REQUIRE(value==1024&&size==4&&type==REG_DWORD);
    value=0;size=4;
    REQUIRE(RegGetValueA(HKEY_LOCAL_MACHINE,"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore","ShowDlssIndicator",RRF_RT_REG_DWORD,&type,&value,&size)==ERROR_SUCCESS);
    REQUIRE(value==1024);
    size=0;REQUIRE(RegGetValueW(HKEY_LOCAL_MACHINE,key,L"ShowDlssIndicator",RRF_RT_REG_DWORD,&type,NULL,&size)==ERROR_SUCCESS&&size==4);
    unsigned char small[2]={0x51,0x52};size=2;
    REQUIRE(RegGetValueW(HKEY_LOCAL_MACHINE,key,L"ShowDlssIndicator",RRF_RT_REG_DWORD,&type,small,&size)==ERROR_MORE_DATA&&size==4);
    REQUIRE(small[0]==0x51&&small[1]==0x52);
    HKEY opened=NULL;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,key,0,KEY_QUERY_VALUE,&opened)==ERROR_SUCCESS){
        size=4;value=0;REQUIRE(RegQueryValueExW(opened,L"ShowDlssIndicator",NULL,&type,(BYTE *)&value,&size)==ERROR_SUCCESS&&value==1024);
        size=4;value=0;REQUIRE(RegQueryValueExA(opened,"ShowDlssIndicator",NULL,&type,(BYTE *)&value,&size)==ERROR_SUCCESS&&value==1024);
        RegCloseKey(opened);
    }
    size=0;REQUIRE(RegGetValueW(HKEY_LOCAL_MACHINE,key,L"ElexIndicatorTestMissingValue",RRF_RT_ANY,NULL,NULL,&size)==unrelated);
    // An identically named value outside NGX must not become our virtual value.
    value=0;size=4;REQUIRE(RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE",L"ShowDlssIndicator",RRF_RT_REG_DWORD,&type,&value,&size)==ERROR_FILE_NOT_FOUND);
    REQUIRE(WritePrivateProfileStringW(L"DLSS",L"Indicator",L"Off",ini));
    REQUIRE(SUCCEEDED(elex_indicator_install(ini,log_line)));
    value=123;size=4;REQUIRE(RegGetValueW(HKEY_LOCAL_MACHINE,key,L"ShowDlssIndicator",RRF_RT_REG_DWORD,&type,&value,&size)==ERROR_SUCCESS&&value==0);
    REQUIRE(WritePrivateProfileStringW(L"DLSS",L"Indicator",L"Driver",ini));
    REQUIRE(SUCCEEDED(elex_indicator_install(ini,log_line)));
    value=0;size=4;LSTATUS after=RegGetValueW(HKEY_LOCAL_MACHINE,key,L"ShowDlssIndicator",RRF_RT_REG_DWORD,&type,&value,&size);
    REQUIRE(after==before_status);if(after==ERROR_SUCCESS)REQUIRE(value==before&&type==before_type);
    REQUIRE(MH_DisableHook(MH_ALL_HOOKS)==MH_OK);REQUIRE(MH_Uninitialize()==MH_OK);
    value=0;size=4;after=RegGetValueW(HKEY_LOCAL_MACHINE,key,L"ShowDlssIndicator",RRF_RT_REG_DWORD,&type,&value,&size);
    REQUIRE(after==before_status);if(after==ERROR_SUCCESS)REQUIRE(value==before);
    puts("PASS: native-indicator On/Off/Driver, A/W APIs, sizing, key isolation and unchanged persistent registry");
    return 0;
}

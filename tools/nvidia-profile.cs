// Minimal NVAPI DRS bindings for ELEX's two FG model-preset settings.
// ABI definitions/IDs: NVIDIA/nvapi nvapi.h, nvapi_interface.h and
// NvApiDriverSettings.h (MIT; notice in third_party/nvapi/LICENSE.txt).
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace ElexDlss {
    public sealed class DriverSetting {
        public uint Id;
        public bool HasOverride;
        public uint Value;
    }

    public sealed class NvidiaFgProfile : IDisposable {
        public const uint OverrideId = 0x10e41e03;
        public const uint PresetId = 0x10e41df1;
        const int SettingNotFound = -160, ApplicationNotFound = -166;
        IntPtr session, profile;
        bool initialized, writable;
        public string ProfileName { get; private set; }
        public bool Exists { get { return profile != IntPtr.Zero; } }

        [DllImport("nvapi64.dll", CallingConvention=CallingConvention.Cdecl, EntryPoint="nvapi_QueryInterface")]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        static extern IntPtr Query(uint id);
        [StructLayout(LayoutKind.Explicit, Pack=4, Size=4100)]
        struct Value { [FieldOffset(0)] public uint Dword; }
        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode, Pack=4)]
        struct Setting {
            public uint Version;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=2048)] public string Name;
            public uint Id, Type, Location, CurrentPredefined, PredefinedValid;
            public Value Predefined, Current;
        }
        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
        struct Application {
            public uint Version, Predefined;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=2048)] public string Name;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=2048)] public string FriendlyName;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=2048)] public string Launcher;
        }
        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
        struct Profile {
            public uint Version;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=2048)] public string Name;
            public uint GpuSupport, Predefined, AppCount, SettingCount;
        }
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int NoArgs();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int NewSession(out IntPtr handle);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int SessionOp(IntPtr handle);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int ErrorText(int code, StringBuilder text);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet=CharSet.Unicode)]
        delegate int FindApp(IntPtr session, [MarshalAs(UnmanagedType.LPWStr)] string name, out IntPtr profile, ref Application app);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int ProfileInfo(IntPtr session, IntPtr profile, ref Profile info);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int NewProfile(IntPtr session, ref Profile info, out IntPtr profile);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int NewApp(IntPtr session, IntPtr profile, ref Application app);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int Apps(IntPtr session, IntPtr profile, uint start, ref uint count, [In, Out] Application[] apps);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int GetValue(IntPtr session, IntPtr profile, uint id, ref Setting value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int SetValue(IntPtr session, IntPtr profile, ref Setting value);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int DeleteValue(IntPtr session, IntPtr profile, uint id);

        static T Function<T>(uint id) where T : class {
            IntPtr address=Query(id);
            if(address==IntPtr.Zero)throw new InvalidOperationException("Required NVIDIA driver API unavailable: 0x"+id.ToString("x8"));
            return Marshal.GetDelegateForFunctionPointer(address,typeof(T)) as T;
        }
        static void Check(int result) {
            if(result==0)return;
            var message=new StringBuilder(64);
            Function<ErrorText>(0x6c2d048c)(result,message);
            throw new InvalidOperationException("NVIDIA driver settings: "+message+" ("+result+")");
        }
        static uint Version(Type type) { return (uint)Marshal.SizeOf(type) | (1u<<16); }

        public static NvidiaFgProfile Open(string executable, bool create) {
            if(!Environment.Is64BitProcess)throw new InvalidOperationException("Use 64-bit PowerShell.");
            if(!String.Equals(Path.GetFileName(executable),"ELEX.exe",StringComparison.OrdinalIgnoreCase))
                throw new ArgumentException("This helper only changes the ELEX.exe profile.");
            if(Marshal.SizeOf(typeof(Setting))!=12320 || Marshal.SizeOf(typeof(Application))!=12296 || Marshal.SizeOf(typeof(Profile))!=4116)
                throw new InvalidOperationException("Unexpected NVAPI structure layout.");
            var api=new NvidiaFgProfile();
            try {
                Check(Function<NoArgs>(0x0150e828)());api.initialized=true;
                Check(Function<NewSession>(0x0694d52e)(out api.session));
                Check(Function<SessionOp>(0x375dbd6b)(api.session));
                var app=new Application {Version=Version(typeof(Application))};
                int result=Function<FindApp>(0xeee566b2)(api.session,executable,out api.profile,ref app);
                if(result==ApplicationNotFound) {
                    app.Version=Version(typeof(Application));
                    result=Function<FindApp>(0xeee566b2)(api.session,"elex.exe",out api.profile,ref app);
                }
                if(result==ApplicationNotFound && !create)return api;
                if(result==ApplicationNotFound) {
                    var added=new Profile {Version=Version(typeof(Profile)),Name="ELEX DLSS presets"};
                    Check(Function<NewProfile>(0xcc176068)(api.session,ref added,out api.profile));
                    app=new Application {Version=Version(typeof(Application)),Name=Path.GetFullPath(executable),FriendlyName="ELEX",Launcher=""};
                    Check(Function<NewApp>(0x4347a9de)(api.session,api.profile,ref app));
                } else Check(result);
                var info=new Profile {Version=Version(typeof(Profile))};
                Check(Function<ProfileInfo>(0x61cd6fd6)(api.session,api.profile,ref info));
                api.ProfileName=info.Name;
                // Never write the global profile or a profile shared with another game.
                if(info.AppCount==0 || info.AppCount>64)throw new InvalidOperationException("Unexpected ELEX application profile.");
                var apps=new Application[info.AppCount];
                for(int i=0;i<apps.Length;i++)apps[i].Version=Version(typeof(Application));
                uint count=info.AppCount;
                Check(Function<Apps>(0x7fa2173a)(api.session,api.profile,0,ref count,apps));
                api.writable=count>0;
                for(int i=0;i<count;i++)api.writable &= String.Equals(Path.GetFileName(apps[i].Name),"elex.exe",StringComparison.OrdinalIgnoreCase);
                if(create && !api.writable)throw new InvalidOperationException("ELEX shares its NVIDIA profile with another application; refusing to change that profile.");
                return api;
            } catch {api.Dispose();throw;}
        }

        public DriverSetting[] Snapshot() {
            var result=new DriverSetting[2];uint[] ids={OverrideId,PresetId};
            for(int i=0;i<ids.Length;i++) {
                result[i]=new DriverSetting {Id=ids[i]};
                if(!Exists)continue;
                var setting=new Setting {Version=Version(typeof(Setting))};
                int status=Function<GetValue>(0x73bf8338)(session,profile,ids[i],ref setting);
                if(status==SettingNotFound)continue;
                Check(status);
                if(setting.Type!=0)throw new InvalidOperationException("NVIDIA preset setting is not a DWORD.");
                result[i].HasOverride=setting.Location==0 && setting.CurrentPredefined==0;
                result[i].Value=setting.Current.Dword;
            }
            return result;
        }

        void Stage(DriverSetting[] settings) {
            if(!Exists || !writable)throw new InvalidOperationException("No writable ELEX-only NVIDIA profile.");
            if(settings.Length!=2 || settings[0].Id!=OverrideId || settings[1].Id!=PresetId)
                throw new ArgumentException("Only the two FG preset settings may be changed.");
            foreach(var row in settings) {
                if(row.HasOverride) {
                    var setting=new Setting {Version=Version(typeof(Setting)),Id=row.Id,Type=0,Current=new Value {Dword=row.Value}};
                    Check(Function<SetValue>(0x577dd202)(session,profile,ref setting));
                } else {
                    int status=Function<DeleteValue>(0xe4a26362)(session,profile,row.Id);
                    if(status!=SettingNotFound)Check(status);
                }
            }
        }

        public void Apply(DriverSetting[] settings) {
            Stage(settings);
            Check(Function<SessionOp>(0xfcbc7e14)(session));
        }

        // Diagnostic: exercises native DRS validation in this session only.
        // Dispose without Apply/SaveSettings discards these staged changes.
        public DriverSetting[] ValidatePreset(uint preset) {
            if(preset!=1 && preset!=2 && preset!=3 && preset!=0x00ffffff)
                throw new ArgumentException("Unsupported FG preset.");
            Stage(new [] {
                new DriverSetting {Id=OverrideId,HasOverride=true,Value=1},
                new DriverSetting {Id=PresetId,HasOverride=true,Value=preset}
            });
            return Snapshot();
        }

        public void Dispose() {
            if(session!=IntPtr.Zero){Function<SessionOp>(0xdad9cff8)(session);session=IntPtr.Zero;}
            if(initialized){Function<NoArgs>(0xd22bdd7e)();initialized=false;}
        }
    }
}

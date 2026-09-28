"""Compile the unchanged screen implementations with desktop platform adapters."""
from pathlib import Path
import subprocess
import sys
import hashlib
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / '.sim-cache'
BUILD = CACHE / 'build'
BUILD.mkdir(parents=True, exist_ok=True)
ZIG = CACHE / 'toolchain/ziglang/zig.exe'
INCLUDES = [ROOT/'simulator/include', ROOT/'src/ui-touch', ROOT/'src', ROOT/'include',
            CACHE/'arduinojson/src',
            CACHE/'lvgl', CACHE/'meshcore/src', CACHE/'meshcore/examples/companion_radio']
FLAGS = ['-DGUARD_SIMULATOR=1', '-DHAS_TOUCH_UI=1', '-DHAS_TDECK_GT911=1',
         '-DHAS_TDECK_KEYBOARD=1', '-DHAS_TDECK_TRACKBALL=1',
         '-DPIN_USER_BTN=0',
         '-DCAP_LUA_APPS=0', '-DCAP_LUA_AUDIO=0', '-DCAP_USB_FILES=0', '-DLV_CONF_INCLUDE_SIMPLE=1']
BUILD_SIGNATURE = ''

def compile_source(source):
    obj=BUILD/(source.stem+'-'+hashlib.sha1(str(source).encode()).hexdigest()[:8]+'.o')
    signature=BUILD_SIGNATURE+str(source.stat().st_mtime_ns)+str(source.stat().st_size)
    stamp=obj.with_suffix('.stamp')
    if obj.exists() and stamp.exists() and stamp.read_text()==signature: return obj,''
    args=[str(ZIG),'cc' if source.suffix=='.c' else 'c++']
    if source.suffix!='.c': args+=['-std=c++17','-Wno-c++11-narrowing']
    args+=['-O1','-g','-ferror-limit=0',*FLAGS,*['-I'+str(x) for x in INCLUDES],'-c',str(source),'-o',str(obj)]
    result=subprocess.run(args,capture_output=True,text=True)
    if result.returncode==0: stamp.write_text(signature)
    return (obj if result.returncode==0 else None),result.stderr

def main():
    global BUILD_SIGNATURE
    subprocess.run([sys.executable,str(ROOT/'scripts/build/patch_lvgl_anim_uaf.py'),'--patch-file',str(CACHE/'lvgl/src/misc/lv_anim.c')],check=True)
    headers=[p for directory in [ROOT/'simulator',ROOT/'src',ROOT/'include',CACHE/'lvgl',CACHE/'meshcore/src'] for p in directory.rglob('*.h')]
    metadata='\n'.join(str(p)+':'+str(p.stat().st_mtime_ns)+':'+str(p.stat().st_size) for p in sorted(headers))
    BUILD_SIGNATURE=hashlib.sha256((metadata+repr(FLAGS)+Path(__file__).read_text()).encode()).hexdigest()
    sources=sorted((CACHE/'lvgl/src').rglob('*.c'))+sorted((ROOT/'src/ui-touch').glob('*.c'))
    sources += [ROOT/'src/ui-touch'/name for name in ['UITask.cpp','i18n.cpp','KeyboardLayouts.cpp','SnakeGame.cpp','TouchSleep.cpp']]
    sources += sorted((ROOT/'src/ui-touch/platform').glob('*.cpp'))
    # Shared modules compile independently on desktop and on the firmware target.
    for module in ['application', 'models', 'services', 'screens', 'widgets', 'theme', 'platform/desktop']:
        sources += sorted((ROOT/'src/ui-touch'/module).rglob('*.cpp'))
    sources += [ROOT/'src/guardian_logo.c', ROOT/'src/helpers/esp32/TouchPrefsStore.cpp',ROOT/'simulator/main.cpp',ROOT/'simulator/ui_regression.cpp',ROOT/'simulator/history_regression.cpp',ROOT/'simulator/map_regression.cpp',ROOT/'simulator/terminal_regression.cpp',ROOT/'simulator/file_screen_regression.cpp']
    sources += [ROOT/'simulator/app_inventory_regression.cpp']
    sources += [ROOT/'simulator/store_install_regression.cpp']
    sources += [ROOT/'simulator/store_data_regression.cpp']
    sources += [ROOT/'simulator/store_screen_regression.cpp']
    sources += [ROOT/'simulator/keyboard_binding_regression.cpp']
    sources += [ROOT/'simulator/chat_timeline_regression.cpp']
    sources += [ROOT/'simulator/message_menu_regression.cpp']
    sources += [ROOT/'simulator/message_info_regression.cpp']
    sources += [ROOT/'simulator/focus_targets_regression.cpp']
    sources += [ROOT/'simulator/focus_navigation_regression.cpp']
    sources += [ROOT/'simulator/focus_context_regression.cpp']
    sources += [ROOT/'simulator/wifi_forms_regression.cpp']
    sources += [ROOT/'simulator/bluetooth_settings_regression.cpp']
    sources += [ROOT/'simulator/lock_screen_regression.cpp']
    sources += [ROOT/'simulator/screen_policy_regression.cpp']
    sources += [ROOT/'simulator/chat_session_regression.cpp']
    sources += [ROOT/'simulator/message_ingress_regression.cpp']
    sources += [ROOT/'simulator/thread_refresh_regression.cpp']
    sources += [ROOT/'simulator/contacts_performance_regression.cpp']
    sources += [ROOT/'simulator/setup_wizard_regression.cpp']
    sources += [ROOT/'simulator/blocked_users_regression.cpp']
    sources += [ROOT/'simulator/spatial_navigation_regression.cpp']
    sources += [ROOT/'simulator/language_file_regression.cpp']
    sources += [ROOT/'simulator/thread_list_regression.cpp']
    sources += [ROOT/'simulator/home_screen_regression.cpp']
    sources += [ROOT/'simulator/guardian_app_regression.cpp', ROOT/'simulator/ping_reply_regression.cpp']
    sources += [ROOT/'simulator/thread_menu_regression.cpp']
    sources += [ROOT/'simulator/glyph_picker_regression.cpp']
    sources += [ROOT/'simulator/quick_reply_picker_regression.cpp']
    sources += [ROOT/'simulator/mention_picker_regression.cpp']
    sources += [ROOT/'simulator/accent_picker_regression.cpp']
    sources += [ROOT/'simulator/accent_cycle_regression.cpp']
    sources += [ROOT/'simulator/text_selection_regression.cpp']
    sources += [ROOT/'simulator/text_edit_menu_regression.cpp']
    sources += [ROOT/'simulator/chat_composer_regression.cpp']
    sources += [ROOT/'simulator/release_picker_regression.cpp', ROOT/'simulator/sightline_regression.cpp', ROOT/'simulator/system_info_regression.cpp']
    sources += [ROOT/'simulator/firmware_panel_regression.cpp']
    sources += [ROOT/'simulator/clock_settings_regression.cpp']
    sources += [ROOT/'simulator/gps_settings_regression.cpp']
    sources += [ROOT/'simulator/battery_history_regression.cpp']
    sources += [ROOT/'simulator/battery_settings_regression.cpp', ROOT/'simulator/sound_settings_regression.cpp', ROOT/'simulator/keyboard_settings_regression.cpp', ROOT/'simulator/display_settings_regression.cpp', ROOT/'simulator/appearance_regression.cpp', ROOT/'simulator/general_settings_regression.cpp', ROOT/'simulator/backup_screen_regression.cpp', ROOT/'simulator/contact_actions_regression.cpp']
    sources += [ROOT/'simulator/backup_operations_regression.cpp']
    sources += [ROOT/'simulator/admin_session_regression.cpp']
    sources += [ROOT/'simulator/crash_diagnostics.cpp']
    errors=[];objects=[]
    with ThreadPoolExecutor(max_workers=4) as pool:
        for i,(obj,diag) in enumerate(pool.map(compile_source,sources)):
            if obj: objects.append(obj)
            if diag: errors.append(diag)
            if not obj: print('\n'.join(l for l in diag.splitlines() if 'error:' in l),flush=True)
            if i%50==0: print(f'Compiled {i+1}/{len(sources)}',flush=True)
    if len(objects)==len(sources):
        signature=hashlib.sha256(''.join(str(p)+str(p.stat().st_mtime_ns) for p in objects).encode()).hexdigest()[:12]
        executable=BUILD/('guard-mesh-sim-'+signature+'.exe')
        response=BUILD/'link.rsp'
        response.write_text('\n'.join('"'+str(p)+'"' for p in objects)+'\n-luser32\n-lgdi32\n-lbcrypt\n-ldbghelp\n-o\n"'+str(executable)+'"',encoding='utf-8')
        code=0
        if not executable.exists():
            link=subprocess.run([str(ZIG),'c++','@'+str(response)],capture_output=True,text=True)
            errors.append(link.stderr)
            if link.returncode: print(link.stderr[:15000])
            code=link.returncode
        if code==0: (BUILD/'executable.txt').write_text(executable.name,encoding='utf-8')
    else: code=1
    (BUILD/'errors.txt').write_text('\n'.join(errors),encoding='utf-8')
    print('Build result:',code,flush=True)
    return code

if __name__ == '__main__': sys.exit(main())

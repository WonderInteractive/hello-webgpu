# Building Windows Dawn (and optionally [ANGLE](https://chromium.googlesource.com/angle/angle))

These are based on [Dawn's build instructions](//dawn.googlesource.com/dawn/+/HEAD/docs/buiding.md) but tailored for Windows (and specifically a DLL that can be linked with MSVC).

1. Install Visual Studio (here VS2019 was used but 2015 or 2017 should also work). The [Community](//visualstudio.microsoft.com/vs/community/) edition is fine. Add [CMake](//cmake.org) (and [Ninja](//ninja-build.org)) in the VS install options.

2. You need the the [full Windows 10 SDK](//developer.microsoft.com/en-gb/windows/downloads/windows-10-sdk/); The VS installer will install the Win10 SDK but it misses the required [Debugging Tools for Windows](//docs.microsoft.com/en-us/windows-hardware/drivers/debugger/).

3. Make sure you have up-to-date graphics drivers. If you're running Windows on Boot Camp install one of the [unofficial AMD drivers](//www.bootcampdrivers.com) (if only to get Vulkan support; it'd be nice if Apple made this step unnecessary).

4. Install the [Vulkan SDK](//www.lunarg.com/vulkan-sdk/). Make sure the examples run (`vkcube.exe`, for example).

5. Install Git. I installed Git for Windows via the [Git Credential Manager](https://github.com/Microsoft/Git-Credential-Manager-for-Windows/releases/).

6. Install Google's [Depot Tools](//commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up). I didn't install Depot Tools the recommended way, instead I did this:

	1. Make sure Git is on the `PATH` (via the environment variable control panel).

	2. Launch a VS2019 x64 Native Tools Command Prompt (found in the Windows menu; note to investigate: a regular Prompt might be enough, since Depot Tools knows how to find the compiler).

	3. Set-up Git following the *Bootstrapping* section in the Depot Tools page above.

	4. `cd` into your dev/work/code directory (mine is `C:\Volumes\Data\Work\Native`).

	5. Clone Depot Tools:

		`git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git`

	6. Add Depot Tools to the front of your `PATH` in the current Command Prompt:

		`set PATH=C:\Volumes\Data\Work\Native\depot_tools;%PATH%`

	7. Run `gclient` (which should download the CIPD client then show you some options).

	8. Add `win32file` to Depot Tools' Python (and verify with `where python` that Depot Tools is the preferred python executable):

		`python -m pip install pywin32`

	The reason for doing it this way is it keeps Depot Tools from being the first `PATH` entry and interferring with other tools (and plus the Google instructions are a bit clunky for Windows).

7. In the same VS2019 x64 Prompt above, with the same `PATH`, etc., clone Dawn following the [Building Dawn instructions](https://dawn.googlesource.com/dawn/+/HEAD/docs/buiding.md). We need to make these steps a little more Windows friendly:

	```bat
	git clone https://dawn.googlesource.com/dawn dawn && cd dawn
	copy scripts\standalone.gclient .gclient
	set DEPOT_TOOLS_WIN_TOOLCHAIN=0
	gclient sync
	```

8. Configure then build Dawn:

	1. `gn args out/Release`

	2. In the text file that just opened add `is_debug=false` then save and close it.

	3. `ninja -C out\Release`

9. That should be it. Run the samples in the `out` directory (`CHelloTriangle.exe`, for example). Now that the basic install builds and runs the configuration can be investigated and tweaked:

	`gn args out/Release --list`

	For release I went with:

	```ini
	is_clang=false
	is_debug=false
	strip_debug_info=true
	symbol_level=0
	asan_globals=false
	```

	Note the the all-important `is_clang=false`, needed since we want to link with MSVC (a step which saves everyone the headache of wondering why the returned `std::vector` and other types have the wrong signature). It's also the reason for the `win32file` addition to Python in the earlier steps. It's safe to ignore the many `D9002 : ignoring unknown option '/Zc:twoPhase'` warnings.

	For debug:

	```ini
	is_clang=false
	is_debug=true
	enable_iterator_debugging=true
	```

	If you don't set the `enable_iterator_debugging` option then you'll need [`_ITERATOR_DEBUG_LEVEL=0`](//docs.microsoft.com/en-us/cpp/standard-library/iterator-debug-level?view=vs-2019) setting in the preprocessor.

	At this point you might want to produce builds for both `target_cpu="x64"` and `target_cpu="x86"`.
	
10. That's it for Dawn but (optionally) almost the same steps can be used to build [ANGLE](https://chromium.googlesource.com/angle/angle/+/HEAD/doc/DevSetup.md).

	Taking the same arguments as Dawn plus:

	```ini
	angle_enable_swiftshader=false
	angle_enable_vulkan=false
	```

	Note: ANGLE currently fails to build when disabling D3D9.
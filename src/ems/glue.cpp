#include <emscripten.h>
#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <emscripten/threading.h>
#include <pthread.h>
/**
 * \def KEEP_IN_MODULE
 * Marks a function to be kept in the \c Module and exposed to script. An
 * alternative to Emscripten's \c bind or \c cwrap.
 * \code
 *	// C++
 *	KEEP_IN_MODULE int getValue() {
 *		return 42;
 *	}
 *	// JavaScript
 *	console.log(Module._getValue());
 * \endcode
 */
#ifndef KEEP_IN_MODULE
#define KEEP_IN_MODULE extern "C" __attribute__((used, visibility("default")))
#endif

/**
 * Entry point for the 'real' application.
 *
 * \param[in] argc count of program arguments in argv
 * \param[in] argv program arguments (excluding the application)
 */
extern "C" int __main__(int /*argc*/, char* /*argv*/[]);

//****************************************************************************/

namespace impl {
/**
 * JavaScript async calls that need to finish before calling \c main().
 */
EM_JS(void, glue_preint, (), {
	var entry = __glue_main_;
	if (entry) {
		/*
		 * None of the WebGPU properties appear to survive Closure, including
		 * Emscripten's own `preinitializedWebGPUDevice` (which from looking at
		 *`library_html5` is probably designed to be inited in script before
		 * loading the Wasm).
		 */
		if (navigator["gpu"]) {
			navigator["gpu"]["requestAdapter"]().then(function (adapter) {
				adapter["requestDevice"]().then( function (device) {
					Module["preinitializedWebGPUDevice"] = device;
					entry();
				});
			}, function () {
				console.error("No WebGPU adapter; not starting");
			});
		} else {
			console.error("No support for WebGPU; not starting");
		}
	} else {
		console.error("Entry point not found; unable to start");
	}
});
}

//****************************************************************************/

/**
 * Redirector to call \c __main__() (exposed to Emscripten's \c Module).
 *
 * \todo pass URL query string for args
 */
KEEP_IN_MODULE void _glue_main_() {
	__main__(0, nullptr);
}

/**
 * Entry point. Workaround for Emscripten needing an \c async start.
 */
void mainloop();
void* start2(void*args){
	impl::glue_preint();
	return 0;
}
int main(int /*argc*/, char* /*argv*/[]) {
	auto arg = 1;
	pthread_t thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	//pthread_attr_setstacksize(&attr, 1024 * 1024 * 10);
	emscripten_pthread_attr_settransferredcanvases(&attr, "#canvas");
	pthread_create(&thread, &attr, start2, 0);
	emscripten_set_main_loop(mainloop, 300, 0);
	return 0;
}

#include "pthreadfs.h"
#include <assert.h>
#include <emscripten.h>
#include <pthread.h>
#include <sys/stat.h>
#include <wasi/api.h>
#include <functional>
#include <iostream>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <utility>

#include <stdarg.h>

namespace emscripten {

	sync_to_async::sync_to_async() : childLock(mutex) {
		// The child lock is associated with the mutex, which takes the lock as we
		// connect them, and so we must free it here so that the child can use it.
		// Only the child will lock/unlock it from now on.
		childLock.unlock();

		// Create the thread after the lock is ready.
		thread = std::make_unique<std::thread>(threadMain, this);
	}

	sync_to_async::~sync_to_async() {
		// Wake up the child to tell it to quit.
		invoke([&](Callback func) {
			quit = true;
			(*func)();
			});

		thread->join();
	}

	void sync_to_async::invoke(std::function<void(sync_to_async::Callback)> newWork) {
		// Use the invokeMutex to prevent more than one invoke being in flight at a
		// time, so that this is usable from multiple threads safely.
		std::lock_guard<std::mutex> invokeLock(invokeMutex);
		// Initialize the PThreadFS file system.
		/*if (!pthreadfs_initialized) {
		  {
			std::lock_guard<std::mutex> lock(mutex);
			work = [](sync_to_async::Callback done) {
			  g_resumeFct = [done]() { (*done)(); };
			  pthreadfs_init(PTHREADFS_FOLDER_NAME, &resumeWrapper_v);
			};
			finishedWork = false;
			readyToWork = true;
		  }
		  condition.notify_one();

		  // Wait for it to be complete.
		  std::unique_lock<std::mutex> lock(mutex);
		  condition.wait(lock, [&]() { return finishedWork; });
		  pthreadfs_initialized = true;
		}*/
		// Send the work over.
		{
			std::lock_guard<std::mutex> lock(mutex);
			work = newWork;
			finishedWork = false;
			readyToWork = true;
		}
		condition.notify_one();

		// Wait for it to be complete.
		std::unique_lock<std::mutex> lock(mutex);
		condition.wait(lock, [&]() { return finishedWork; });
	}

	void* sync_to_async::threadMain(void* arg) {
		// Prevent the pthread from shutting down too early.
		EM_ASM(runtimeKeepalivePush(););
		emscripten_async_call(threadIter, arg, 0);
		return 0;
	}

	void sync_to_async::threadIter(void* arg) {
		auto* parent = (sync_to_async*)arg;
		if (parent->quit) {
			EM_ASM(runtimeKeepalivePop(););
			pthread_exit(0);
		}
		// Wait until we get something to do.
		parent->childLock.lock();
		parent->condition.wait(parent->childLock, [&]() { return parent->readyToWork; });
		auto work = parent->work;
		parent->readyToWork = false;
		// Allocate a resume function, and stash it on the parent.
		parent->resume = std::make_unique<std::function<void()>>([parent, arg]() {
			// We are called, so the work was finished. Notify the caller.
			parent->finishedWork = true;
			parent->childLock.unlock();
			parent->condition.notify_one();
			// Look for more work.
			// For performance reasons, this is an asynchronous call. If a synchronous API
			// were to be called, these chained calls would lead to an out-of-stack error.
			threadIter(arg);
			});
		// Run the work function the user gave us. Give it a pointer to the resume
		// function.
		work(parent->resume.get());
	}

	bool is_pthreadfs_file(std::string path) {
		auto const regex = std::regex("/*" PTHREADFS_FOLDER_NAME "(/*$|/+.*)");
		return std::regex_match(path, regex);
	}

	bool is_pthreadfs_fd_link(std::string path) {
		auto const regex = std::regex("^/*proc/+self/+fd/+([0-9]+)$");
		std::smatch match;
		if (regex_match(path, match, regex)) {
			char* p;
			long fd = strtol(match.str(1).c_str(), &p, 10);
			// As defined in library_asyncfs.js, the minimum fd for PThreadFS is 4097.
			if (*p == 0 && fd >= 4097) {
				return true;
			}
		}
		return false;
	}

} // namespace emscripten

// Static functions calling resumFct and setting the return value.
void resumeWrapper_v() { g_resumeFct(); }
// return value long
long resume_result_long = 0;
void resumeWrapper_l(long retVal) {
	resume_result_long = retVal;
	g_resumeFct();
}
// return value __wasi_errno_t
__wasi_errno_t resume_result_wasi = 0;
void resumeWrapper_wasi(__wasi_errno_t retVal) {
	resume_result_wasi = retVal;
	g_resumeFct();
}

// File System Access collection
std::set<long> fsa_file_descriptors;
std::set<std::string> mounted_directories;

// Define global variables to be populated by resume;
std::function<void()> g_resumeFct;
emscripten::sync_to_async g_sync_to_async_helper __attribute__((init_priority(102)));

void pthreadfs_load_package(const char* package_path) {
	g_sync_to_async_helper.invoke([package_path](emscripten::sync_to_async::Callback resume) {
		g_resumeFct = [resume]() { (*resume)(); };
		// clang-format off
		EM_ASM({
		  (async() => {
			  console.log(`Loading package ${UTF8ToString($1)}`);
			  importScripts(UTF8ToString($1));
			  await PThreadFS.loadAvailablePackages();
			  wasmTable.get($0)();
		  })();
			  }, &resumeWrapper_v, package_path);
		// clang-format on
			});
}

#include "CppUnitTest.h"

#include <functional>
#include <future>
#include <optional>
#include <thread>
#include <chrono>
#include <atomic>
#include <format>

///////////////////////////////////////////////////////////////////////////////

using namespace std::chrono_literals;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

///////////////////////////////////////////////////////////////////////////////

template<typename Fn_T>
struct janitor
{
	Fn_T _fn;

	janitor(Fn_T&& fn) : _fn{ std::forward<Fn_T>(fn) } {}
	~janitor() { _fn(); }
};

///////////////////////////////////////////////////////////////////////////////

class not_a_timer
{
public:
	using Fn_t = std::function<bool()>;

	~not_a_timer(){
		if (_running_fn) {
			_running_fn->wait();
		}
	}

	not_a_timer() = default;

	not_a_timer(const not_a_timer&) = delete;
	not_a_timer& operator=(const not_a_timer&) = delete;

	not_a_timer(not_a_timer&&) = default;
	not_a_timer& operator=(not_a_timer&&) = default;

	void run(Fn_t&& fn) {
		_keep_running = true;
		_is_running = true;
		janitor _{[this]() { _is_running = false; }};
		while (_keep_running && fn());
	}

	void run_async(Fn_t&& fn) {
		std::unique_lock<std::mutex> lock(_mutex);
		_running_fn = std::async(std::launch::async, [this, &fn](){
			_notify_async_run_started();
			run(std::move(fn));
			});

		_async_run_started.wait(lock);
	}

	void stop() {
		_keep_running = false;
	}

	bool is_running() const { return _is_running; }

private:
	void _notify_async_run_started() {
		std::unique_lock<std::mutex> lock{ _mutex };
		_async_run_started.notify_all();
	}

	std::optional<std::future<void>> _running_fn;
	std::condition_variable _async_run_started;
	std::mutex _mutex;
	std::atomic<bool> _keep_running{ true };
	std::atomic<bool> _is_running{ false };
};

///////////////////////////////////////////////////////////////////////////////

namespace test
{
	TEST_CLASS(test_not_a_timer)
	{
	public:
		
		TEST_METHOD(runs_synchronously)
		{
			auto t = not_a_timer{};
			
			auto count = size_t{ 100 };
			t.run([&count]() { return --count > 0; });

			Assert::AreEqual(size_t{ 0 }, count);
		}

		TEST_METHOD(destructor_blocks_while_timer_is_running)
		{
			auto count = size_t{ 360'000'000 };

			{
				auto t = not_a_timer{};
				t.run_async([&count]() { return --count > 0; });
			}

			Assert::AreEqual(size_t{ 0 }, count);
		}

		TEST_METHOD(async_run_can_be_stopped)
		{
			auto count = size_t{ 360'000'000 };

			{
				auto t = not_a_timer{};
				t.run_async([&count]() { return --count > 0; });

				std::this_thread::sleep_for(100ms);
				t.stop();
			}

			Assert::IsTrue(size_t{ 0 } < count, std::format(L"count = {}", count).c_str());
		}

		TEST_METHOD(is_running_reports_correctly)
		{
			auto count = size_t{ 360'000'000 };

			{
				auto t = not_a_timer{};
				t.run_async([&count]() { return --count > 0; });

				std::this_thread::sleep_for(100ms);
				Assert::IsTrue(t.is_running());

				t.stop();

				std::this_thread::sleep_for(100ms);
				Assert::IsFalse(t.is_running());
			}

			Assert::IsTrue(size_t{ 0 } < count, std::format(L"count = {}", count).c_str());
		}
	};
}

///////////////////////////////////////////////////////////////////////////////


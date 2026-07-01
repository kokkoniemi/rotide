#include <vector>
#define APPLY(x) ((x) + 1)

namespace Demo {
template <typename T>
concept Number = requires(T value) { value + 1; };

class Widget final {
public:
	explicit Widget(int value) noexcept : value_(value) {}

	template <typename U>
	U convert(U input) const {
		auto local = static_cast<U>(value_) + input;
		return local;
	}

private:
	int value_;
};

constexpr int answer = 42;
const char marker = 'x';
const char *message = "text";
const char *empty = nullptr;

int run() {
	Widget item(answer);
	item.convert(3);
	return Demo::helper(answer);
}
}

const char *page = R"html(<section class="card"></section>)html";
const char *query = R"sql(SELECT value FROM records)sql";

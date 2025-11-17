#include <variant>

template <typename T, typename E>
class Result {
private:
    std::variant<T, E> data;

public:
    Result(const T& value) : data(value) {}
    Result(const E& error) : data(error) {}

    bool is_ok() const { return std::holds_alternative<T>(data); }
    bool is_err() const { return std::holds_alternative<E>(data); }

    T& unwrap() { return std::get<T>(data); }
    E& unwrap_err() { return std::get<E>(data); }
};


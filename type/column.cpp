#include "column.h"

#include <cstring>
#include <sstream>
#include <utility>

namespace {

template <typename T>
auto ReadPod(const uint8_t *buf) -> T {
	T value{};
	std::memcpy(&value, buf, sizeof(T));
	return value;
}

template <typename T>
auto WritePod(uint8_t *buf, const T &value) -> void {
	std::memcpy(buf, &value, sizeof(T));
}

}  // namespace

Column::Column(std::string name, TypeId type_id, uint32_t length, uint32_t offset)
	: name_(std::move(name)), type_id_(type_id), length_(length), offset_(offset) {}

Column::Column(std::string name, TypeId type_id, uint32_t offset)
	: name_(std::move(name)), type_id_(type_id), length_(sizeof(varchar_len_t)), offset_(offset) {}

auto Column::GetName() const -> const std::string & { return name_; }

auto Column::GetType() const -> TypeId { return type_id_; }

auto Column::GetLength() const -> uint32_t { return length_; }

auto Column::GetOffset() const -> uint32_t { return offset_; }

auto Column::IsVariableLength() const -> bool { return type_id_ == TypeId::VARCHAR; }

auto Column::ToString() const -> std::string {
	std::ostringstream out;
	out << "Column{name=" << name_ << ", type=" << static_cast<int>(type_id_)
	    << ", length=" << length_ << ", offset=" << offset_ << "}";
	return out.str();
}

auto Column::Serialize(uint8_t *buf) const -> uint16_t {
	const auto name_len = static_cast<uint16_t>(name_.size());
	uint8_t *cursor = buf;
	WritePod(cursor, name_len);
	cursor += sizeof(name_len);
	std::memcpy(cursor, name_.data(), name_len);
	cursor += name_len;
	const auto type = static_cast<uint8_t>(type_id_);
	WritePod(cursor, type);
	cursor += sizeof(type);
	WritePod(cursor, length_);
	cursor += sizeof(length_);
	WritePod(cursor, offset_);
	cursor += sizeof(offset_);
	return static_cast<uint16_t>(cursor - buf);
}

auto Column::Deserialize(const uint8_t *buf, std::size_t *consumed) -> Column {
	const uint8_t *cursor = buf;
	const auto name_len = ReadPod<uint16_t>(cursor);
	cursor += sizeof(name_len);
	std::string name(reinterpret_cast<const char *>(cursor), name_len);
	cursor += name_len;
	const auto type = static_cast<TypeId>(ReadPod<uint8_t>(cursor));
	cursor += sizeof(uint8_t);
	const auto length = ReadPod<uint32_t>(cursor);
	cursor += sizeof(uint32_t);
	const auto offset = ReadPod<uint32_t>(cursor);
	cursor += sizeof(uint32_t);
	if (consumed != nullptr) {
		*consumed = static_cast<std::size_t>(cursor - buf);
	}
	return Column(std::move(name), type, length, offset);
}
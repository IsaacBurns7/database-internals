#include "schema.h"

#include <cstring>
#include <sstream>

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

Schema::Schema(const std::vector<Column> &columns) : columns_(columns) {
	for (std::size_t i = 0; i < columns_.size(); ++i) {
		if (!columns_[i].IsVariableLength()) {
			fixed_size_columns.push_back(static_cast<uint32_t>(i));
		}
		fixed_size_ += columns_[i].GetLength();
	}
}

Schema::Schema(const Schema &other, const std::vector<uint32_t> &col_indices) {
	columns_.reserve(col_indices.size());
	for (const auto &index : col_indices) {
		const Column &col = other.GetColumn(index);
		columns_.push_back(col);
		if (!col.IsVariableLength()) {
			fixed_size_columns.push_back(static_cast<uint32_t>(columns_.size() - 1));
		}
		fixed_size_ += col.GetLength();
	}
}

auto Schema::GetColumn(uint32_t col_idx) const -> const Column & {
	return columns_[col_idx];
}

auto Schema::GetColIdx(const std::string &col_name) const -> std::optional<uint32_t> {
	for (std::size_t i = 0; i < columns_.size(); ++i) {
		if (columns_[i].GetName() == col_name) {
			return static_cast<uint32_t>(i);
		}
	}
	return std::nullopt;
}

auto Schema::GetType(uint32_t colidx) const -> TypeId {
    return columns_[colidx].GetType(); //this is so insanely chud... 
}

auto Schema::GetColumns() const -> const std::vector<Column> & { return columns_; }

auto Schema::GetColumnCount() const -> uint32_t {
	return static_cast<uint32_t>(columns_.size());
}

auto Schema::GetFixedSize() const -> uint32_t {
	return fixed_size_;
}

auto Schema::HasVariableLengthColumns() const -> bool {
	return fixed_size_columns.size() != columns_.size();
}

auto Schema::IsFixedLength(uint32_t col_idx) const -> bool {
	return !columns_[col_idx].IsVariableLength();
}

auto Schema::ToString() const -> std::string {
	std::ostringstream out;
	out << "Schema{";
	for (std::size_t i = 0; i < columns_.size(); ++i) {
		if (i != 0) {
			out << ", ";
		}
		out << columns_[i].ToString();
	}
	out << "}";
	return out.str();
}

auto Schema::SerializeSchema(uint8_t *buf) const -> uint32_t {
	uint8_t *cursor = buf;
	const auto column_count = static_cast<uint32_t>(columns_.size());
	WritePod(cursor, column_count);
	cursor += sizeof(column_count);
	for (const auto &column : columns_) {
		cursor += column.Serialize(cursor);
	}
	return static_cast<uint32_t>(cursor - buf);
}

auto Schema::Deserialize(const uint8_t *buf, std::size_t *consumed) -> Schema {
	const uint8_t *cursor = buf;
	const auto column_count = ReadPod<uint32_t>(cursor);
	cursor += sizeof(column_count);
	std::vector<Column> columns;
	columns.reserve(column_count);
	for (uint32_t i = 0; i < column_count; ++i) {
		std::size_t column_consumed = 0;
		columns.push_back(Column::Deserialize(cursor, &column_consumed));
		cursor += column_consumed;
	}
	if (consumed != nullptr) {
		*consumed = static_cast<std::size_t>(cursor - buf);
	}
	return Schema(columns);
}

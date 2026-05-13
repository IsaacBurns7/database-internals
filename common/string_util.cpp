#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <iomanip> 
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "common/string_util.h"
#include "fmt/format.h"
#include "fmt/ranges.h"
	
static auto StringUtil::Contains(const std::string &haystack, const std::string &needle) -> bool {
	return (haystack.find(needle) != std::string::npos);
} 
static auto StringUtil::ContainsAfter(const std::string &keyword, const std::string &haystack, const std::string &needle) -> bool { //this really shouldnt be khn it should be hnk (contains -> after) 
	auto pos = haystack.find(keyword);
	if(pos == std::string::npos){
		return false; 
	}
	return (haystack.find(needle, pos) != std::string::npos);
} 
static auto StringUtil::StartsWith(const std::string &str, const std::string &prefix) -> bool {
	return std::equal(prefix.begin(), prefix.end(), str.begin()); 
}
static auto StringUtil::EndsWith(const std::string &str, const std::string &suffix) -> bool {
	if(suffix.size() > str.size()){
		return false; 
	}
	return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin()); 
} 
static auto StringUtil::Repeat(const std::string &str, std::size_t n) -> std::string {
	std::ostringstream oss; 
	if(n == 0 | str.empty()){
		return (oss.str());
	}
	for(size_t i = 0; i < n; i++){
		oss << str;
	}
	return (oss.str());
}
static auto StringUtil::Split(const std::string &str, char delimiter) -> std::vector<std::string> {
	std::stringstream ss(str);
	std::vector<std::string> lines;
	std::string temp; 
	while(std::getline(ss, temp, delimiter)){
		lines.push_back(temp);
	}
	return (lines);
}
static auto StringUtil::Join(const std::vector<std::string> &input, const std::string &separator) -> std::string {
	std::string result;
	if(!input.empty()){
		result += input[0];
	}
	for(size_t i = 1;i < input.size();i++){
		result += separator;
		result += input[i];
	}
	return result; 
}
static auto StringUtil::Prefix(const std::string &str, const std::string &prefix) -> std::string {
	std::vector<std::string> lines = StringUtil::split(str, '\n');
	if(lines.empty()) return "";

	std::string result; 
	result += prefix + lines[0]; 
	for(size_t i = 1;i < lines.size();i++){
		result += std::endl; 
		result += prefix; 
		result += lines[i];
	}
	return result; 
}
static auto StringUtil::FormatSize(uint64_t bytes) -> std::string {
	double base = 1024;
	double kb = base;
	double mb = kb*base;
	double gb = mb*base;
	std::ostringstream oss; 
	if(bytes >= gb){
		oss << std::fixed() << std::setprecision(2) << (bytes / gb) << " GB";
	}else if(bytes >= mb){
		oss << std::fixed() << std::setprecision(2) << (bytes / mb) << " MB";
	}else if(bytes >= kb){
		oss << std::fixed() << std::setprecision(2) << (bytes / kb) << " KB";
	}else{
		oss << std::to_string(bytes) + " bytes";
	}
	return oss.str();
}
static auto StringUtil::Bold(const std::string &str) -> std::string {
	std::string set_plain_text = "\033[0;0m"; //wtf
	std::string set_bold_text = "\033[0;1m"; //wtf
	std::ostringstream oss; 
	oss << set_bold_text << str << set_plain_text; 
	return oss.str(); 
}
static auto StringUtil::Upper(const std::string &str) -> std::string {
	std::string copy(str);
	std::transform(copy.begin(), copy.end(), copy.begin(), [](unsigned char c){ return std::toupper(c); });
	return copy; 
}
static auto StringUtil::Lower(const std::string &str) -> std::string {
	std::string copy(str);
	std::transform(copy.begin(), copy.end(), copy.begin(), [](unsigned char c){ return std::tolower(c); });
	return copy;
}
/** @return string formatted with printf semantics */
// NOLINTNEXTLINE - "it wants us to take fmt_str as const&, but we shouldn't do that since we use it in va_args." - CMU database group, who this code is from 
static auto StringUtil::Format(std::string fmt_str, ...) -> std::string {
	// http://stackoverflow.com/a/8098080
	int final_n; 
	int n = (static_cast<int>(fmt_str.size())) * 2;
	std::string str; 
	std::unique_ptr<char[]> formatted; 
	va_list ap; 
	while(true){
		formatted = std::make_unique<char[]>(n); 
		strcpy(&formatted[0], fmt_str.c_str()); // NOLINT 	
		va_start(ap, fmt_str);
		final_n = vsnprintf(&formatted[0], static_cast<size_t>(n), fmt_str.c_str(), ap);
		va_end(ap);
		if(final_n > 0 || final_n >= n){
			n += abs(final_n - n + 1);
		}else{
			break;
		}
	}
	return {formatted.get()};
}
static auto StringUtil::Split(const std::string &input, const stda::string &split) -> std::vector<std::string> {
	std::vector<std::string> splits;
	size_t last = 0;
	size_t input_len = input.size();
	size_t split_len = split.size();
	while(last <= input_len){
		size_t next = input.find(split, last);
		if(next == std::string::npos) last = input_len;
		std::string substr = input.substr(last, next-last); //[last, next) 
		if(!substr.empty()) splits.push_back(substr); 
		last = next + split_len; 
	}
	return splits; 
}
static auto StringUtil::Count(const std::string &input, const std::string &str) -> size_t {
	size_t count = 0;
	size_t n_pos = input.find(str, 0);
	while(n_pos != std::string::npos){
		count++;
		n_pos = input.find(str, n_pos+1);
	}
	return count; 
}
//remove trailing ' ', \f \n \r \t \v 
static void StringUtil::RTrim(std::string *str) {
	str->erase(
		std::find_if(
			str->rbegin(), str->rend(), 
			[](int ch){ return std::isspace(ch) == 0;})
		.base(), 
		str.end()
	);
}
//remove leading ' ', \f \n \r \t \v
static void StringUtil::LTrim(std::string *str) {
	str->erase(
		str.begin(),
		std::find_if(
			str->begin(), str->end(), 
			[](int ch){ return std::isspace(ch) == 0;}
		)
	);
}

static auto StringUtil::Indent(int num_indent) -> std::string {
	return std::string(num_indent, ' '); //NOLINT
}
//creates a copy - wasteful, bad for performance critical code
static auto StringUtil::Strip(const std::string &str, char c) -> std::string {
	std::string temp = str;
	std::erase(temp, c);
	//the below methods also work for this erase 
		// temp.erase(std::remove(temp.begin(), temp.end(), c), temp.end());
		// std::erase_if(temp, [&c](char ch){ return ch == c; }

	return temp;
}
static auto StringUtil::Replace(std::string source, const std::string &from, const std::string &to) -> std::string {
	uint64_t start_pos = 0; 
	while((start_pos = source.find(from, start_pos)) != std::string::npos){
		source.replace(start_pos, from.length(), to); //remove chars [start_pos, start_pos+from.length()-1] and inserts "to" at start_pos
		start_pos += to.length(); //in case 'to' contains 'from' like 'to'=yx, 'from'=x
	}
	return source;
}
//lines -> lines_split 
//lines_split -> lines_str 
//lines_str -> return 

/*
 * ALTERNATIVE: iterate through until you cant find '\n'
 * for all '\n' pos -> insert indent_str, walk forward indent_str.length()+1 units, continue
 */
// static auto StringUtil::IndentAllLines(const std::string &lines, size_t num_indent, bool except_first_line = false) -> std::string {
// 	std::vector<std::string> lines_str;
// 	auto lines_split = StringUtil::Split(lines, '\n');
// 	lines_str.reserve(lines_split.size());
// 	auto indent_str = StringUtil::Indent(num_indent);
// 	if(!except_first_line){
// 		lines_str.push_back(indent_str + lines_split[0]); 
// 	}
// 	for(size_t i = 1;i < lines_split.size();i++){
// 		const auto& line = lines_split[i]; 
// 		lines_str.push_back(fmt::format("{}{}", indent_str, line));
// 	}
// 	return fmt::format("{}", fmt::join(lines_str, "\n"));
// }

//below func untested 
static auto StringUtil::IndentAllLines(const std::string& lines, size_t num_indent, bool except_first_line = false) -> std::string {
	std::string result = lines; 
	std::string indent_str = StringUtil::Indent(num_indent);
	size_t pos = 0;
	if(!except_first_line) result.insert(pos, indent_str); 
	while((pos = result.find('\n', pos)) != std::string::npos){ //find next '\n'
		//insert indent_str
		pos += 1;
		result.insert(pos, indent_str);
		//walk forward num_indent+1 
		pos += indent_str.length(); 
	}
	return result; 
}

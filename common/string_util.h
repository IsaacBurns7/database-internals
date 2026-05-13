#pragma once

#include <string>
#include <vector>

class StringUtil{
public:
	static auto Contains(const std::string &haystack, const std::string &needle) -> bool; 
	static auto ContainsAfter(const std::string &keyword, const std::string &haystack, const std::string &needle) -> bool; //this really shouldnt be khn it should be hnk (contains -> after) 
	static auto StartsWith(const std::string &str, const std::string &prefix) -> bool;
	static auto EndsWith(const std::string &str, const std::string &suffix) -> bool; 
	static auto Repeat(const std::string &str, std::size_t n) -> std::string;
	static auto Split(const std::string &str, char delimiter) -> std::vector<std::string>;
	static auto Join(const std::vector<std::string> &input, const std::string &separator) -> std::string;

	/* join multiple items of container with given size, transformed to string using function, into one string using the given separator 
	 */
	template <typename C, typename S, typename Func>
	static auto Join(const C &input, S count, const std::string &separator) -> std::string {
		std::string result; 
		if(count > 0)
			result += f(input[0]);
			//added so we dont have to put the if in the for loop 
		for(size_t i = 1;i < count; i++){
			result += separator + f(input[i]);
		}
		return result; 
	}
	static auto Prefix(const std::string &str, const std::string &prefix) -> std::string;
	static auto FormatSize(uint64_t bytes) -> std::string;
	static auto Bold(const std::string &str) -> std::string;
	static auto Upper(const std::string &str) -> std::string;
	static auto Lower(const std::string &str) -> std::string;
	static auto Format(std::string fmt_str, ...) -> std::string;
	static auto Split(const std::string &input, const std::string &split) -> std::vector<std::string>;
	static auto Count(const std::string &input, const std::string &str) -> size_t;
	static void RTrim(std::string *str);
	static void LTrim(std::string *str);
	static auto Indent(int num_indent) -> std::string;
	static auto Strip(const std::string &str, char c) -> std::string;
	static auto Replace(std::string source, const std::string &from, const std::string &to) -> std::string;
	static auto IndentAllLines(const std::string &lines, size_t num_indent, bool except_first_line = false) -> std::string;
};

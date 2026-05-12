//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// exception.h
//
// Identification: src/include/common/exception.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "type/type.h"

enum class ExceptionType{
	INVALID = 0,
	OUT_OF_RANGE=1, 
	CONVERSION=2,  //casting
	UNKNOWN_TYPE=3,
	DECIMAL=4,
	MISMATCH_TYPE=5,
	DIVIDE_BY_ZERO=6,
	INCOMPATIBLE_TYPE=8,
	OUT_OF_MEMORY=9,
	NOT_IMPLEMENTED=11,
	EXECUTION=12
};

//i dont know what the below is and I will look into ONCE multithreading is a thing
//extern std::atomic<bool> global_disable_execution_exception_print;

class Exception: public std::runtime_error{
public:
	explicit Exception(const std::string &message, bool print = True): std::runtime_error(message), type(ExceptionType::INVALID) {
	#ifndef NDEBUG
		if(print){
			std::string exception_message = "Message:: " + message + "\n";
			std::cerr << exception_message; 
		}
	#endif
	}
	auto GetType() const -> ExceptionType { return type_; }
	static auto ExceptionTypeToString(ExceptionType type) -> std::string {
		switch(type) {
			case ExceptionType::INVALID:
				return "Invalid"; 
			case ExceptionType::OUT_OF_RANGE:
				return "Out of Range";
			case ExceptionType::CONVERSION:
				return "Conversion";
			case ExceptionType::UNKNOWN_TYPE:
				return "Unknown Type";
			case ExceptionType::DECIMAL:
				return "Decimal";
			case ExceptionType::MISMATCH_TYPE:
				return "Mismatch Type";
			case ExceptionType::DIVIDE_BY_ZERO:
				return "Divide by Zero";
			case ExceptionType::INCOMPATIBLE_TYPE:
				return "Incompatible type";
			case ExceptionType::OUT_OF_MEMORY:
				return "Out of Memory";
			case ExceptionType::NOT_IMPLEMENTED:
				return "Not implemented";
			case ExceptionType::EXECUTION:
				return "Execution";
			default:
				return "Unknown";
		}
	}

private:
	ExceptionType type_;
};


#pragma once

#include <cassert>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>

//if "assert" asserts, print msg 
#define ASSERT_WITH_MESSAGE(expr, message) assert((expr) && (message))

namespace internal{ 

class LogFatalStream{
public:
	LogFatalStream(const char *file, int line): file_(file), line_(line) {} 
	~LogFatalStream(){
		std::cerr << file_ << ":" << line_ << ": " << log_stream_.str() << std::endl;
		std::abort(); 
	}
	template <typename T>
	auto operator<<(const T& val) -> LogFatalStream& {
		log_stream_ << val;
		return *this;
	}
private:
	const char *file_;
	int line_;
	std::ostringstream log_stream_;
};

}

//some NOLINTs not added for DISALLOW_COPY and DISALLOW_MOVE

//same as ASSERT_WITH_MESSAGE but with stream style params 
#define ASSERT_AND_LOG(expr)                       			/*NOLINT*/ \
  	if (bool val = (expr); !val) internal::LogFatalStream { /*NOLINT*/ \
    	__FILE__, __LINE__                                  /*NOLINT*/ \
    }
#define UNIMPLEMENTED(message) throw std::logic_error(message)
#define ENSURE(expr, message) 								\
	if(!(expr)){ 											\
		std::cerr << "ERROR: " << (message) << std::endl; 	\
		std::terminate(); 									\ 
	}
#define UNREACHABLE(message) throw std::logic_error(message)
#define DISALLOW_COPY(cname) 		\
	cname(const cname &) = delete; 	\
	auto operator=(const cname &)->cname & = delete;
#define DISALLOW_MOVE(cname) 	\
	cname(cname &&) = delete; 	\
	auto operator=(cname &&)->cname & = delete;
#define DISALLOW_COPY_AND_MOVE(cname) 	\
	DISALLOW_COPY(cname); 				\
	DISALLOW_MOVE(cname); 

exception - no external dependencies
macro - no external dependencies 
util/string_util - fmt/format and fmt/ranges (both 3rd-party)

fmt/format (third-party)
fmt/ranges (third-party)
BELOW
    # CMakeLists.txt
    find_package(fmt REQUIRED)
    target_link_libraries(your_target fmt::fmt)

I'm effectively just going to copy the CMU database group's type system from their "bustub" database.
I'm trying to make a database not a type system
I will walk through it so I understand it well, incase I ever want to utilize it myself, but its to me a third party ABI that I will know particularly well 
"Walk through" = copy line by line, ensuring I understand each line
in total it may be ~5000-8000 lines of code, but should take only about a week to walk through fully. I hope to have this walkthrough done by next week monday,
    and since it's less brain intensive than creative work, I'll spend that extra effort on leetcoding 
        this will basically be a break from leetcoding now... 
            perhaps I will review siddhartha as well...

plan 
walk through exception, macro, string_util 
walk through type(+type_id), value
walk through boolean
walk through numeric, integer_parent_type, <4 integer types> 
walk through numeric, decimal 
walk through varlen
walk through timestamp 
walk through value factory 
walk through vector
walk through type util

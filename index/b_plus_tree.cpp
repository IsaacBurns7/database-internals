#include "b_plus_tree.h"

template <typename... Args>
void Record<Args...>::serialize(char& buf, const RecordType<Args...>& record){
    std::apply([&](auto&&... args){
        ([&](auto&& x){
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_arithmetic_v<T>){
                //write to buf
                std::memcpy(buf, &x, sizeof(T));
                buf += sizeof(T);
            }else{
                //add uint32_t (4 bytes) for size, then the element 
                //probably have to figure out its size dynamically dependent on the actual type 
                //ADVANCED PROBABLY DO IN THE FUTURE: maybe allow them to pass in the "length" access function for each type
                    //for now just support std::string 
                if constexpr (std::is_same_v<T, std::string>){
                    std::memcpy(buf, x.size(), sizeof(std::string::size_type));
                    buf += sizeof(std::string::size_type);
                    std::memcpy(buf, x.data(), x.size());
                    buf += sizeof(x.size());
                }
            }
        }(args), ...); //invoked lambda per element
    }, record.fields);
}

template <typename... Args>
void Record<Args...>::deserialize(const char& buf, RecordType<Args...>& record){
    std::apply([&](auto&&... args){
        uint32_t index = 0;
		([&](auto&& x){
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_arithmetic_v<T>){
                std::memcpy(&x, buf+index, sizeof(T));
                index += sizeof(T);
            }else{
                if constexpr (std::is_same_v<T, std::string>){
                    //read in sizetype 
                    std::string::size_type string_size; 
                    std::memcpy(&string_size, buf+index, sizeof(string_size)); //maybe this should be a uint16_t or sth idk 
                    index += sizeof(string_size);
                    //read in data and size
                    x = std::string(buf, string_size);
                    index += string_size;
                }
            }
        }(args), ...);
    }, record.fields);
}

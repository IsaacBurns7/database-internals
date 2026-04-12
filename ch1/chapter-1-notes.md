# Below is a list of everything I don't understand from chapter 1 
!std::variant
!boost::pfr::get<N>(data[index]) //this is a struct,
V&&,
!boost::pfr::get_element_t<N, RecordStruct>,
std::forward<V>,
templated Func as lambda
typename std::decay_t<decltype(idx)>::key_type,
std::decay_t, 
::key_type as part of std::decay_t,
!std::visit(lambda, sit->second) //standard iterator over second element of a hashmap,
init = func(init, data.at(key)), func is lambda 
data.upper_bound, //map
data.lower_bound, //map
!is_same_v<IdxKeyType, FieldType>,
!boost::pfr::for_each_field,
!boost::pfr::tuple_size<RecordStruct>::value; 
!template<class... Ts> 

## Below are concepts not yet learned, but not part of this project, and encountered when going down a rabbit hole
Partial template specialization
Explicit/full template specialization 
Variadic templates plus pack expansion

# Notes after studying
## std::variant	and std::visit 
std::variant<T1, T2, T3> is a safe union. 
you can get try to get a certain type and it will throw error if wrong - get<type>
get_if<type> will return a nullptr if wrong 
std::visit takes in a callable, and applies it to the variant depending on the current type of the variant. because you only pass in 1 callable, you often have to pass in a varadiac template with pack expansion. what the fuck does this mean?

## Varadiac templates and pack expansion
template<class... Ts> //Ts is a "parameter pack" - it could be <int>, <int,float>, <int,float,string>, etc 
struct overloaded: Ts... //expands pack into a list of base classes. If you pass in 3 lambdas, Ts = {lambda1, lambda2, lambda3}, so Ts... is "struct overloaded: Lambda1, Lambda2, Lambda3"
using Ts::operator() expands to 
	using Lambda1::operator();
	using Lambda2::operator();
	using Lambda3::operator();
Ts...               // just the types:   T1, T2, T3
Ts::operator()...   // the whole thing:  T1::operator(), T2::operator(), T3::operator()
foo<Ts>...          // wraps each type:  foo<T1>, foo<T2>, foo<T3>

## Boost stuff 



.PHONY: test build cmake rescan clean
.PHONY: type_test

test:
	@echo "Configuring and building..."
	cmake -S . -B build && cmake --build build -j$$(nproc)
	@echo "Running tests..."
	ctest --test-dir build --output-on-failure

build: 
	@echo "Configuring and building..."
	cmake -S . -B build && cmake --build build -j$$(nproc)

cmake: 
	cmake -S . -B build

test_granular:
	cmake -S . -B build && cmake --build build -j $$(nproc)
	./build/storage_tests

type_test:
	cmake -S . -B build && cmake --build build --target type_tests -j$$(nproc)
	./build/type_tests

rescan:
	@echo "Nuking build directory..."
	rm -rf build
	mkdir -p build
	@echo "Configuring and building..."
	cmake -S . -B build && cmake --build build -j$$(nproc)
	@echo "Running tests..."
	ctest --test-dir build --output-on-failure

clean:
	rm -rf build

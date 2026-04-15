
.PHONY: test build cmake rescan clean

test:
	@echo "Configuring and building..."
	cd build && cmake .. && make -j$$(nproc)
	@echo "Running tests..."
	ctest --test-dir build --output-on-failure

build: 
	@echo "Configuring and building..."
	cd build && cmake .. && make -j$$(nproc)

cmake: 
	cd build && cmake .. && cd ..

test_granular:
	cd build && cmake .. && make -j $$(nproc) 
	./build/storage_tests

rescan:
	@echo "Nuking build directory..."
	rm -rf build
	mkdir -p build
	@echo "Configuring and building..."
	cd build && cmake .. && make -j$$(nproc)
	@echo "Running tests..."
	cd build && ctest --output-on-failure

clean:
	rm -rf build

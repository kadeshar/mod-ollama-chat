# Ensure the module is correctly registered before linking
if(TARGET modules)
    # Include nlohmann/json library
    if(EXISTS "${CMAKE_SOURCE_DIR}/deps/nlohmann")
        target_include_directories(modules PRIVATE ${CMAKE_SOURCE_DIR}/deps/nlohmann)
    else()
        # Try to find nlohmann-json via find_package (vcpkg)
        find_package(nlohmann_json CONFIG)
        if(nlohmann_json_FOUND)
            target_link_libraries(modules PRIVATE nlohmann_json::nlohmann_json)
        else()
            message(WARNING "nlohmann/json not found. Please install it or place in deps/nlohmann/")
        endif()
    endif()
    
    # Include cpp-httplib library (header-only)
    target_include_directories(modules PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
    
    # Enable SSL/TLS support for HTTPS connections
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND OR OPENSSL_FOUND)
        target_compile_definitions(modules PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
        target_link_libraries(modules PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        message(STATUS "[mod-ollama-chat] OpenSSL found - HTTPS support enabled")
    else()
        message(WARNING "[mod-ollama-chat] OpenSSL not found - only HTTP support available")
        message(STATUS "[mod-ollama-chat] To enable HTTPS: install OpenSSL development libraries")
        if(WIN32)
            message(STATUS "[mod-ollama-chat] Windows: Install vcpkg and run 'vcpkg install openssl'")
        else()
            message(STATUS "[mod-ollama-chat] Linux: apt install libssl-dev (Ubuntu/Debian) or yum install openssl-devel (RHEL/CentOS)")
        endif()
    endif()
    
    # Platform-specific threading and networking libraries
    if(WIN32)
        # Windows requires winsock for networking and additional SSL libraries
        target_link_libraries(modules PRIVATE ws2_32 crypt32)

        # Windows-specific settings
		set(CMAKE_CXX_STANDARD 17)
		set(CURL_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
		set(CURL_LIBRARY ${CMAKE_SOURCE_DIR}/lib/libcurl_a_debug.lib )
		set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")

		target_include_directories(modules PRIVATE "C:/vcpkg/vcpkg/installed/x64-windows-static/include")
		target_link_libraries(modules PRIVATE "C:/vcpkg/vcpkg/installed/x64-windows-static/lib/libcurl.lib")
		target_link_libraries(modules PRIVATE "C:/vcpkg/vcpkg/installed/x64-windows-static/lib/zlib.lib")
		
		add_definitions(-DCURL_STATICLIB)

		find_package(CURL REQUIRED)
		include_directories(${CURL_INCLUDE_DIR})
        
		
        # Explicitly include nlohmann-json path
        target_include_directories(modules PRIVATE "C:/vcpkg/vcpkg/installed/x64-windows-static/include/curl" "C:/src/azerothcore-wotlk-llm/modules/mod-ollama-chat/src/nlohmann")
    else()
        # Unix-like systems settings
        target_link_libraries(modules PRIVATE curl)
        # Linux/macOS requires pthread
        target_link_libraries(modules PRIVATE pthread)
    endif()
endif()
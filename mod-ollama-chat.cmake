# Ensure the module is correctly registered before linking
if(TARGET modules)
    if(WIN32)

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
        
        # Explicitly include nlohmann-json path
        target_include_directories(modules PRIVATE /usr/local/include /usr/local/include/nlohmann)
    endif()
endif()
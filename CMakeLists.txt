cmake_minimum_required(VERSION 3.1)
# note: cxx-17 requires cmake 3.8, cxx-20 requires cmake 3.12
set(COMPONENT_NAME aws-iot-pubsub-agent)
#project(basic-pub-sub CXX)
project(${COMPONENT_NAME})
file(GLOB SRC_FILES
       "*.cpp"
       "../../utils/CommandLineUtils.cpp"
       "../../utils/CommandLineUtils.h"
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 14)

#set warnings
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX /wd4068)
else ()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wno-long-long -pedantic -Werror)
endif ()

find_package(aws-crt-cpp REQUIRED)
install(TARGETS ${COMPONENT_NAME} DESTINATION sbin)

file(GLOB ROOTCA_FILE "configs/*.pem")
install(FILES ${ROOTCA_FILE} DESTINATION etc)

file(GLOB CONF_FILE "configs/*.conf")
install(FILES ${CONF_FILE} DESTINATION etc)

target_link_libraries(${PROJECT_NAME} AWS::aws-crt-cpp)
